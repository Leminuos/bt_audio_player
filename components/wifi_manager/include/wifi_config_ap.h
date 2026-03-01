#ifndef _WIFI_CONFIG_AP_H_
#define _WIFI_CONFIG_AP_H_

#include "esp_err.h"

typedef void (*on_exit_requested_cb)(void);

void wifi_ap_set_ssid_prefix(const char* ssid_prefix);

void wifi_ap_stop(void);
esp_err_t wifi_ap_start(void);

void wifi_ap_register_exit_requested_cb(on_exit_requested_cb cb);

#endif /* _WIFI_CONFIG_AP_H_ */

