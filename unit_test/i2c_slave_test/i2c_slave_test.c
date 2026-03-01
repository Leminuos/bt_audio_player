// i2c_slave_esp32.c
#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C_SLAVE";

// Định nghĩa I2C
#define I2C_SLAVE_SCL_IO           22
#define I2C_SLAVE_SDA_IO           21
#define I2C_SLAVE_NUM              I2C_NUM_0
#define I2C_SLAVE_ADDR             0x28
#define I2C_SLAVE_TX_BUF_LEN       128
#define I2C_SLAVE_RX_BUF_LEN       128

// Định nghĩa Packet
#define PACKET_HEADER              0xAA
#define MAX_DATA_LENGTH            32
#define PACKET_OVERHEAD            4  // Header + Command + Length + Checksum

// Commands
typedef enum {
    CMD_READ_SENSOR = 0x01,
    CMD_WRITE_CONFIG = 0x02,
    CMD_GET_STATUS = 0x03,
    CMD_RESET = 0x04,
    CMD_ACK = 0x10,
    CMD_NACK = 0x11
} packet_command_t;

// Cấu trúc Packet
typedef struct {
    uint8_t header;
    uint8_t command;
    uint8_t length;
    uint8_t data[MAX_DATA_LENGTH];
    uint8_t checksum;
} data_packet_t;

// Buffer cho giao tiếp
static uint8_t rx_buffer[I2C_SLAVE_RX_BUF_LEN];
static uint8_t tx_buffer[I2C_SLAVE_TX_BUF_LEN];
static data_packet_t response_packet;

