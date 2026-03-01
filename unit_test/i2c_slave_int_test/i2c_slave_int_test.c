#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C_SLAVE_INT";

// ============= Định nghĩa I2C =============
#define I2C_SLAVE_SCL_IO           22
#define I2C_SLAVE_SDA_IO           21
#define I2C_SLAVE_NUM              I2C_NUM_0
#define I2C_SLAVE_ADDR             0x28
#define I2C_SLAVE_TX_BUF_LEN       128
#define I2C_SLAVE_RX_BUF_LEN       128

// ============= Định nghĩa GPIO Interrupt =============
#define GPIO_INT_PIN               4
#define GPIO_INT_PIN_SEL           (1ULL << GPIO_INT_PIN)
#define ESP_INTR_FLAG_DEFAULT      0

// ============= Định nghĩa Packet =============
#define PACKET_HEADER              0xAA
#define MAX_DATA_LENGTH            32
#define PACKET_OVERHEAD            4

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

// ============= Biến toàn cục =============
static QueueHandle_t gpio_evt_queue = NULL;
static SemaphoreHandle_t i2c_data_ready_sem = NULL;
static uint8_t rx_buffer[I2C_SLAVE_RX_BUF_LEN];
static uint8_t tx_buffer[I2C_SLAVE_TX_BUF_LEN];
static data_packet_t response_packet;
static volatile bool data_pending = false;

