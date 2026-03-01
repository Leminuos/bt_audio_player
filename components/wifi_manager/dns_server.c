#include <stdatomic.h>
#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "wifi_manager";
static int s_fd = -1;
static int s_port = 53;
static esp_ip4_addr_t s_gateway;
static atomic_bool s_dns_running;

void dns_server_run(void* params);

void dns_server_start(esp_ip4_addr_t gateway) {

    if (atomic_load(&s_dns_running)) return;

    ESP_LOGI(TAG, "Starting DNS server");

    s_gateway = gateway;

    s_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(s_port);

    if (bind(s_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "failed to bind port %d", s_port);
        close(s_fd);
        s_fd = -1;
        return;
    }

    atomic_store(&s_dns_running, true);

    xTaskCreate(dns_server_run, "DnsServerTask", 4096, NULL, 5, NULL);
}

void dns_server_stop() {

    if (!atomic_load(&s_dns_running)) return;

    ESP_LOGI(TAG, "Stopping DNS server");
    atomic_store(&s_dns_running, false);

    // Close socket to unblock recvfrom
    if (s_fd >= 0) {
        shutdown(s_fd, SHUT_RDWR);
        close(s_fd);
        s_fd = -1;
    }
}

void dns_server_run(void* params) {
    int len = 0;
    char buffer[512] = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = 0;

    (void) params;

    while (atomic_load(&s_dns_running)) {
        client_addr_len = sizeof(client_addr);
        len = recvfrom(s_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);

        if (len < 0) {
            if (!atomic_load(&s_dns_running)) break;
            ESP_LOGE(TAG, "recvfrom failed, errno=%d", errno);
            continue;
        }

        if (!atomic_load(&s_dns_running)) break;

        // Simple DNS response: point all queries to 192.168.4.1
        buffer[2] |= 0x80;  // Set response flag
        buffer[3] |= 0x80;  // Set Recursion Available
        buffer[7] = 1;      // Set answer count to 1

        // Add answer section
        memcpy(&buffer[len], "\xc0\x0c", 2);  // Name pointer
        len += 2;
        memcpy(&buffer[len], "\x00\x01\x00\x01\x00\x00\x00\x1c\x00\x04", 10);  // Type, class, TTL, data length
        len += 10;
        memcpy(&buffer[len], &s_gateway.addr, 4);  // 192.168.4.1
        len += 4;
        ESP_LOGI(TAG, "Sending DNS response to %s", inet_ntoa(s_gateway.addr));

        sendto(s_fd, buffer, len, 0, (struct sockaddr *)&client_addr, client_addr_len);
    }

    vTaskDelete(NULL);
    ESP_LOGI(TAG, "DNS server task exiting");
}
