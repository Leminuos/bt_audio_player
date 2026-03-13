#include "display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"

#include "lvgl.h"

static const char *TAG = "display";

/*============================================================================
 * Private Data
 *============================================================================*/

static void *s_buf1 = NULL;
static void *s_buf2 = NULL;
static lv_display_t *s_disp = NULL;
static lv_indev_t *s_indev = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;
static esp_timer_handle_t s_tick_timer = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;

/*--- UI hook queue: replaces mutex, all LVGL access from single task ---*/

typedef struct {
    display_ui_cb_t cb;
    void *arg;
    TaskHandle_t caller;   /* non-NULL → sync: give task notification when done */
} ui_hook_msg_t;

#define UI_HOOK_QUEUE_SIZE  8
static QueueHandle_t s_ui_hook_queue = NULL;

/*============================================================================
 * Backlight Control
 *============================================================================*/

static esp_err_t display_brightness_init(void)
{
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = LCD_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_BACKLIGHT_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = false
    };
 
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&LCD_backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&LCD_backlight_channel));
 
    return ESP_OK;
}

esp_err_t display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);

    uint32_t duty_cycle = (1023 * brightness_percent) / 100;

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_LEDC_CH, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_LEDC_CH));

    return ESP_OK;
}

esp_err_t display_backlight_off(void)
{
    return display_brightness_set(0);
}

esp_err_t display_backlight_on(void)
{
    return display_brightness_set(100);
}

/*============================================================================
 * LCD
 *============================================================================*/

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
    /* Frame Rate Control — 79 Hz */
    {0xB1, (uint8_t []){0x00, 0x13}, 2, 0},
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

static esp_err_t display_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    // 1) Init SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = LCD_MISO_GPIO,
        .sclk_io_num = LCD_CLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_DRAWBUF_SIZE * sizeof(uint16_t),
    };

    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI bus already initialized, reusing");
            ret = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // 2) Create panel IO
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = LCD_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io_handle));

    // 3) Create LCD panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PIXEL,
        .vendor_config  = (void *)&vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(s_io_handle, &panel_config, &s_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    esp_lcd_panel_mirror(s_panel_handle, LCD_MIRROR_X, LCD_MIRROR_Y);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    return ret;
}

/*============================================================================
 * Touch
 *============================================================================*/

#define XPT2046_CMD_X  0xD0  // X position
#define XPT2046_CMD_Y  0x90  // Y position