// Hàm tính checksum
static uint8_t calculate_checksum(uint8_t *data, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

// Hàm validate packet
static bool validate_packet(data_packet_t *packet) {
    if (packet->header != PACKET_HEADER) {
        ESP_LOGW(TAG, "Invalid header: 0x%02X", packet->header);
        return false;
    }
    
    if (packet->length > MAX_DATA_LENGTH) {
        ESP_LOGW(TAG, "Invalid length: %d", packet->length);
        return false;
    }
    
    uint8_t calc_checksum = packet->header ^ packet->command ^ packet->length;
    for (int i = 0; i < packet->length; i++) {
        calc_checksum ^= packet->data[i];
    }
    
    if (calc_checksum != packet->checksum) {
        ESP_LOGW(TAG, "Checksum mismatch: calc=0x%02X, recv=0x%02X", 
                 calc_checksum, packet->checksum);
        return false;
    }
    
    return true;
}

// Hàm tạo response packet
static void create_response_packet(uint8_t command, uint8_t *data, uint8_t length) {
    response_packet.header = PACKET_HEADER;
    response_packet.command = command;
    response_packet.length = length;
    
    if (data != NULL && length > 0) {
        memcpy(response_packet.data, data, length);
    }
    
    response_packet.checksum = response_packet.header ^ 
                                response_packet.command ^ 
                                response_packet.length;
    for (int i = 0; i < length; i++) {
        response_packet.checksum ^= response_packet.data[i];
    }
}

// Hàm serialize packet thành buffer
static size_t serialize_packet(data_packet_t *packet, uint8_t *buffer) {
    size_t idx = 0;
    buffer[idx++] = packet->header;
    buffer[idx++] = packet->command;
    buffer[idx++] = packet->length;
    
    for (int i = 0; i < packet->length; i++) {
        buffer[idx++] = packet->data[i];
    }
    
    buffer[idx++] = packet->checksum;
    return idx;
}

// Hàm deserialize buffer thành packet
static bool deserialize_packet(uint8_t *buffer, size_t len, data_packet_t *packet) {
    if (len < PACKET_OVERHEAD) {
        return false;
    }
    
    packet->header = buffer[0];
    packet->command = buffer[1];
    packet->length = buffer[2];
    
    if (len < (PACKET_OVERHEAD + packet->length)) {
        return false;
    }
    
    for (int i = 0; i < packet->length; i++) {
        packet->data[i] = buffer[3 + i];
    }
    
    packet->checksum = buffer[3 + packet->length];
    return true;
}

// Xử lý commands từ Master
static void process_command(data_packet_t *rx_packet) {
    ESP_LOGI(TAG, "Processing command: 0x%02X", rx_packet->command);
    
    switch (rx_packet->command) {
        case CMD_READ_SENSOR: {
            // Giả lập đọc cảm biến (ví dụ: nhiệt độ và độ ẩm)
            uint8_t sensor_data[4];
            sensor_data[0] = 25;  // Nhiệt độ: 25°C
            sensor_data[1] = 60;  // Độ ẩm: 60%
            sensor_data[2] = 0x12; // Dữ liệu bổ sung
            sensor_data[3] = 0x34;
            
            create_response_packet(CMD_ACK, sensor_data, 4);
            ESP_LOGI(TAG, "Sensor data sent: Temp=%d, Humidity=%d", 
                     sensor_data[0], sensor_data[1]);
            break;
        }
        
        case CMD_WRITE_CONFIG: {
            // Xử lý cấu hình
            ESP_LOGI(TAG, "Config received: %d bytes", rx_packet->length);
            for (int i = 0; i < rx_packet->length; i++) {
                ESP_LOGI(TAG, "  Config[%d] = 0x%02X", i, rx_packet->data[i]);
            }
            
            create_response_packet(CMD_ACK, NULL, 0);
            break;
        }
        
        case CMD_GET_STATUS: {
            // Trả về trạng thái
            uint8_t status_data[2];
            status_data[0] = 0x01;  // Status OK
            status_data[1] = 0x55;  // Version
            
            create_response_packet(CMD_ACK, status_data, 2);
            ESP_LOGI(TAG, "Status sent: 0x%02X", status_data[0]);
            break;
        }
        
        case CMD_RESET: {
            ESP_LOGI(TAG, "Reset command received");
            create_response_packet(CMD_ACK, NULL, 0);
            break;
        }
        
        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", rx_packet->command);
            create_response_packet(CMD_NACK, NULL, 0);
            break;
    }
    
    // Serialize response vào tx_buffer
    serialize_packet(&response_packet, tx_buffer);
}

// Task xử lý I2C
static void i2c_slave_task(void *arg) {
    data_packet_t rx_packet;
    
    while (1) {
        // Đọc dữ liệu từ Master
        int len = i2c_slave_read_buffer(I2C_SLAVE_NUM, rx_buffer, 
                                        I2C_SLAVE_RX_BUF_LEN, 
                                        1000 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            ESP_LOGI(TAG, "Received %d bytes", len);
            
            // Deserialize packet
            if (deserialize_packet(rx_buffer, len, &rx_packet)) {
                // Validate packet
                if (validate_packet(&rx_packet)) {
                    // Xử lý command
                    process_command(&rx_packet);
                    
                    // Ghi response vào buffer để Master đọc
                    size_t response_len = 4 + response_packet.length;
                    i2c_slave_write_buffer(I2C_SLAVE_NUM, tx_buffer, 
                                           response_len, 
                                           1000 / portTICK_PERIOD_MS);
                    
                    ESP_LOGI(TAG, "Response sent: %d bytes", response_len);
                } else {
                    ESP_LOGE(TAG, "Invalid packet received");
                    // Gửi NACK
                    create_response_packet(CMD_NACK, NULL, 0);
                    serialize_packet(&response_packet, tx_buffer);
                    i2c_slave_write_buffer(I2C_SLAVE_NUM, tx_buffer, 4, 
                                           1000 / portTICK_PERIOD_MS);
                }
            } else {
                ESP_LOGE(TAG, "Failed to deserialize packet");
            }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Khởi tạo I2C Slave
static esp_err_t i2c_slave_init(void) {
    i2c_config_t conf_slave = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SLAVE_SDA_IO,
        .scl_io_num = I2C_SLAVE_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = I2C_SLAVE_ADDR,
        .slave.maximum_speed = 100000,
    };
    
    esp_err_t err = i2c_param_config(I2C_SLAVE_NUM, &conf_slave);
    if (err != ESP_OK) {
        return err;
    }
    
    return i2c_driver_install(I2C_SLAVE_NUM, conf_slave.mode, 
                             I2C_SLAVE_RX_BUF_LEN, 
                             I2C_SLAVE_TX_BUF_LEN, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting I2C Slave (ESP32)");
    
    // Khởi tạo I2C
    ESP_ERROR_CHECK(i2c_slave_init());
    ESP_LOGI(TAG, "I2C slave initialized at address 0x%02X", I2C_SLAVE_ADDR);
    
    // Tạo task xử lý
    xTaskCreate(i2c_slave_task, "i2c_slave_task", 4096, NULL, 10, NULL);
}
