/*
 * ESP32 A2DP Sink - Nhận audio qua Bluetooth A2DP
 * ESP-IDF 5.4+
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "bt_audio_snk";

// Cấu hình I2S cho audio output
#define I2S_NUM             I2S_NUM_0
#define I2S_BCK_IO          GPIO_NUM_26  // Bit Clock
#define I2S_WS_IO           GPIO_NUM_25  // Word Select (LRCK)
#define I2S_DO_IO           GPIO_NUM_22  // Data Out
#define I2S_SAMPLE_RATE     44100
#define I2S_CHANNELS        2
#define I2S_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT

static i2s_chan_handle_t tx_handle = NULL;

/* Khởi tạo I2S cho audio output */
static esp_err_t i2s_init(void)
{
    esp_err_t ret = ESP_OK;

    // Cấu hình I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    
    ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Cấu hình I2S standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS_PER_SAMPLE, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S standard mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized: %d Hz, %d-bit, %s", 
            I2S_SAMPLE_RATE,
            I2S_BITS_PER_SAMPLE,
            I2S_CHANNELS == 2 ? "Stereo" : "Mono"
    );

    return ret;
}

/* Callback nhận dữ liệu audio từ A2DP */
static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    if (tx_handle == NULL) {
        return;
    }
    
    size_t bytes_written = 0;
    
    // Ghi dữ liệu audio vào I2S
    esp_err_t ret = i2s_channel_write(tx_handle, data, len, &bytes_written, portMAX_DELAY);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
    }
    
    if (bytes_written < len) {
        ESP_LOGW(TAG, "I2S underrun: %d bytes written, %lu bytes expected", bytes_written, len);
    }
}

/* Callback cho A2DP sink */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        uint8_t *bda = param->conn_stat.remote_bda;
        ESP_LOGI(TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED ? "Connected" : "Disconnected",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
        ESP_LOGI(TAG, "A2DP audio state: %s",
                 param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED ? "Started" : 
                 param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED ? "Stopped" : "Suspended");
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT: {
        ESP_LOGI(TAG, "A2DP audio codec configured");
        esp_a2d_cb_param_t *p = (esp_a2d_cb_param_t *)param;
        
        // In thông tin codec (SBC)
        if (p->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            int sample_rate = 16000;
            char oct0 = p->audio_cfg.mcc.cie.sbc[0];
            
            if (oct0 & (0x01 << 6)) {
                sample_rate = 32000;
            } else if (oct0 & (0x01 << 5)) {
                sample_rate = 44100;
            } else if (oct0 & (0x01 << 4)) {
                sample_rate = 48000;
            }
            
            ESP_LOGI(TAG, "Configure audio codec: SBC, sample rate: %d", sample_rate);
        }
        break;
    }
    default:
        ESP_LOGI(TAG, "A2DP event: %d", event);
        break;
    }
}

/* Callback cho GAP */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_CFM_REQ_EVT - Please confirm passkey: %lu", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %lu", param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_REQ_EVT - Please enter passkey");
        break;
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
        break;
    default:
        ESP_LOGI(TAG, "GAP event: %d", event);
        break;
    }
}

void bt_audio_snk(void)
{
    // 1. Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting bluetooth audio sink");

    // 2. Khởi tạo I2S
    i2s_init();

    // 3. Khởi tạo Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize controller: %s", esp_err_to_name(ret));
        return;
    }

    // 4. Enable Bluetooth controller
    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable controller: %s", esp_err_to_name(ret));
        return;
    }

    // 5. Khởi tạo Bluedroid stack
    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bluedroid: %s", esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable bluedroid: %s", esp_err_to_name(ret));
        return;
    }

    // 6. Đăng ký callback cho GAP và A2DP
    esp_bt_gap_register_callback(bt_app_gap_cb);
    
    // 7. Khởi tạo A2DP Sink
    if ((ret = esp_a2d_register_callback(bt_app_a2d_cb)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register A2DP callback: %s", esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register data callback: %s", esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_a2d_sink_init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize A2DP sink: %s", esp_err_to_name(ret));
        return;
    }

    // 8. Thiết lập device name
    esp_bt_gap_set_device_name("ESP32_AUDIO_SNK");

    // 9. Thiết lập Simple Secure Pairing
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // 10. Thiết lập discoverable và connectable mode
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(TAG, "A2DP Sink initialized. Device is discoverable as 'ESP32_AUDIO_SNK'");
    
    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}