#ifndef _DNS_SERVER_H_
#define _DNS_SERVER_H_

#include "esp_netif_ip_addr.h"

void dns_server_stop(void);
void dns_server_start(esp_ip4_addr_t gateway);

#endif /* _DNS_SERVER_H_ */
