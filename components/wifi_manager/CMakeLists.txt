idf_component_register(
    SRCS
        "wifi_manager.c"
        "ssid_manager.c"
        "wifi_station.c"
        "wifi_config_ap.c"
        "dns_server.c"
    
    INCLUDE_DIRS
        "include"

    EMBED_TXTFILES
        "assets/wifi_configuration.html"
        "assets/wifi_configuration_done.html"
    
    REQUIRES
        "esp_timer"
        "esp_http_server"
        "esp_wifi"
        "nvs_flash"
        "espressif__cjson"
)