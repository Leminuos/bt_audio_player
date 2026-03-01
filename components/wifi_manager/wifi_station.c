#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_station.h"
#include "ssid_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#define WIFI_EVENT_CONNECTED_BIT    BIT0
#define WIFI_EVENT_STOPPED_BIT      BIT1
#define WIFI_EVENT_SCAN_DONE_BIT    BIT2
#define WIFI_MAX_RECONNECT_COUNT    5

static const char *TAG = "wifi_manager";

static int s_reconnect_num = 0;
static int8_t s_max_tx_power;
static uint8_t s_remember_bssid;
static EventGroupHandle_t s_sta_event_group;
static esp_timer_handle_t s_timer_scan_handle;
static int s_scan_min_interval_microseconds = 10 * 1000 * 1000;      // Default 10 seconds
static int s_scan_max_interval_microseconds = 300 * 1000 * 1000;     // Default 5 minutes
static int s_scan_current_interval_microsecond = 10 * 1000 * 1000;  // Current interval

// Static queue management
static WifiApRecord *connect_queue = NULL;
static int connect_queue_size = 0;
static int connect_queue_capacity = 0;

static void wifi_sta_start_connect(void);
static void wifi_handle_scan_result(void);
static void wifi_sta_scan_timer_callback(void* arg);
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// Helper function to add item to queue
static void connect_queue_clear(void);
static bool connect_queue_is_empty(void);
static WifiApRecord connect_queue_pop_front(void);
static void connect_queue_push_back(const WifiApRecord *record);

void wifi_sta_start(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        s_max_tx_power = 0;
        s_remember_bssid = 0;
    } else {
        err = nvs_get_i8(nvs, "max_tx_power", &s_max_tx_power);
        if (err != ESP_OK) s_max_tx_power = 0;

        err = nvs_get_u8(nvs, "remember_bssid", &s_remember_bssid);
        if (err != ESP_OK) s_remember_bssid = 0;

        nvs_close(nvs);
    }

    s_sta_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register( WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                wifi_sta_event_handler,
                                                NULL ));

    ESP_ERROR_CHECK(esp_event_handler_register( IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                wifi_sta_event_handler,
                                                NULL ));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Setup the timer to scan WiFi
    esp_timer_create_args_t timer_args = {
        .callback = wifi_sta_scan_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "WiFiScanTimer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer_scan_handle));
}

void wifi_sta_deinit(void) {

    ESP_LOGI(TAG, "Stopping WiFi station");

    // Stop timer
    if (s_timer_scan_handle) {
        esp_timer_stop(s_timer_scan_handle);
        esp_timer_delete(s_timer_scan_handle);
        s_timer_scan_handle = NULL;
    }

    if (s_sta_event_group) {
        vEventGroupDelete(s_sta_event_group);
        s_sta_event_group = NULL;
    }
}

bool wifi_sta_wait_for_connected(int timeout_ms) {
    // Wait for either connected or stopped event
    EventBits_t bits = 0;

    if (!s_sta_event_group) return false;
    
    bits = xEventGroupWaitBits( s_sta_event_group,
                                WIFI_EVENT_CONNECTED_BIT | WIFI_EVENT_STOPPED_BIT, 
                                pdFALSE,
                                pdFALSE,
                                timeout_ms / portTICK_PERIOD_MS );

    // Return true only if connected (not if stopped)
    return (bits & WIFI_EVENT_CONNECTED_BIT) != 0;
}

void wifi_sta_set_scan_interval_range (int min_interval_seconds, int max_interval_seconds) {
    s_scan_min_interval_microseconds = min_interval_seconds * 1000 * 1000;
    s_scan_max_interval_microseconds = max_interval_seconds * 1000 * 1000;
    s_scan_current_interval_microsecond = s_scan_min_interval_microseconds;
}

void wifi_sta_update_scan_interval() {
    // Apply exponential backoff: double the interval, up to max
    if (s_scan_current_interval_microsecond < s_scan_max_interval_microseconds) {
        s_scan_current_interval_microsecond *= 2;
        if (s_scan_current_interval_microsecond > s_scan_max_interval_microseconds) {
            s_scan_current_interval_microsecond = s_scan_max_interval_microseconds;
        }
    }
}

int8_t wifi_sta_get_rssi(void)
{
    return 0;
}

static void wifi_sta_scan_timer_callback(void* arg) {
    esp_wifi_scan_start(NULL, false);
}

