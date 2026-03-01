#include "display_test.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "ili9341.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEST_LCD_H_RES              320
#define TEST_LCD_V_RES              240
#define TEST_LCD_HOST               SPI2_HOST
#define TEST_LCD_CLK_GPIO           14
#define TEST_LCD_MOSI_GPIO          13
#define TEST_LCD_MISO_GPIO          12
#define TEST_LCD_CS_GPIO            15
#define TEST_LCD_DC_GPIO            2
#define TEST_LCD_RST_GPIO           -1
#define TEST_LCD_BL_GPIO            21
#define TEST_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)

// Set this to 1 to use custom direct-SPI driver, 0 to use ESP-LCD component
#define USE_CUSTOM_DRIVER            1

static const char *TAG = "display_test";

static const ili9341_lcd_init_cmd_t lcd_init_cmds[] = {
    /* Power Control A */
    {0xCB, (uint8_t []){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    /* Power Control B */
    {0xCF, (uint8_t []){0x00, 0xC1, 0x30}, 3, 0},
    /* Driver Timing Control A */
    {0xE8, (uint8_t []){0x85, 0x00, 0x78}, 3, 0},
    /* Driver Timing Control B */
    {0xEA, (uint8_t []){0x00, 0x00}, 2, 0},
    /* Power On Sequence Control */
    {0xED, (uint8_t []){0x64, 0x03, 0x12, 0x81}, 4, 0},
    /* Pump Ratio Control */
    {0xF7, (uint8_t []){0x20}, 1, 0},
    /* Power Control 1 — GVDD level */
    {0xC0, (uint8_t []){0x23}, 1, 0},
    /* Power Control 2 */
    {0xC1, (uint8_t []){0x10}, 1, 0},
    /* VCOM Control 1 */
    {0xC5, (uint8_t []){0x3E, 0x28}, 2, 0},
    /* VCOM Control 2 */
    {0xC7, (uint8_t []){0x86}, 1, 0},
    /* Frame Rate Control — 70 Hz */
    {0xB1, (uint8_t []){0x00, 0x18}, 2, 0},
    /* Display Function Control */
    {0xB6, (uint8_t []){0x08, 0x82, 0x27}, 3, 0},
    /* Gamma Function Disable */
    {0xF2, (uint8_t []){0x00}, 1, 0},
    /* Gamma Set — use curve 1 */
    {0x26, (uint8_t []){0x01}, 1, 0},
    /* Positive Gamma Correction */
    {0xE0, (uint8_t []){ 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15, 0},
    /* Negative Gamma Correction */
    {0xE1, (uint8_t []){ 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15, 0},
};

const ili9341_vendor_config_t vendor_config = {
    .init_cmds      = lcd_init_cmds,
    .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
};

static void display_test_task(void *pvParameters)
{
#if USE_CUSTOM_DRIVER
    (void) pvParameters;
#else
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)pvParameters;
#endif
    uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
    int color_idx = 0;

    ESP_LOGI(TAG, "Starting drawing loop...");
    while (1) {
        uint16_t color = colors[color_idx];
        for (int y = 0; y < TEST_LCD_V_RES; y++) {
            for (int x = 0; x < TEST_LCD_H_RES; x++) {
#if USE_CUSTOM_DRIVER
                ili9341_draw(x, y, x + 1, y + 1, &color);
#else
                esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, &color);
#endif
            }

            vTaskDelay(1);
        }

        color_idx = (color_idx + 1) % (sizeof(colors) / sizeof(colors[0]));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void test_display_raw(void)
{
    ESP_LOGI(TAG, "Initializing standalone display for raw test...");

#if USE_CUSTOM_DRIVER

    /* Backlight ON */
    gpio_config_t bl_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TEST_LCD_BL_GPIO),
    };
    gpio_config(&bl_conf);
    gpio_set_level(TEST_LCD_BL_GPIO, 1);

    ili9341_config_t cfg = {
        .spi_host           = TEST_LCD_HOST,
        .clk_gpio           = TEST_LCD_CLK_GPIO,
        .mosi_gpio          = TEST_LCD_MOSI_GPIO,
        .miso_gpio          = TEST_LCD_MISO_GPIO,
        .cs_gpio            = TEST_LCD_CS_GPIO,
        .dc_gpio            = TEST_LCD_DC_GPIO,
        .rst_gpio           = TEST_LCD_RST_GPIO,
        .clk_speed_hz       = TEST_LCD_PIXEL_CLOCK_HZ,
        .max_transfer_sz    = TEST_LCD_H_RES * 80 * sizeof(uint16_t),
    };

    ili9341_init(&cfg);
    ili9341_set_rotation(ILI9341_LANDSCAPE_0);
    xTaskCreate(display_test_task, "display_test_task", 4096, NULL, 5, NULL);
#else
    // 1. Backlight
    gpio_config_t bl_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TEST_LCD_BL_GPIO),
    };
    gpio_config(&bl_conf);
    gpio_set_level(TEST_LCD_BL_GPIO, 1);

    // 2. SPI Bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = TEST_LCD_CLK_GPIO,
        .mosi_io_num = TEST_LCD_MOSI_GPIO,
        .miso_io_num = TEST_LCD_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TEST_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    spi_bus_initialize(TEST_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // 3. Panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TEST_LCD_DC_GPIO,
        .cs_gpio_num = TEST_LCD_CS_GPIO,
        .pclk_hz = TEST_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TEST_LCD_HOST, &io_config, &io_handle);

    // 4. LCD Panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TEST_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = (void *)&vendor_config,
    };
    esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle);

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_swap_xy(panel_handle, false);
    esp_lcd_panel_mirror(panel_handle, false, false);
    esp_lcd_panel_invert_color(panel_handle, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);
    xTaskCreate(display_test_task, "display_test_task", 4096, panel_handle, 5, NULL);
#endif
}
