idf_component_register(SRCS "lvgl_test.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd esp_lcd_ili9341 esp_lvgl_port lvgl driver)
