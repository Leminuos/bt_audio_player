#ifndef _DISPLAY_DRIVER_H_
#define _DISPLAY_DRIVER_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hardware.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Display Driver API
 *============================================================================*/

/**
 * @brief Initialize display and LVGL
 * @return ESP_OK on success
 */
esp_err_t display_init(void);

/**
 * @brief Deinitialize display
 */
void display_deinit(void);

/**
 * @brief Lock LVGL mutex
 */
bool display_port_lock(int timeout_ms);

/**
 * @brief Unlock LVGL mutex
 */
void display_port_unlock(void);

/**
 * @brief Set backlight percent
 * @param brightness_percent Brightness percent 0-100
 */
esp_err_t display_brightness_set(int brightness_percent);

esp_err_t display_backlight_off(void);

esp_err_t display_backlight_on(void);

/*============================================================================
 * Getters
 *============================================================================*/

 /**
 * @brief Get display width
 * @return Width in pixels
 */
static inline uint16_t display_get_width(void)
{
    return LCD_H_RES;
}

/**
 * @brief Get display height
 * @return Height in pixels
 */
static inline uint16_t display_get_height(void)
{
    return LCD_V_RES;
}

#ifdef __cplusplus
}
#endif

#endif /* _DISPLAY_DRIVER_H_ */
