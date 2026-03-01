#include "ili9341.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ili9341";

/*============================================================================
 * ILI9341 Register Definitions
 *============================================================================*/

#define ILI9341_CMD_SWRESET     0x01
#define ILI9341_CMD_SLPOUT      0x11
#define ILI9341_CMD_DISPOFF     0x28
#define ILI9341_CMD_DISPON      0x29
#define ILI9341_CMD_CASET       0x2A
#define ILI9341_CMD_PASET       0x2B
#define ILI9341_CMD_RAMWR       0x2C
#define ILI9341_CMD_MADCTL      0x36
#define ILI9341_CMD_COLMOD      0x3A
#define ILI9341_CMD_SLPIN       0x10

/* MADCTL bit flags */
#define MADCTL_MY               (1 << 7)
#define MADCTL_MX               (1 << 6)
#define MADCTL_MV               (1 << 5)
#define MADCTL_BGR              (1 << 3)

#define ILI9341_WIDTH           240
#define ILI9341_HEIGHT          320

/*============================================================================
 * Singleton State
 *============================================================================*/

static struct {
    spi_device_handle_t spi;
    spi_host_device_t   spi_host;
    int                 dc_gpio;
    int                 rst_gpio;
    bool                bus_owned;
    bool                initialized;

    ili9341_rotation_t      rotation;
    ili9341_pixel_format_t  pixel_fmt;
    bool                    bgr_mode;

    ili9341_flush_done_cb_t flush_done_cb;
    void                   *flush_done_ctx;
    spi_transaction_t       async_trans;
} s_dev;

/*============================================================================
 * Low-Level SPI Helpers
 *============================================================================*/

static inline void spi_send_cmd(uint8_t cmd)
{
    gpio_set_level(s_dev.dc_gpio, 0);
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_dev.spi, &t);
}

static inline void spi_send_cmd_params(uint8_t cmd, const uint8_t *params, size_t len)
{
    spi_send_cmd(cmd);
    if (len > 0 && params != NULL) {
        gpio_set_level(s_dev.dc_gpio, 1);
        spi_transaction_t t = {
            .length    = len * 8,
            .tx_buffer = params,
        };
        spi_device_polling_transmit(s_dev.spi, &t);
    }
}

/*============================================================================
 * MADCTL Lookup Table
 *
 *   Rotation         MY  MX  MV  BGR    Value
 *   ─────────────────────────────────────────
 *   PORTRAIT_0       0   1   0   1      0x48
 *   LANDSCAPE_0      0   0   1   1      0x28
 *   PORTRAIT_180     1   0   0   1      0x88
 *   LANDSCAPE_180    1   1   1   1      0xE8
 *
 *============================================================================*/

static const uint8_t s_madctl_base[] = {
    [ILI9341_PORTRAIT_0]    = MADCTL_MX,                        /* 0x40 */
    [ILI9341_LANDSCAPE_0]   = MADCTL_MV,                        /* 0x20 */
    [ILI9341_PORTRAIT_180]  = MADCTL_MY,                        /* 0x80 */
    [ILI9341_LANDSCAPE_180] = MADCTL_MY | MADCTL_MX | MADCTL_MV, /* 0xE0 */
};

static void ili9341_apply_madctl(void)
{
    uint8_t val = s_madctl_base[s_dev.rotation];
    if (s_dev.bgr_mode) val |= MADCTL_BGR;
    spi_send_cmd_params(ILI9341_CMD_MADCTL, &val, 1);
}

/*============================================================================
 * ILI9341 Register Initialization Sequence
 *============================================================================*/

