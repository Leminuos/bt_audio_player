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

/*============================================================================
 * UI Hook API — execute callbacks on the LVGL task
 *
 * All LVGL API calls MUST happen on the LVGL task. Use these functions to
 * schedule work from any other task. Callbacks execute in lvgl_port_task
 * context, so no mutex is needed.
 *============================================================================*/

typedef void (*display_ui_cb_t)(void *arg);

/**
 * @brief Post a callback to run on the LVGL task (async, fire-and-forget)
 * @return true if posted successfully
 */
bool display_run_on_ui(display_ui_cb_t cb, void *arg);

/**
 * @brief Post a callback and block until it completes on the LVGL task
 * @note  Must NOT be called from the LVGL task itself (deadlock).
 * @return true if executed successfully
 */
bool display_run_on_ui_sync(display_ui_cb_t cb, void *arg);

/*============================================================================
 * Backlight
 *============================================================================*/

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
