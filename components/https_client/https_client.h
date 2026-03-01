#ifndef __HTTPS_CLIENT__
#define __HTTPS_CLIENT__

#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_tls_errors.h"

#define MAX_HTTP_HEADER_SIZE       1024

extern void https_request_task(void *pvparameters);
extern char* https_get_request(esp_tls_cfg_t cfg, const char *WEB_SERVER_URL, const char *REQUEST);

#endif /* __HTTPS_CLIENT__ */