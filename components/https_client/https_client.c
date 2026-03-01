#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "https_client.h"
#include <string.h>

#define WEB_SERVER "www.howsmyssl.com"
#define WEB_PORT "443"
#define WEB_URL "https://www.howsmyssl.com/a/check"

static const char HOWSMYSSL_REQUEST[] = "GET " WEB_URL " HTTP/1.1\r\n"
                             "Host: "WEB_SERVER"\r\n"
                             "User-Agent: esp-idf/1.0 esp32\r\n"
                             "\r\n";

static const char* TAG = "HTTPS";

char* https_get_request(esp_tls_cfg_t cfg, const char *WEB_SERVER_URL, const char *REQUEST)
{
    int ret = 0;
    int written_bytes = 0;
    int response_len = 0;
    char* response = NULL;
    char buf[MAX_HTTP_HEADER_SIZE];
    esp_tls_t *tls = esp_tls_init();

    if (!tls)
    {
        ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
        return response;
    }

    if (esp_tls_conn_http_new_sync(WEB_SERVER_URL, &cfg, tls) != 1)
    {
        ESP_LOGE(TAG, "Connection failed...");
        esp_tls_conn_destroy(tls);
        return response;
    }

    ESP_LOGI(TAG, "Connection established...");

    do
    {
        ret = esp_tls_conn_write(tls,
                                 REQUEST + written_bytes,
                                 strlen(REQUEST) - written_bytes);
        if (ret >= 0)
        {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        }
        else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE)
        {
            ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
            esp_tls_conn_destroy(tls);
            return response;
        }
    
    } while (written_bytes < strlen(REQUEST));

    ESP_LOGI(TAG, "Reading HTTP response...");

    do {
        memset(buf, 0x00, sizeof(buf));
        ret = esp_tls_conn_read(tls, buf, sizeof(buf) - 1);
        if (ret <= 0) break;

        char *new_response = realloc(response, response_len + ret + 1);

        if (!new_response) {
            ESP_LOGE(TAG, "Memory allocation failed");
            break;
        }

        response = new_response;
        memcpy(response + response_len, buf, ret);
        response_len += ret;
        response[response_len] = '\0';

    } while (1);

    esp_tls_conn_destroy(tls);
    
    return response;
}

static void https_get_request_using_crt_bundle(void)
{
    char* response = NULL;

    ESP_LOGI(TAG, "https_request using crt bundle");
    
    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    response = https_get_request(cfg, WEB_URL, HOWSMYSSL_REQUEST);

    ESP_LOGI(TAG, "%s", response);

    free(response);
}

void https_request_task(void *pvparameters)
{
    ESP_LOGI(TAG, "Start https_request example");

    https_get_request_using_crt_bundle();

    ESP_LOGI(TAG, "Finish https_request example");
    vTaskDelete(NULL);
}