static void ili9341_init_sequence(void)
{
    /* Software reset */
    spi_send_cmd(ILI9341_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Display OFF during configuration */
    spi_send_cmd(ILI9341_CMD_DISPOFF);

    /* Power Control A */
    spi_send_cmd_params(0xCB, (const uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5);

    /* Power Control B */
    spi_send_cmd_params(0xCF, (const uint8_t[]){0x00, 0xC1, 0x30}, 3);

    /* Driver Timing Control A */
    spi_send_cmd_params(0xE8, (const uint8_t[]){0x85, 0x00, 0x78}, 3);

    /* Driver Timing Control B */
    spi_send_cmd_params(0xEA, (const uint8_t[]){0x00, 0x00}, 2);

    /* Power On Sequence Control */
    spi_send_cmd_params(0xED, (const uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4);

    /* Pump Ratio Control */
    spi_send_cmd_params(0xF7, (const uint8_t[]){0x20}, 1);

    /* Power Control 1 — GVDD level */
    spi_send_cmd_params(0xC0, (const uint8_t[]){0x23}, 1);

    /* Power Control 2 */
    spi_send_cmd_params(0xC1, (const uint8_t[]){0x10}, 1);

    /* VCOM Control 1 */
    spi_send_cmd_params(0xC5, (const uint8_t[]){0x3E, 0x28}, 2);

    /* VCOM Control 2 */
    spi_send_cmd_params(0xC7, (const uint8_t[]){0x86}, 1);

    /* Frame Rate Control — 70 Hz */
    spi_send_cmd_params(0xB1, (const uint8_t[]){0x00, 0x18}, 2);

    /* Display Function Control */
    spi_send_cmd_params(0xB6, (const uint8_t[]){0x08, 0x82, 0x27}, 3);

    /* Gamma Function Disable */
    spi_send_cmd_params(0xF2, (const uint8_t[]){0x00}, 1);

    /* Gamma Set — use curve 1 */
    spi_send_cmd_params(0x26, (const uint8_t[]){0x01}, 1);

    /* Positive Gamma Correction */
    spi_send_cmd_params(0xE0, (const uint8_t[]){
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
        0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03,
        0x0E, 0x09, 0x00
    }, 15);

    /* Negative Gamma Correction */
    spi_send_cmd_params(0xE1, (const uint8_t[]){
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
        0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C,
        0x31, 0x36, 0x0F
    }, 15);

    /* Sleep Out */
    spi_send_cmd(ILI9341_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/*============================================================================
 * Post-transfer callback (ISR context)
 *============================================================================*/

static IRAM_ATTR void spi_post_cb(spi_transaction_t *trans)
{
    if (trans->user == NULL) return;

    ili9341_flush_done_cb_t cb  = s_dev.flush_done_cb;
    void                   *ctx = s_dev.flush_done_ctx;
    if (cb != NULL) {
        cb(ctx);
    }
}

/*============================================================================
 * Public API
 *============================================================================*/

esp_err_t ili9341_init(const ili9341_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_dev, 0, sizeof(s_dev));
    s_dev.dc_gpio  = cfg->dc_gpio;
    s_dev.rst_gpio = cfg->rst_gpio;
    s_dev.spi_host = cfg->spi_host;

    /* --- DC pin as GPIO output --- */
    gpio_config_t dc_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << cfg->dc_gpio),
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&dc_conf));

    /* --- SPI bus --- */
    spi_bus_config_t bus = {
        .sclk_io_num     = cfg->clk_gpio,
        .mosi_io_num     = cfg->mosi_gpio,
        .miso_io_num     = cfg->miso_gpio,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = cfg->max_transfer_sz,
    };
    esp_err_t ret = spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO);
    if (ret == ESP_OK) {
        s_dev.bus_owned = true;
    } else if (ret == ESP_ERR_INVALID_STATE) {
        s_dev.bus_owned = false;
        ESP_LOGW(TAG, "SPI bus already initialised, attaching device only");
    } else {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* --- SPI device --- */
    spi_device_interface_config_t dev = {
        .clock_speed_hz = cfg->clk_speed_hz,
        .mode           = 0,
        .spics_io_num   = cfg->cs_gpio,
        .queue_size     = 7,
        .post_cb        = spi_post_cb,
    };
    ret = spi_bus_add_device(cfg->spi_host, &dev, &s_dev.spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed: %s", esp_err_to_name(ret));
        if (s_dev.bus_owned) spi_bus_free(cfg->spi_host);
        return ret;
    }

    /* --- Hardware reset (optional) --- */
    if (cfg->rst_gpio >= 0) {
        gpio_config_t rst_conf = {
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << cfg->rst_gpio),
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_conf);
        gpio_set_level(cfg->rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    /* --- ILI9341 register init --- */
    ili9341_init_sequence();

    /* Default settings: Portrait 0, RGB565, RGB */
    s_dev.rotation  = ILI9341_PORTRAIT_0;
    s_dev.pixel_fmt = ILI9341_PIXEL_FORMAT_RGB565;
    s_dev.bgr_mode  = false;

    ili9341_apply_madctl();

    uint8_t colmod = (uint8_t)s_dev.pixel_fmt;
    spi_send_cmd_params(ILI9341_CMD_COLMOD, &colmod, 1);

    spi_send_cmd(ILI9341_CMD_DISPON);

    s_dev.initialized = true;
    ESP_LOGI(TAG, "ILI9341 initialized (RGB565, RGB, Portrait 0)");
    return ESP_OK;
}

void ili9341_deinit(void)
{
    if (!s_dev.initialized) return;

    spi_send_cmd(ILI9341_CMD_DISPOFF);
    spi_send_cmd(ILI9341_CMD_SLPIN);
    vTaskDelay(pdMS_TO_TICKS(5));

    spi_bus_remove_device(s_dev.spi);
    if (s_dev.bus_owned) {
        spi_bus_free(s_dev.spi_host);
    }

    s_dev.initialized = false;
    memset(&s_dev, 0, sizeof(s_dev));
}

void ili9341_set_rotation(ili9341_rotation_t rot)
{
    if (!s_dev.initialized) return;
    if (rot > ILI9341_LANDSCAPE_180) return;

    s_dev.rotation = rot;
    ili9341_apply_madctl();

    ESP_LOGD(TAG, "Rotation set to %d", rot);
}

void ili9341_register_flush_done_cb(ili9341_flush_done_cb_t cb,
                                    void *user_ctx)
{
    s_dev.flush_done_cb  = cb;
    s_dev.flush_done_ctx = user_ctx;
}

void ili9341_draw(uint16_t x1, uint16_t y1,
                  uint16_t x2, uint16_t y2,
                  const void *data)
{
    if (!s_dev.initialized) return;

    /* RGB565: 2 bytes per pixel */
    size_t len = (size_t)(x2 - x1 + 1) * (y2 - y1 + 1) * 2;

    /* Set window (CASET + PASET + RAMWR) */
    const uint8_t col[4] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF};
    spi_send_cmd_params(ILI9341_CMD_CASET, col, 4);

    const uint8_t row[4] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    spi_send_cmd_params(ILI9341_CMD_PASET, row, 4);

    spi_send_cmd(ILI9341_CMD_RAMWR);

    /* Send pixel data */
    gpio_set_level(s_dev.dc_gpio, 1);

    if (s_dev.flush_done_cb != NULL) {
        /* Async DMA transfer */
        memset(&s_dev.async_trans, 0, sizeof(spi_transaction_t));
        s_dev.async_trans.length    = len * 8;
        s_dev.async_trans.tx_buffer = data;
        s_dev.async_trans.user      = (void *)1;   /* non-NULL → trigger callback */

        esp_err_t ret = spi_device_queue_trans(s_dev.spi, &s_dev.async_trans, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "spi_device_queue_trans failed: %s", esp_err_to_name(ret));
            s_dev.flush_done_cb(s_dev.flush_done_ctx);
        }
    } else {
        /* Blocking transfer */
        spi_transaction_t t = {
            .length    = len * 8,
            .tx_buffer = data,
        };
        spi_device_polling_transmit(s_dev.spi, &t);
    }
}

