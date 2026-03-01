#ifndef _WIFI_STATION_H_
#define _WIFI_STATION_H_

#include "esp_wifi_types_generic.h"

#ifndef MAX_SSID_LENGTH
#define MAX_SSID_LENGTH         32
#endif

#ifndef MAX_PASSWORD_LENGTH
#define MAX_PASSWORD_LENGTH     64
#endif

typedef struct {
    char ssid[MAX_SSID_LENGTH];
    char password[MAX_PASSWORD_LENGTH];
    int channel;
    wifi_auth_mode_t authmode;
    uint8_t bssid[6];
} WifiApRecord;

void wifi_sta_start(void);

#endif /* _WIFI_STATION_H_ */