static void display_touch_init(void)
{
    gpio_set_direction(TOUCH_CLK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(TOUCH_MOSI_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(TOUCH_MISO_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(TOUCH_CS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(TOUCH_CS_GPIO, 1);  // CS inactive
}

static uint16_t touch_spi_xfer(uint8_t cmd)
{
    uint16_t result = 0;

    gpio_set_level(TOUCH_CS_GPIO, 0);

    // Send 8-bit command
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(TOUCH_MOSI_GPIO, (cmd >> i) & 1);
        gpio_set_level(TOUCH_CLK_GPIO, 1);
        esp_rom_delay_us(1);
        gpio_set_level(TOUCH_CLK_GPIO, 0);
        esp_rom_delay_us(1);
    }

    // 1 busy bit (skip)
    gpio_set_level(TOUCH_CLK_GPIO, 1);
    esp_rom_delay_us(1);
    gpio_set_level(TOUCH_CLK_GPIO, 0);
    esp_rom_delay_us(1);

    // Read 12-bit result
    for (int i = 11; i >= 0; i--) {
        gpio_set_level(TOUCH_CLK_GPIO, 1);
        esp_rom_delay_us(1);
        if (gpio_get_level(TOUCH_MISO_GPIO)) {
            result |= (1 << i);
        }
        gpio_set_level(TOUCH_CLK_GPIO, 0);
        esp_rom_delay_us(1);
    }

    gpio_set_level(TOUCH_CS_GPIO, 1);
    return result;
}

static void touch_read_raw(uint16_t *x, uint16_t *y, bool *pressed)
{
    // Read Z pressure first to detect touch
    uint16_t z1 = touch_spi_xfer(0xB0);  // Z1
    uint16_t z2 = touch_spi_xfer(0xC0);  // Z2

    if (z1 > 100 && z2 < 3900) {
        *x = touch_spi_xfer(XPT2046_CMD_X);
        *y = touch_spi_xfer(XPT2046_CMD_Y);
        *pressed = true;
    } else {
        *pressed = false;
    }
}

/*============================================================================
 * LVGL
 *============================================================================*/

static bool notify_lvgl_flush_ready(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_get_tick_cb(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // Get the LCD handle and pixel offset
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2; 
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    // because SPI LCD is big-endian, we need to swap the RGB bytes order
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));

    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t raw_x, raw_y;
    bool pressed;

    touch_read_raw(&raw_x, &raw_y, &pressed);

    if (pressed) {
        // Calibration + mapping (adjust these values for your board)
        int x = (int)(raw_x - 200) * 240 / (3700 - 200);
        int y = (int)(raw_y - 240) * 320 / (3800 - 240);

        // Clamp
        if (x < 0) x = 0;
        if (x >= 240) x = 240 - 1;
        if (y < 0) y = 0;
        if (y >= 320) y = 320 - 1;

        // Apply mirror/swap (swap_xy=1, mirror_x=true, mirror_y=false)
        int temp = x;           // swap XY
        x = y;
        y = temp;

        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void resolution_changed_event_cb(lv_event_t * e)
{
    lv_display_t * disp = (lv_display_t *)lv_event_get_target(e);
    int32_t hor_res = lv_display_get_horizontal_resolution(disp);
    int32_t ver_res = lv_display_get_vertical_resolution(disp);
    lv_display_rotation_t rot = lv_display_get_rotation(disp);

    /* handle rotation */
    switch(rot) {
        case LV_DISPLAY_ROTATION_0:
            /* Portrait orientation */
            break;
        case LV_DISPLAY_ROTATION_90:
            /* Landscape orientation */
            break;
        case LV_DISPLAY_ROTATION_180:
            /* Portrait orientation, flipped */
            break;
        case LV_DISPLAY_ROTATION_270:
            /* Landscape orientation, flipped */
            break;
    }
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = LVGL_TASK_MIN_DELAY_MS;
    ui_hook_msg_t msg;

    while (1) {
        /* Block on queue — wakes immediately when a hook is posted,
         * or after time_till_next_ms for the next lv_timer_handler() call.
         * This replaces the old vTaskDelay + mutex pattern. */
        TickType_t wait = pdMS_TO_TICKS(time_till_next_ms);

        while (xQueueReceive(s_ui_hook_queue, &msg, wait) == pdTRUE) {
            if (msg.cb) msg.cb(msg.arg);
            if (msg.caller) xTaskNotifyGive(msg.caller);
            wait = 0;   /* drain remaining hooks without blocking */
        }

        time_till_next_ms = lv_timer_handler();

        if (time_till_next_ms < LVGL_TASK_MIN_DELAY_MS) time_till_next_ms = LVGL_TASK_MIN_DELAY_MS;
        if (time_till_next_ms > LVGL_TASK_MAX_DELAY_MS) time_till_next_ms = LVGL_TASK_MAX_DELAY_MS;
    }
}

static esp_err_t display_lvgl_init(void)
{
    /* ---------- 1. Init LVGL core ---------- */

    lv_init();

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_get_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    s_ui_hook_queue = xQueueCreate(UI_HOOK_QUEUE_SIZE, sizeof(ui_hook_msg_t));
    if (s_ui_hook_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI hook queue");
        return ESP_FAIL;
    }

    /* ---------- 2. Tạo LVGL display ---------- */

    size_t draw_buffer_sz = LCD_DRAWBUF_SIZE * sizeof(lv_color16_t);
    s_buf1 = (lv_color_t *)heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(s_buf1);

#if LCD_DOUBLE_BUFFER
    s_buf2 = (lv_color_t *)heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    assert(s_buf2);
#endif

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return ESP_FAIL;
    }

    lv_display_set_buffers(s_disp, s_buf1, s_buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Đăng ký flush callback — LVGL gọi khi cần gửi pixel xuống LCD */
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);

    lv_display_add_event_cb(s_disp, resolution_changed_event_cb, LV_EVENT_RESOLUTION_CHANGED, NULL);

    /* Lưu panel_handle vào user_data để flush_cb có thể truy cập được */
    lv_display_set_user_data(s_disp, s_panel_handle);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io_handle, &cbs, s_disp));

    /* Color byte swap: ILI9341 nhận big-endian RGB565,
     * nhưng ESP32 là little-endian → cần swap bytes */
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    /* ---------- 3. Tạo touch input device ---------- */

    s_indev = lv_indev_create();
    if (s_indev == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL input device");
        return ESP_FAIL;
    }

    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvgl_touch_read_cb);
    lv_indev_set_user_data(s_indev, NULL);
    lv_indev_set_display(s_indev, s_disp);

    /* ---------- 4. Tạo LVGL task ------------------ */

    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_port_task,
        "lvgl_task",
        LVGL_TASK_STACK_SIZE,
        NULL,
        LVGL_TASK_PRIORITY,
        &s_lvgl_task_handle,
        1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool display_run_on_ui(display_ui_cb_t cb, void *arg)
{
    if (!s_ui_hook_queue || !cb) return false;
    ui_hook_msg_t msg = { .cb = cb, .arg = arg, .caller = NULL };
    return xQueueSend(s_ui_hook_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool display_run_on_ui_sync(display_ui_cb_t cb, void *arg)
{
    if (!s_ui_hook_queue || !cb) return false;
    ui_hook_msg_t msg = {
        .cb     = cb,
        .arg    = arg,
        .caller = xTaskGetCurrentTaskHandle()
    };
    if (xQueueSend(s_ui_hook_queue, &msg, portMAX_DELAY) != pdTRUE) return false;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return true;
}

/*============================================================================
 * Initialization
 *============================================================================*/

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display driver");

    display_brightness_init();

    display_lcd_init();

    display_touch_init();

    display_lvgl_init();

    display_brightness_set(75);
    
    ESP_LOGI(TAG, "Display driver initialized (%dx%d)", LCD_H_RES, LCD_V_RES);

    return ESP_OK;
}

void display_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing display subsystem");

    /* 1) Dừng LVGL task trước tiên
     *    Nếu không dừng task mà xoá display/indev → task gọi lv_timer_handler()
     *    trên object đã bị free → crash */
    if (s_lvgl_task_handle != NULL) {
        vTaskDelete(s_lvgl_task_handle);
        s_lvgl_task_handle = NULL;
    }

    /* 2) Dừng tick timer
     *    Sau khi task đã dừng, tick timer không còn ý nghĩa
     *    Phải stop trước khi delete */
    if (s_tick_timer != NULL) {
        esp_timer_stop(s_tick_timer);
        esp_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }

    /* 3) Xoá LVGL objects
     *    indev trước vì nó reference tới display
     *    display sau vì nó độc lập */
    if (s_indev != NULL) {
        lv_indev_delete(s_indev);
        s_indev = NULL;
    }

    if (s_disp != NULL) {
        lv_display_delete(s_disp);
        s_disp = NULL;
    }

    /* 4) Deinit LVGL core
     *    Phải gọi sau khi đã xoá hết display/indev */
    lv_deinit();

    /* 5) Free draw buffers
     *    Chỉ free SAU khi LVGL đã deinit — vì lv_deinit có thể còn
     *    truy cập buffer trong quá trình cleanup */
    if (s_buf1 != NULL) {
        heap_caps_free(s_buf1);
        s_buf1 = NULL;
    }
    if (s_buf2 != NULL) {
        heap_caps_free(s_buf2);
        s_buf2 = NULL;
    }

    /* 6) Tắt LCD display và backlight */
    if (s_panel_handle != NULL) {
        esp_lcd_panel_disp_on_off(s_panel_handle, false);  /* tắt display trước */
    }
    display_backlight_off();

    /* 8) Xoá LCD panel + panel IO
     *    Panel trước (nó dùng IO), IO sau
     *    Phải xoá trước khi free SPI bus */
    if (s_panel_handle != NULL) {
        esp_lcd_panel_del(s_panel_handle);
        s_panel_handle = NULL;
    }
    if (s_io_handle != NULL) {
        esp_lcd_panel_io_del(s_io_handle);
        s_io_handle = NULL;
    }

    /* 9) Free SPI buses
     *    Chỉ free khi không còn device nào gắn trên bus */
    spi_bus_free(LCD_HOST);
    spi_bus_free(TOUCH_SPI_HOST);

    /* 10) Xoá hook queue cuối cùng */
    if (s_ui_hook_queue != NULL) {
        vQueueDelete(s_ui_hook_queue);
        s_ui_hook_queue = NULL;
    }

    ESP_LOGI(TAG, "Display subsystem deinitialized");
}
