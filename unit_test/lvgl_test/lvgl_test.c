#include "lvgl_test.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "lvgl_test";

#define TEST_LCD_H_RES               320
#define TEST_LCD_V_RES               240
#define TEST_LCD_HOST                SPI2_HOST
#define TEST_LCD_CLK_GPIO            14
#define TEST_LCD_MOSI_GPIO           13
#define TEST_LCD_MISO_GPIO           12
#define TEST_LCD_CS_GPIO             15
#define TEST_LCD_DC_GPIO             2
#define TEST_LCD_RST_GPIO            -1
#define TEST_LCD_BL_GPIO             21
#define TEST_LCD_PIXEL_CLOCK_HZ      (20 * 1000 * 1000)

void test_lvgl(void)
{
    ESP_LOGI(TAG, "Initializing standalone LVGL for rotation test...");

    // 1. Backlight
    gpio_config_t bl_conf = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << TEST_LCD_BL_GPIO) };
    gpio_config(&bl_conf);
    gpio_set_level(TEST_LCD_BL_GPIO, 1);

    // 2. SPI Bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = TEST_LCD_CLK_GPIO, .mosi_io_num = TEST_LCD_MOSI_GPIO, .miso_io_num = TEST_LCD_MISO_GPIO,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = TEST_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    spi_bus_initialize(TEST_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // 3. Panel IO & Panel
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TEST_LCD_DC_GPIO, .cs_gpio_num = TEST_LCD_CS_GPIO, .pclk_hz = TEST_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 10,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TEST_LCD_HOST, &io_config, &io_handle);

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TEST_LCD_RST_GPIO, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle);
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, false);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    // 4. LVGL Port
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = TEST_LCD_H_RES * 40,
        .double_buffer = true,
        .hres = TEST_LCD_H_RES,
        .vres = TEST_LCD_V_RES,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false }, // Default
        .flags = { .buff_dma = 1 },
    };
    
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.swap_bytes = 1;
#endif

    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);

    // 5. Build UI
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "LVGL 9.4 Rotation Test\nTop-Left Marker");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 10);
    
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Center Button");
    lvgl_port_unlock();
}
