idf_component_register(
    SRCS "sht30.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES "esp_driver_i2c"
)