void ili9341_draw_wait(void)
{
    if (!s_dev.initialized) return;
    spi_transaction_t *finished = NULL;
    spi_device_get_trans_result(s_dev.spi, &finished, portMAX_DELAY);
}

void ili9341_set_pixel_format(ili9341_pixel_format_t fmt)
{
    if (!s_dev.initialized) return;
    s_dev.pixel_fmt = fmt;
    uint8_t val = (uint8_t)fmt;
    spi_send_cmd_params(ILI9341_CMD_COLMOD, &val, 1);

    ESP_LOGD(TAG, "Pixel format set to 0x%02X", val);
}

void ili9341_set_bgr(bool bgr)
{
    if (!s_dev.initialized) return;
    s_dev.bgr_mode = bgr;
    ili9341_apply_madctl();

    ESP_LOGD(TAG, "Color order set to %s", bgr ? "BGR" : "RGB");
}

/**
 * @brief Convert 0xRRGGBB → RGB565 big-endian (ready for SPI).
 */
static inline uint16_t rgb888_to_565be(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >>  8) & 0xFF;
    uint8_t b =  color        & 0xFF;

    uint16_t c565 = ((r & 0xF8) << 8)
                   | ((g & 0xFC) << 3)
                   | ((b & 0xF8) >> 3);

    /* Swap to big-endian for SPI byte order */
    return (c565 >> 8) | (c565 << 8);
}

void ili9341_fill_screen(uint32_t color)
{
    if (!s_dev.initialized) return;

    uint16_t w = ili9341_get_width();
    uint16_t h = ili9341_get_height();

    /* One full row of pixels */
    uint16_t pixel = rgb888_to_565be(color);
    uint16_t line[w];
    for (int i = 0; i < w; i++) {
        line[i] = pixel;
    }

    /* Fill row by row */
    for (uint16_t y = 0; y < h; y++) {
        ili9341_draw(0, y, w - 1, y, line);
    }
}

/*============================================================================
 * Getters API
 *============================================================================*/

uint16_t ili9341_get_width(void)
{
    return (s_dev.rotation == ILI9341_LANDSCAPE_0 ||
            s_dev.rotation == ILI9341_LANDSCAPE_180)
           ? ILI9341_HEIGHT : ILI9341_WIDTH;
}

uint16_t ili9341_get_height(void)
{
    return (s_dev.rotation == ILI9341_LANDSCAPE_0 ||
            s_dev.rotation == ILI9341_LANDSCAPE_180)
           ? ILI9341_WIDTH : ILI9341_HEIGHT;
}
