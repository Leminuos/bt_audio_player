#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_tls_errors.h"
#include "https_client.h"
#include <string.h>

static char url[256];
static char request[512];
static const char* TAG = "firebase";

#define FIREBASE_PORT   443
#define FIREBASE_HOST   "esp32-test-61c02-default-rtdb.asia-southeast1.firebasedatabase.app"

char* firebase_get_data(const char *path)
{
    memset(url, 0, sizeof(url));
    snprintf(url, sizeof(url), "https://%s/%s.json", FIREBASE_HOST, path);
    
    memset(request, 0, sizeof(request));
    snprintf(request, sizeof(request),
             "GET %s.json HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: esp-idf/1.0 esp32\r\n"
             "\r\n"
            ,path, FIREBASE_HOST);

    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    return https_get_request(cfg, url, request);
}

char* firebase_post_data(const char *path, const char *json_data)
{
    memset(url, 0, sizeof(url));
    snprintf(url, sizeof(url), "https://%s/%s.json", FIREBASE_HOST, path);
    
    memset(request, 0, sizeof(request));
    snprintf(request, sizeof(request),
             "PUT %s.json HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "User-Agent: esp-idf/1.0 esp32\r\n"
             "Content-Length: %d\r\n"
             "\r\n"
             "%s",
             path, FIREBASE_HOST, strlen(json_data), json_data);

    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    return https_get_request(cfg, url, request);
}

void firebase_stream_task(void *pvParameters)
{
    int ret = 0;
    char buf[512];
    int written_bytes = 0;

    memset(url, 0, sizeof(url));
    snprintf(url, sizeof(url), "https://%s/.json", FIREBASE_HOST);
    
    memset(request, 0, sizeof(request));
    snprintf(request, sizeof(request),
             "GET /.json HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Accept: text/event-stream\r\n"
             "User-Agent: esp-idf\r\n"
             "\r\n",
             FIREBASE_HOST);

    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_tls_t *tls = esp_tls_init();

    if (!tls)
    {
        ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
        return;
    }

    if (esp_tls_conn_http_new_sync(url, &cfg, tls) != 1)
    {
        ESP_LOGE(TAG, "Connection failed...");
        esp_tls_conn_destroy(tls);
        return;
    }

    ESP_LOGI(TAG, "Connection established...");

    while (written_bytes < strlen(request))
    {
        ret = esp_tls_conn_write(tls, request + written_bytes, strlen(request) - written_bytes);
        
        if (ret >= 0)
        {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        }
        else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE)
        {
            ESP_LOGI(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
            esp_tls_conn_destroy(tls);
            return;
        }
    }

    while (1)
    {
        
        memset(buf, 0x00, sizeof(buf));
        ret = esp_tls_conn_read(tls, buf, sizeof(buf)-1);

        if (ret == 0)
        {
            ESP_LOGI(TAG, "Connection closed");
            vTaskDelete(NULL);
            break;
        }
        else if (ret > 0)
        {
            buf[ret] = 0;
            ESP_LOGI(TAG, "%s", buf);
        }
        else
        {
            if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE)
            {
                ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
                vTaskDelay(10 / portTICK_PERIOD_MS);
                continue;
            }
        }
    }
}