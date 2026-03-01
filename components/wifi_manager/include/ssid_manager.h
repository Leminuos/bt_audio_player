#ifndef _SSID_MANAGER_H_
#define _SSID_MANAGER_H_

#ifndef MAX_SSID_LENGTH
#define MAX_SSID_LENGTH         32
#endif

#ifndef MAX_PASSWORD_LENGTH
#define MAX_PASSWORD_LENGTH     64
#endif

typedef struct {
    char ssid[MAX_SSID_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
} wifi_credentials_t;

/**
 * Initialize the SSID manager and load credentials from NVS
 */
void ssid_manager_init(void);

/**
 * Cleanup and free resources
 */
void ssid_manager_deinit(void);

/**
 * Clear all SSIDs from manager and NVS
 */
void ssid_manager_clear(void);

/**
 * Load SSIDs from NVS
 */
void ssid_manager_load_from_nvs(void);

/**
 * Save SSIDs to NVS
 */
void ssid_manager_save_to_nvs(void);

/**
 * Add or update an SSID
 */
void ssid_manager_add_ssid(const char *ssid, const char *password);

/**
 * Remove an SSID by index
 */
void ssid_manager_remove_ssid(int index);

/**
 * Set default SSID (move to front)
 */
void ssid_manager_set_default_ssid(int index);

/**
 * Get SSID count
 */
int ssid_manager_get_count(void);

/**
 * Get credentials at index
 */
wifi_credentials_t* ssid_manager_get_credentials(int index);

/**
 * Get the entire SSID list
 * Returns pointer to internal array (read-only, do not modify)
 */
const wifi_credentials_t* ssid_manager_get_ssid_list(void);

#endif /* _SSID_MANAGER_H_ */
