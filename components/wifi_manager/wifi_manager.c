#include <string.h>
#include "freertos/FreeRTOS.h"
#include "wifi_manager.h"
#include "wifi_station.h"
#include "ssid_manager.h"
#include "wifi_config_ap.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#define ESP_PRODUCT_TOKEN       0

static const char *TAG = "wifi_manager";

void wifi_try_to_connect(void)
{
    int ssid_list = ssid_manager_get_count();

    if (ssid_list) {
        ESP_LOGI(TAG, "Starting WiFi connection attempt");

        wifi_sta_start();
    }
    else {
        // No SSID configured, enter config mode
        // Wait for the board version to be shown
        vTaskDelay(pdMS_TO_TICKS(1500));
        wifi_ap_start();
    }
}

void wifi_config_exit_requested(void) {
    ESP_LOGI(TAG, "Config exit requested from web");
    wifi_ap_stop();
    wifi_try_to_connect();
}

esp_err_t wifi_manager_init(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();

#if ESP_PRODUCT_TOKEN
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
#endif
    {
        ESP_LOGW(TAG, "Erasing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize TCP/IP
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Create event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ssid_manager_init();
    wifi_ap_set_ssid_prefix("esp32");
    wifi_ap_register_exit_requested_cb(wifi_config_exit_requested);

    wifi_try_to_connect();

    return ESP_OK;
}