static void wifi_handle_scan_result(void) {
    ESP_LOGI(TAG, "--- Scan result ---");

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_num * sizeof(wifi_ap_record_t));
    if (!ap_records) return;

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

    int ssid_cnt = ssid_manager_get_count();
    const wifi_credentials_t* ssid_list_saved = ssid_manager_get_ssid_list();

    for (int i = 0; i < ap_num; i++) {
        wifi_ap_record_t ap_record = ap_records[i];

        for (int ssid_idx = 0; ssid_idx < ssid_cnt; ssid_idx++) {
            if (strcmp(ssid_list_saved[ssid_idx].ssid, (char*)ap_record.ssid) == 0) {
                ESP_LOGI(TAG, "Found AP: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x, RSSI: %d, Channel: %d, Authmode: %d",
                        (char *)ap_record.ssid, 
                        ap_record.bssid[0], ap_record.bssid[1], ap_record.bssid[2],
                        ap_record.bssid[3], ap_record.bssid[4], ap_record.bssid[5],
                        ap_record.rssi, ap_record.primary, ap_record.authmode);
                
                WifiApRecord record = {
                    .ssid = {0},
                    .password = {0},
                    .channel = ap_record.primary,
                    .authmode = ap_record.authmode,
                    .bssid = {0}
                };

                memcpy(record.bssid, ap_record.bssid, 6);
                memcpy(record.ssid, ssid_list_saved[ssid_idx].ssid, MAX_SSID_LENGTH);
                memcpy(record.password, ssid_list_saved[ssid_idx].password, MAX_PASSWORD_LENGTH);

                connect_queue_push_back(&record);
            }
        }

    }

    free(ap_records);
    
    ESP_LOGI(TAG, "-------------------");

    if (connect_queue_is_empty()) {
        ESP_LOGI(TAG, "No AP found, next scan in %d seconds", 
                 s_scan_current_interval_microsecond / 1000 / 1000);
        esp_timer_start_once(s_timer_scan_handle, s_scan_current_interval_microsecond);
        wifi_sta_update_scan_interval();
        return;
    }

    wifi_sta_start_connect();
}

static void wifi_sta_start_connect(void) {
    WifiApRecord ap_record = connect_queue_pop_front();

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    
    strcpy((char *)wifi_config.sta.ssid, ap_record.ssid);
    strcpy((char *)wifi_config.sta.password, ap_record.password);
    
    // If remember_bssid is enabled
    bool remember_bssid = true;  // This should be a static variable
    if (remember_bssid) {
        wifi_config.sta.channel = ap_record.channel;
        memcpy(wifi_config.sta.bssid, ap_record.bssid, 6);
        wifi_config.sta.bssid_set = true;
    }
    
    wifi_config.sta.listen_interval = 10;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT)
    {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
            {
                ESP_LOGI(TAG, "WiFi scanning");
                esp_wifi_scan_start(NULL, false);
                break;
            }

            case WIFI_EVENT_SCAN_DONE:
            {
                wifi_handle_scan_result();
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED:
            {
                if (s_reconnect_num < WIFI_MAX_RECONNECT_COUNT) {
                    s_reconnect_num++;
                    ESP_LOGI(TAG, "Reconnecting (attempt %d / %d)", s_reconnect_num, WIFI_MAX_RECONNECT_COUNT);
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "Connect failed");
                }

                break;
            }

            default:
                break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
                s_reconnect_num = 0;
                xEventGroupSetBits(s_sta_event_group, WIFI_EVENT_CONNECTED_BIT);
                break;
            }
            
            default:
                break;
        }
    }
}

static void connect_queue_push_back(const WifiApRecord *record) {
    if (connect_queue_size >= connect_queue_capacity) {
        // Expand capacity
        int new_capacity = connect_queue_capacity == 0 ? 4 : connect_queue_capacity * 2;
        WifiApRecord *new_queue = (WifiApRecord*)realloc(
            connect_queue, 
            new_capacity * sizeof(WifiApRecord)
        );
        if (!new_queue) {
            ESP_LOGE(TAG, "Failed to expand connect queue");
            return;
        }
        connect_queue = new_queue;
        connect_queue_capacity = new_capacity;
    }
    
    connect_queue[connect_queue_size] = *record;
    connect_queue_size++;
}

// Helper function to check if queue is empty
static bool connect_queue_is_empty(void) {
    return connect_queue_size == 0;
}

// Helper function to get front item and remove it
static WifiApRecord connect_queue_pop_front(void) {
    WifiApRecord record = connect_queue[0];
    
    // Shift all items to the left
    for (int i = 0; i < connect_queue_size - 1; i++) {
        connect_queue[i] = connect_queue[i + 1];
    }
    connect_queue_size--;
    
    return record;
}

// Helper function to clear queue
static void connect_queue_clear(void) {
    if (connect_queue) {
        free(connect_queue);
        connect_queue = NULL;
    }
    connect_queue_size = 0;
    connect_queue_capacity = 0;
}