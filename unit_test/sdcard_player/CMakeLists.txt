idf_component_register(
    SRCS "sdcard_raw_player.c"
    INCLUDE_DIRS "."
    REQUIRES fatfs driver esp_driver_spi
)
