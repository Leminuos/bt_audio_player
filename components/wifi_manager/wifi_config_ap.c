#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_config_ap.h"
#include "ssid_manager.h"
#include "dns_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "lwip/ip_addr.h"
#include "esp_http_server.h"

#define WIFI_EVENT_CONNECTED_BIT        BIT0
#define WIFI_EVENT_FAIL_BIT             BIT1

static const char *TAG = "wifi_manager";

esp_netif_t* s_ap_netif;
static char s_ssid_prefix[32];
static uint16_t s_ap_records_num;
static wifi_ap_record_t *s_ap_records;
static httpd_handle_t s_server_handle;
static EventGroupHandle_t s_ap_event_group;
static esp_timer_handle_t s_scan_timer_handle;
static on_exit_requested_cb s_on_exit_requested_cb;

extern const char index_html_start[] asm("_binary_wifi_configuration_html_start");
extern const char done_html_start[] asm("_binary_wifi_configuration_done_html_start");

static void wifi_ap_start_webserver(void);
static void wifi_ap_scan_timer_callback(void* arg);
static esp_err_t wifi_ap_get_ssid(char* out_ssid, size_t ssid_size);
static void wifi_ap_save_ssid(const char* ssid, const char* password);
static bool wifi_ap_connect_to_ssid(const char* ssid, const char* password);
static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

void wifi_ap_set_ssid_prefix(const char* ssid_prefix)
{
    strncpy(s_ssid_prefix, ssid_prefix, sizeof(s_ssid_prefix) - 1);
    s_ssid_prefix[sizeof(s_ssid_prefix) - 1] = '\0';
}

void wifi_ap_register_exit_requested_cb(on_exit_requested_cb cb)
{
    s_on_exit_requested_cb = cb;
}

esp_err_t wifi_ap_start(void)
{
    s_ap_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register( WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                wifi_ap_event_handler,
                                                NULL ));

    ESP_ERROR_CHECK(esp_event_handler_register( IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                wifi_ap_event_handler,
                                                NULL ));

    // Create the default WiFi AP interface
    s_ap_netif = esp_netif_create_default_wifi_ap();

    // Set the router IP address to 192.168.4.1
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip_info);
    esp_netif_dhcps_start(s_ap_netif);

    dns_server_start(ip_info.gw);

    // Set the WiFi configuration
    char ssid[32];
    wifi_config_t wifi_config = {};

    wifi_ap_get_ssid(ssid, sizeof(ssid));
    strcpy((char *)wifi_config.ap.ssid, ssid);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    // Start the WiFi Access Point
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started with SSID %s", ssid);

    wifi_ap_start_webserver();

    // Start scan immediately
    esp_wifi_scan_start(NULL, false);

    // Setup periodic WiFi scan timer
    esp_timer_create_args_t timer_args = {
        .callback = wifi_ap_scan_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_scan_timer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_scan_timer_handle));

    return ESP_OK;
}

void wifi_ap_stop(void) {
    if (s_scan_timer_handle) {
        esp_timer_stop(s_scan_timer_handle);
        esp_timer_delete(s_scan_timer_handle);
        s_scan_timer_handle = NULL;
    }

    if (s_server_handle) {
        httpd_stop(s_server_handle);
        s_server_handle = NULL;
    }

    dns_server_stop();
    esp_wifi_stop();
    
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }

    ESP_LOGI(TAG, "Wifi configuration AP stopped");
}

static void wifi_ap_scan_timer_callback(void* arg) {
    esp_wifi_scan_start(NULL, false);
}

static esp_err_t wifi_ap_get_ssid(char* out_ssid, size_t ssid_size) {
    uint8_t mac[6];

    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;

    snprintf(out_ssid, ssid_size, "%s-%02X%02X", s_ssid_prefix, mac[4], mac[5]);

    return ESP_OK;
}

static esp_err_t http_root_handler (httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, index_html_start, strlen(index_html_start));
    return ESP_OK;
}

