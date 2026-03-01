#include "touch_test.h"
#include "esp_lcd_touch_xpt2046.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch_test";

#define TEST_TOUCH_SPI_HOST          SPI3_HOST
#define TEST_TOUCH_CS_GPIO           33
#define TEST_TOUCH_CLK_GPIO          25
#define TEST_TOUCH_MOSI_GPIO         32
#define TEST_TOUCH_MISO_GPIO         39
#define TEST_TOUCH_IRQ_GPIO          -1
#define TEST_TOUCH_CLOCK_HZ          (2 * 1000 * 1000)

static void touch_test_task(void *pvParameters)
{
    uint16_t x[1], y[1], strength[1];
    uint8_t count;
    esp_lcd_touch_handle_t tp_handle = (esp_lcd_touch_handle_t)pvParameters;

    ESP_LOGI(TAG, "Touch calibration test started. Use these RAW values for calibration.");
    while (1) {
        esp_lcd_touch_read_data(tp_handle);
        bool pressed = esp_lcd_touch_get_coordinates(tp_handle, x, y, strength, &count, 1);
        if (pressed && count > 0) {
            ESP_LOGI(TAG, "RAW Touch: X=%d, Y=%d", x[0], y[0]);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void test_touch_calibration(void)
{
    ESP_LOGI(TAG, "Initializing standalone touch for calibration test...");

    // 1. SPI Bus for Touch
    spi_bus_config_t buscfg = {
        .mosi_io_num = TEST_TOUCH_MOSI_GPIO, .miso_io_num = TEST_TOUCH_MISO_GPIO, .sclk_io_num = TEST_TOUCH_CLK_GPIO,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 0,
    };
    spi_bus_initialize(TEST_TOUCH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // 2. XPT2046 Config
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 320, .y_max = 240,
        .rst_gpio_num = -1, .int_gpio_num = TEST_TOUCH_IRQ_GPIO,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };

    xpt2046_spi_config_t spi_device = {
        .cs_gpio_num = TEST_TOUCH_CS_GPIO,
        .spi_freq_hz = TEST_TOUCH_CLOCK_HZ,
        .spi_host = TEST_TOUCH_SPI_HOST
    };

    esp_lcd_touch_handle_t tp_handle = NULL;
    esp_lcd_touch_new_spi_xpt2046(&spi_device, &tp_cfg, &tp_handle);

    xTaskCreate(touch_test_task, "touch_test_task", 4096, tp_handle, 5, NULL);
}
