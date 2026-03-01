#include <string.h>
#include "ssid_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define NVS_WIFI_NAMESPACE      "wifi"
#define MAX_WIFI_SSID_COUNT     10

static const char *TAG = "ssid_manager";

// Static variables
static wifi_credentials_t *ssid_list = NULL;
static int ssid_count = 0;
static int ssid_capacity = 0;

int ssid_manager_get_count(void) {
    return ssid_count;
}

inline const wifi_credentials_t* ssid_manager_get_ssid_list(void) {
    return ssid_list;
}

void ssid_manager_init(void) {
    ssid_list = NULL;
    ssid_count = 0;
    ssid_capacity = 0;
    
    ssid_manager_load_from_nvs();
}

void ssid_manager_deinit(void) {
    if (ssid_list) {
        free(ssid_list);
        ssid_list = NULL;
    }
    ssid_count = 0;
    ssid_capacity = 0;
}

void ssid_manager_clear(void) {
    if (ssid_list) {
        free(ssid_list);
        ssid_list = NULL;
    }
    ssid_count = 0;
    ssid_capacity = 0;
    
    ssid_manager_save_to_nvs();
}

void ssid_manager_load_from_nvs(void) {
    // Clear existing list
    if (ssid_list) {
        free(ssid_list);
        ssid_list = NULL;
    }
    ssid_count = 0;
    ssid_capacity = 0;
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace %s doesn't exist", NVS_WIFI_NAMESPACE);
        return;
    }
    
    // Allocate initial array
    ssid_capacity = MAX_WIFI_SSID_COUNT;
    ssid_list = (wifi_credentials_t*)malloc(sizeof(wifi_credentials_t) * ssid_capacity);
    if (!ssid_list) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        nvs_close(nvs_handle);
        return;
    }
    
    // Load credentials
    for (int i = 0; i < MAX_WIFI_SSID_COUNT; i++) {
        char ssid_key[16];
        char password_key[16];
        
        if (i == 0) {
            strcpy(ssid_key, "ssid");
            strcpy(password_key, "password");
        } else {
            snprintf(ssid_key, sizeof(ssid_key), "ssid%d", i);
            snprintf(password_key, sizeof(password_key), "password%d", i);
        }
        
        char ssid[33];
        char password[65];
        size_t length = sizeof(ssid);
        
        if (nvs_get_str(nvs_handle, ssid_key, ssid, &length) != ESP_OK) {
            continue;
        }
        
        length = sizeof(password);
        if (nvs_get_str(nvs_handle, password_key, password, &length) != ESP_OK) {
            continue;
        }
        
        // Add to list
        strncpy(ssid_list[ssid_count].ssid, ssid, MAX_SSID_LENGTH);
        ssid_list[ssid_count].ssid[MAX_SSID_LENGTH] = '\0';
        strncpy(ssid_list[ssid_count].password, password, MAX_PASSWORD_LENGTH);
        ssid_list[ssid_count].password[MAX_PASSWORD_LENGTH] = '\0';
        ssid_count++;
    }
    
    nvs_close(nvs_handle);
}

void ssid_manager_save_to_nvs(void) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle));
    
    for (int i = 0; i < MAX_WIFI_SSID_COUNT; i++) {
        char ssid_key[16];
        char password_key[16];
        
        if (i == 0) {
            strcpy(ssid_key, "ssid");
            strcpy(password_key, "password");
        } else {
            snprintf(ssid_key, sizeof(ssid_key), "ssid%d", i);
            snprintf(password_key, sizeof(password_key), "password%d", i);
        }
        
        if (i < ssid_count) {
            nvs_set_str(nvs_handle, ssid_key, ssid_list[i].ssid);
            nvs_set_str(nvs_handle, password_key, ssid_list[i].password);
        } else {
            nvs_erase_key(nvs_handle, ssid_key);
            nvs_erase_key(nvs_handle, password_key);
        }
    }
    
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

void ssid_manager_add_ssid(const char *ssid, const char *password) {
    if (!ssid || !password) return;
    
    // Check if SSID already exists
    for (int i = 0; i < ssid_count; i++) {
        ESP_LOGI(TAG, "compare [%s:%d] [%s:%d]", 
                 ssid_list[i].ssid, strlen(ssid_list[i].ssid),
                 ssid, strlen(ssid));
        
        if (strcmp(ssid_list[i].ssid, ssid) == 0) {
            ESP_LOGW(TAG, "SSID %s already exists, overwrite it", ssid);
            strncpy(ssid_list[i].password, password, MAX_PASSWORD_LENGTH);
            ssid_list[i].password[MAX_PASSWORD_LENGTH] = '\0';
            ssid_manager_save_to_nvs();
            return;
        }
    }
    
    // Check if list is full
    if (ssid_count >= MAX_WIFI_SSID_COUNT) {
        ESP_LOGW(TAG, "SSID list is full, pop one");
        ssid_count--;
    }
    
    // Ensure we have capacity
    if (ssid_count >= ssid_capacity) {
        ssid_capacity = MAX_WIFI_SSID_COUNT;
        wifi_credentials_t *new_list = (wifi_credentials_t*)realloc(
            ssid_list, 
            sizeof(wifi_credentials_t) * ssid_capacity
        );
        if (!new_list) {
            ESP_LOGE(TAG, "Failed to reallocate memory");
            return;
        }
        ssid_list = new_list;
    }
    
    // Shift all items to the right
    for (int i = ssid_count; i > 0; i--) {
        ssid_list[i] = ssid_list[i - 1];
    }
    
    // Add new item at the beginning
    strncpy(ssid_list[0].ssid, ssid, MAX_SSID_LENGTH);
    ssid_list[0].ssid[MAX_SSID_LENGTH] = '\0';
    strncpy(ssid_list[0].password, password, MAX_PASSWORD_LENGTH);
    ssid_list[0].password[MAX_PASSWORD_LENGTH] = '\0';
    ssid_count++;
    
    ssid_manager_save_to_nvs();
}

void ssid_manager_remove_ssid(int index) {
    if (index < 0 || index >= ssid_count) {
        ESP_LOGW(TAG, "Invalid index %d", index);
        return;
    }
    
    // Shift items to the left
    for (int i = index; i < ssid_count - 1; i++) {
        ssid_list[i] = ssid_list[i + 1];
    }
    ssid_count--;
    
    ssid_manager_save_to_nvs();
}

void ssid_manager_set_default_ssid(int index) {
    if (index < 0 || index >= ssid_count) {
        ESP_LOGW(TAG, "Invalid index %d", index);
        return;
    }
    
    // Save the item to move
    wifi_credentials_t temp = ssid_list[index];
    
    // Shift items to the right
    for (int i = index; i > 0; i--) {
        ssid_list[i] = ssid_list[i - 1];
    }
    
    // Place item at the beginning
    ssid_list[0] = temp;
    
    ssid_manager_save_to_nvs();
}

wifi_credentials_t* ssid_manager_get_credentials(int index) {
    if (index < 0 || index >= ssid_count) return NULL;
    return &ssid_list[index];
}