static esp_err_t http_scan_handler(httpd_req_t *req) {
    // Check if 5G is supported
    bool support_5g = false;

#ifdef CONFIG_SOC_WIFI_SUPPORT_5G
    support_5g = true;
#endif

    // Send the scan results as JSON
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr_chunk(req, "{\"support_5g\":");
    httpd_resp_sendstr_chunk(req, support_5g ? "true" : "false");
    httpd_resp_sendstr_chunk(req, ",\"aps\":[");

    for (int i = 0; i < s_ap_records_num; i++) {
        ESP_LOGI( TAG, "SSID: %s, RSSI: %d, Authmode: %d",
                  (char *)s_ap_records[i].ssid,
                  s_ap_records[i].rssi,
                  s_ap_records[i].authmode );
        
        char buf[128];
        snprintf( buf, sizeof(buf), "{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":%d}",
                  (char *)s_ap_records[i].ssid,
                  s_ap_records[i].rssi,
                  s_ap_records[i].authmode );
        
        httpd_resp_sendstr_chunk(req, buf);

        if (i < s_ap_records_num - 1) httpd_resp_sendstr_chunk(req, ",");
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static esp_err_t http_form_submit_handler(httpd_req_t *req) {
    char *buf;
    size_t buf_len = req->content_len;
    if (buf_len > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    buf = (char *)malloc(buf_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate memory");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, buf_len);
    if (ret <= 0) {
        free(buf);

        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        else httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive request");

        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *password_item = cJSON_GetObjectItemCaseSensitive(json, "password");

    if (!cJSON_IsString(ssid_item) || (ssid_item->valuestring == NULL) || (strlen(ssid_item->valuestring) >= 33)) {
        cJSON_Delete(json);
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid SSID\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char* ssid_str = ssid_item->valuestring;
    char* password_str = NULL;
    if (cJSON_IsString(password_item) && (password_item->valuestring != NULL) && (strlen(password_item->valuestring) < 65)) {
        password_str = password_item->valuestring;
    }

    if (!wifi_ap_connect_to_ssid(ssid_str, password_str)) {
        cJSON_Delete(json);
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to connect to the Access Point\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    wifi_ap_save_ssid(ssid_str, password_str);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_done_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, done_html_start, strlen(done_html_start));
    return ESP_OK;
}

static void exit_config_task(void* args) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (s_on_exit_requested_cb) s_on_exit_requested_cb();
    vTaskDelete(NULL);
}

static esp_err_t http_exit_handler(httpd_req_t *req) {            
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    
    ESP_LOGI(TAG, "Exiting config mode...");

    xTaskCreate(exit_config_task, "exit_config_task", 4096, NULL, 5, NULL);
    
    return ESP_OK;
}

static void wifi_ap_start_webserver(void) {
    // Start the web server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.max_uri_handlers = 24;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;

    ESP_ERROR_CHECK(httpd_start(&s_server_handle, &config));

    // Register the index.html file
    httpd_uri_t index_html = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = http_root_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server_handle, &index_html));

    // Register the /scan URI
    httpd_uri_t scan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = http_scan_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server_handle, &scan));

    // Register the form submission
    httpd_uri_t form_submit = {
        .uri = "/submit",
        .method = HTTP_POST,
        .handler = http_form_submit_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server_handle, &form_submit));

    // Register the done.html page
    httpd_uri_t done_html = {
        .uri = "/done.html",
        .method = HTTP_GET,
        .handler = http_done_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server_handle, &done_html));

    // Register the exit endpoint - exits config mode without rebooting
    httpd_uri_t exit_config = {
        .uri = "/exit",
        .method = HTTP_POST,
        .handler = http_exit_handler,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server_handle, &exit_config));

    ESP_LOGI(TAG, "Web server started");
}

static bool wifi_ap_connect_to_ssid(const char* ssid, const char* password) {

    if (!ssid) {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return false;
    }
    
    if (strlen(ssid) > 32) {
        ESP_LOGE(TAG, "SSID too long");
        return false;
    }

    if (strlen(password) > 64) {
        ESP_LOGE(TAG, "Password too long");
        return false;
    }

    esp_wifi_scan_stop();
    xEventGroupClearBits(s_ap_event_group, WIFI_EVENT_CONNECTED_BIT | WIFI_EVENT_FAIL_BIT);

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strlcpy((char *)wifi_config.sta.ssid, ssid, 32);
    strlcpy((char *)wifi_config.sta.password, password, 64);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.failure_retry_cnt = 1;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %d", ret);
        return false;
    }

    ESP_LOGI(TAG, "Connecting to WiFi %s", ssid);

    // Wait for the connection to complete for 10 or 25 seconds
    EventBits_t bits = xEventGroupWaitBits(
        s_ap_event_group,
        WIFI_EVENT_CONNECTED_BIT | WIFI_EVENT_FAIL_BIT,
        pdTRUE,
        pdFALSE,
#ifdef CONFIG_SOC_WIFI_SUPPORT_5G
        pdMS_TO_TICKS(25000)
#else
        pdMS_TO_TICKS(10000)
#endif
    );

    if (bits & WIFI_EVENT_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi %s", ssid);
        esp_wifi_disconnect();
        return true;
    }

    ESP_LOGE(TAG, "Failed to connect to WiFi %s", ssid);
    return false;
}

static void wifi_ap_save_ssid(const char* ssid, const char* password) {
    ESP_LOGI(TAG, "Save SSID %s %d", ssid, strlen(ssid));
    ssid_manager_add_ssid(ssid, password);
}

static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT)
    {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
            {
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED:
            {
                break;
            }

            case WIFI_EVENT_STA_CONNECTED:
            {
                xEventGroupSetBits(s_ap_event_group, WIFI_EVENT_CONNECTED_BIT);
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED:
            {
                xEventGroupSetBits(s_ap_event_group, WIFI_EVENT_FAIL_BIT);
                break;
            }

            case WIFI_EVENT_SCAN_DONE:
            {;
                
                if (s_ap_records)
                {
                    free(s_ap_records);
                    s_ap_records = NULL;
                    s_ap_records_num = 0;
                }
                
                esp_wifi_scan_get_ap_num(&s_ap_records_num);
                s_ap_records = (wifi_ap_record_t *)malloc(s_ap_records_num * sizeof(wifi_ap_record_t));

                ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&s_ap_records_num, s_ap_records));

                esp_timer_start_once(s_scan_timer_handle, 10 * 1000000);
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
                xEventGroupSetBits(s_ap_event_group, WIFI_EVENT_CONNECTED_BIT);
                break;
            }
            
            default:
                break;
        }
    }
}