// ============= ISR Handler (Top Half) =============
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Gửi event vào queue để xử lý ở bottom half
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    
    // Yield nếu cần
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ============= Hàm tính checksum =============
static uint8_t calculate_checksum(uint8_t *data, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

// ============= Hàm validate packet =============
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

// ============= Hàm tạo response packet =============
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

// ============= Hàm serialize packet =============
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

// ============= Hàm deserialize packet =============
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

// ============= Xử lý commands từ Master =============
static void process_command(data_packet_t *rx_packet) {
    ESP_LOGI(TAG, "Processing command: 0x%02X", rx_packet->command);
    
    switch (rx_packet->command) {
        case CMD_READ_SENSOR: {
            // Giả lập đọc cảm biến
            uint8_t sensor_data[4];
            sensor_data[0] = 25;  // Nhiệt độ: 25°C
            sensor_data[1] = 60;  // Độ ẩm: 60%
            sensor_data[2] = 0x12;
            sensor_data[3] = 0x34;
            
            create_response_packet(CMD_ACK, sensor_data, 4);
            ESP_LOGI(TAG, "Sensor data sent: Temp=%d, Humidity=%d", 
                     sensor_data[0], sensor_data[1]);
            break;
        }
        
        case CMD_WRITE_CONFIG: {
            ESP_LOGI(TAG, "Config received: %d bytes", rx_packet->length);
            for (int i = 0; i < rx_packet->length; i++) {
                ESP_LOGI(TAG, "  Config[%d] = 0x%02X", i, rx_packet->data[i]);
            }
            
            create_response_packet(CMD_ACK, NULL, 0);
            break;
        }
        
        case CMD_GET_STATUS: {
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
}

// ============= Task xử lý I2C (Bottom Half) =============
static void i2c_processing_task(void *arg) {
    data_packet_t rx_packet;
    
    ESP_LOGI(TAG, "I2C processing task started");
    
    while (1) {
        // Chờ semaphore từ GPIO interrupt task
        if (xSemaphoreTake(i2c_data_ready_sem, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Semaphore received, reading I2C data...");
            
            // Đọc dữ liệu từ I2C slave buffer
            int len = i2c_slave_read_buffer(I2C_SLAVE_NUM, rx_buffer, 
                                            I2C_SLAVE_RX_BUF_LEN, 
                                            100 / portTICK_PERIOD_MS);
            
            if (len > 0) {
                ESP_LOGI(TAG, "Received %d bytes from Master", len);
                
                // Deserialize packet
                if (deserialize_packet(rx_buffer, len, &rx_packet)) {
                    // Validate packet
                    if (validate_packet(&rx_packet)) {
                        // Xử lý command
                        process_command(&rx_packet);
                        
                        // Serialize response vào tx_buffer
                        size_t response_len = serialize_packet(&response_packet, tx_buffer);
                        
                        // Ghi response vào I2C slave buffer để Master đọc
                        int written = i2c_slave_write_buffer(I2C_SLAVE_NUM, tx_buffer, 
                                                             response_len, 
                                                             100 / portTICK_PERIOD_MS);
                        
                        if (written > 0) {
                            ESP_LOGI(TAG, "Response sent: %d bytes", written);
                        } else {
                            ESP_LOGE(TAG, "Failed to write response");
                        }
                    } else {
                        ESP_LOGE(TAG, "Invalid packet received");
                        // Gửi NACK
                        create_response_packet(CMD_NACK, NULL, 0);
                        size_t nack_len = serialize_packet(&response_packet, tx_buffer);
                        i2c_slave_write_buffer(I2C_SLAVE_NUM, tx_buffer, nack_len, 
                                               100 / portTICK_PERIOD_MS);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to deserialize packet");
                }
            } else if (len == 0) {
                ESP_LOGW(TAG, "No data available in I2C buffer");
            } else {
                ESP_LOGE(TAG, "I2C read error: %d", len);
            }
            
            // Reset flag
            data_pending = false;
        }
    }
}

// ============= Task xử lý GPIO Event (Bottom Half) =============
static void gpio_event_task(void *arg) {
    uint32_t io_num;
    
    ESP_LOGI(TAG, "GPIO event task started");
    
    while (1) {
        // Chờ event từ ISR
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGI(TAG, "GPIO[%lu] interrupt triggered!", io_num);
            
            // Đọc trạng thái pin để xác nhận
            int pin_state = gpio_get_level(io_num);
            ESP_LOGI(TAG, "GPIO[%lu] state: %d", io_num, pin_state);
            
            // Nếu pin ở mức cao (Master đang báo hiệu)
            if (pin_state == 1) {
                // Đánh dấu có dữ liệu đang chờ
                data_pending = true;
                
                // Delay nhỏ để Master hoàn thành việc gửi data qua I2C
                vTaskDelay(5 / portTICK_PERIOD_MS);
                
                // Báo hiệu cho I2C processing task bắt đầu đọc
                xSemaphoreGive(i2c_data_ready_sem);
                
                ESP_LOGI(TAG, "Data ready signal sent to I2C task");
            }
        }
    }
}

// ============= Khởi tạo GPIO Interrupt =============
static void gpio_interrupt_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,        // Ngắt cạnh lên
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = GPIO_INT_PIN_SEL,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    gpio_config(&io_conf);
    
    // Tạo queue cho GPIO events
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    
    // Install ISR service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    
    // Gắn ISR handler cho pin
    gpio_isr_handler_add(GPIO_INT_PIN, gpio_isr_handler, (void*) GPIO_INT_PIN);
    
    ESP_LOGI(TAG, "GPIO interrupt configured on pin %d", GPIO_INT_PIN);
}

// ============= Khởi tạo I2C Slave =============
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

// ============= Main Application =============
void app_main(void) {
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "Starting I2C Slave with GPIO Interrupt");
    ESP_LOGI(TAG, "===========================================");
    
    // Tạo semaphore để đồng bộ giữa GPIO task và I2C task
    i2c_data_ready_sem = xSemaphoreCreateBinary();
    if (i2c_data_ready_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }
    
    // Khởi tạo I2C Slave
    ESP_ERROR_CHECK(i2c_slave_init());
    ESP_LOGI(TAG, "✓ I2C slave initialized at address 0x%02X", I2C_SLAVE_ADDR);
    
    // Khởi tạo GPIO Interrupt
    gpio_interrupt_init();
    ESP_LOGI(TAG, "✓ GPIO interrupt initialized on pin %d", GPIO_INT_PIN);
    
    // Tạo task xử lý GPIO events (Bottom Half)
    xTaskCreate(gpio_event_task, "gpio_event_task", 2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "✓ GPIO event task created");
    
    // Tạo task xử lý I2C (Bottom Half)
    xTaskCreate(i2c_processing_task, "i2c_processing_task", 4096, NULL, 9, NULL);
    ESP_LOGI(TAG, "✓ I2C processing task created");
    
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "System ready and waiting for interrupts...");
    ESP_LOGI(TAG, "===========================================");
}
