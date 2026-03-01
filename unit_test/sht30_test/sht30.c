/**
 * @file sht30_i2c_example.c
 * @brief Chương trình đọc cảm biến SHT30 qua I2C với ESP32
 * @note ESP-IDF 5.4
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "SHT30_I2C";

// Cấu hình I2C
#define I2C_MASTER_SCL_IO           22        // GPIO cho SCL
#define I2C_MASTER_SDA_IO           21        // GPIO cho SDA
#define I2C_MASTER_FREQ_HZ          100000    // Tần số I2C 100kHz

// Địa chỉ SHT30
#define SHT30_SENSOR_ADDR           0x44      // Địa chỉ mặc định SHT30

// Lệnh SHT30
#define SHT30_CMD_MEASURE_HIGH      0x2C06    // Đo với độ lặp lại cao, clock stretching enabled
#define SHT30_CMD_MEASURE_MEDIUM    0x2C0D    // Đo với độ lặp lại trung bình
#define SHT30_CMD_MEASURE_LOW       0x2C10    // Đo với độ lặp lại thấp
#define SHT30_CMD_SOFT_RESET        0x30A2    // Soft reset
#define SHT30_CMD_READ_STATUS       0xF32D    // Đọc thanh ghi trạng thái

// Biến toàn cục
i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t dev_handle;

/**
 * @brief Tính toán CRC8 cho SHT30
 * @param data Dữ liệu cần tính CRC
 * @param len Độ dài dữ liệu
 * @return Giá trị CRC8
 */
static uint8_t sht30_crc8(const uint8_t *data, int len)
{
    const uint8_t POLYNOMIAL = 0x31;
    uint8_t crc = 0xFF;

    for (int j = len; j; --j) {
        crc ^= *data++;

        for (int i = 8; i; --i) {
            crc = (crc & 0x80) ? (crc << 1) ^ POLYNOMIAL : (crc << 1);
        }
    }
    return crc;
}

/**
 * @brief Khởi tạo I2C master
 * @return ESP_OK nếu thành công
 */
static esp_err_t i2c_master_init(void)
{
    ESP_LOGI(TAG, "I2C master initialization...");

    // Cấu hình I2C bus
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // Cấu hình thiết bị SHT30
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT30_SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    ESP_LOGI(TAG, "I2C master initialized successfully");
    return ESP_OK;
}

/**
 * @brief Gửi lệnh đến SHT30
 * @param cmd Lệnh 16-bit
 * @return ESP_OK nếu thành công
 */
static esp_err_t sht30_send_command(uint16_t cmd)
{
    uint8_t cmd_buf[2];
    cmd_buf[0] = (cmd >> 8) & 0xFF;  // MSB
    cmd_buf[1] = cmd & 0xFF;         // LSB

    esp_err_t ret = i2c_master_transmit(dev_handle, cmd_buf, sizeof(cmd_buf), 1000);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send command: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Đọc dữ liệu nhiệt độ và độ ẩm từ SHT30
 * @param temperature Con trỏ lưu giá trị nhiệt độ (°C)
 * @param humidity Con trỏ lưu giá trị độ ẩm (%)
 * @return ESP_OK nếu thành công
 */
static esp_err_t sht30_read_data(float *temperature, float *humidity)
{
    esp_err_t ret;
    uint8_t data[6]; // 2 byte temp + 1 CRC + 2 byte hum + 1 CRC

    // Gửi lệnh đo
    ret = sht30_send_command(SHT30_CMD_MEASURE_HIGH);
    if (ret != ESP_OK) {
        return ret;
    }

    // Đọc 6 byte dữ liệu
    int max_retries = 20; // Tối đa 20 lần thử
    for (int i = 0; i < max_retries; i++) {
        
        ret = i2c_master_receive(dev_handle, data, sizeof(data), 100);
        if (ret == ESP_OK) break;

        vTaskDelay(pdMS_TO_TICKS(1)); // Chờ 1ms mỗi lần
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read data timeout");
        return ret;
    }

    // Kiểm tra CRC cho nhiệt độ
    uint8_t temp_crc = sht30_crc8(&data[0], 2);
    if (temp_crc != data[2]) {
        ESP_LOGE(TAG, "Temperature CRC mismatch! Expected: 0x%02X, Got: 0x%02X", 
                 temp_crc, data[2]);
        return ESP_ERR_INVALID_CRC;
    }

    // Kiểm tra CRC cho độ ẩm
    uint8_t hum_crc = sht30_crc8(&data[3], 2);
    if (hum_crc != data[5]) {
        ESP_LOGE(TAG, "Humidity CRC mismatch! Expected: 0x%02X, Got: 0x%02X", 
                 hum_crc, data[5]);
        return ESP_ERR_INVALID_CRC;
    }

    // Tính toán nhiệt độ
    uint16_t temp_raw = (data[0] << 8) | data[1];
    *temperature = -45.0f + 175.0f * (temp_raw / 65535.0f);

    // Tính toán độ ẩm
    uint16_t hum_raw = (data[3] << 8) | data[4];
    *humidity = 100.0f * (hum_raw / 65535.0f);

    return ESP_OK;
}

/**
 * @brief Reset SHT30
 * @return ESP_OK nếu thành công
 */
static esp_err_t sht30_soft_reset(void)
{
    ESP_LOGI(TAG, "Performing soft reset...");
    esp_err_t ret = sht30_send_command(SHT30_CMD_SOFT_RESET);
    
    if (ret == ESP_OK)
    {
        vTaskDelay(pdMS_TO_TICKS(10)); // Chờ reset hoàn tất
        ESP_LOGI(TAG, "Soft reset successful");
    }

    return ret;
}

/**
 * @brief Task chính đọc cảm biến
 */
void sht30_task(void *pvParameters)
{
    float temperature, humidity;

    while (1) {
        esp_err_t ret = sht30_read_data(&temperature, &humidity);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Temperature: %.2f °C, Humidity: %.2f %%", temperature, humidity);
        } else {
            ESP_LOGE(TAG, "Failed to read sensor data");
        }

        // Đọc mỗi 2 giây
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void sht30_test(void)
{
    ESP_LOGI(TAG, "Starting SHT30 I2C Master Example");

    // Khởi tạo I2C
    ESP_ERROR_CHECK(i2c_master_init());

    // Reset cảm biến
    ESP_ERROR_CHECK(sht30_soft_reset());

    // Tạo task đọc cảm biến
    xTaskCreate(sht30_task, "sht30_task", 4096, NULL, 5, NULL);
}