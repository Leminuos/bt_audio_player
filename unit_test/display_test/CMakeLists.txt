idf_component_register(SRCS "ili9341.c" "display_test.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd driver esp_lcd_ili9341)
