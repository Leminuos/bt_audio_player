#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

struct WifiManagerConfig {
    char ssid_prefix[255];
    char language[10];
    
    int sta_scan_min_interval_seconds;
    int sta_scan_max_interval_seconds;
};

esp_err_t wifi_manager_init(void);

#endif /* _WIFI_MANAGER_H_ */
