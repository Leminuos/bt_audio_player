#pragma once

#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Types
 *============================================================================*/

/**
 * @brief Display orientation.
 *
 *   MADCTL mapping:
 *     PORTRAIT_0    : MY=0 MX=1 MV=0  → 240×320, connector bottom
 *     LANDSCAPE_0   : MY=0 MX=0 MV=1  → 320×240, connector right
 *     PORTRAIT_180  : MY=1 MX=0 MV=0  → 240×320, connector top
 *     LANDSCAPE_180 : MY=1 MX=1 MV=1  → 320×240, connector left
 */
typedef enum {
    ILI9341_PORTRAIT_0 = 0,
    ILI9341_LANDSCAPE_0,
    ILI9341_PORTRAIT_180,
    ILI9341_LANDSCAPE_180,
} ili9341_rotation_t;

/**
 * @brief Pixel color format — COLMOD register (0x3A).
 */
typedef enum {
    ILI9341_PIXEL_FORMAT_RGB565 = 0x55,   /**< 16-bit, 2 bytes/px (default) */
    ILI9341_PIXEL_FORMAT_RGB666 = 0x66,   /**< 18-bit, 3 bytes/px           */
} ili9341_pixel_format_t;

typedef struct {
    /* SPI pins */
    spi_host_device_t spi_host;         /**< SPI host (e.g. SPI2_HOST)       */
    int clk_gpio;                       /**< SPI clock pin                   */
    int mosi_gpio;                      /**< SPI MOSI pin                    */
    int miso_gpio;                      /**< SPI MISO pin (set -1 if unused) */
    int cs_gpio;                        /**< Chip select pin                 */
    int dc_gpio;                        /**< Data/Command pin                */
    int rst_gpio;                       /**< Reset pin (set -1 if unused)    */

    /* SPI timing */
    int clk_speed_hz;                   /**< SPI clock frequency in Hz       */
    size_t max_transfer_sz;             /**< Max DMA transfer size in bytes  */
} ili9341_config_t;

typedef void (*ili9341_flush_done_cb_t)(void *user_ctx);

/*============================================================================
 * Public API
 *============================================================================*/

/**
 * @brief Initialize SPI bus + device and run the ILI9341 init sequence.
 *
 * Default after init: RGB565, BGR mode, PORTRAIT_0.
 *
 * @param[in] cfg  Pin and SPI configuration
 * @return ESP_OK on success
 */
esp_err_t ili9341_init(const ili9341_config_t *cfg);

/**
 * @brief Release SPI device and free resources.
 */
void ili9341_deinit(void);

/**
 * @brief Set orientation.
 */
void ili9341_set_rotation(ili9341_rotation_t rot);

/**
 * @brief Set window + write pixel data (async if flush callback registered).
 *
 * Data length is calculated automatically from the window size (RGB565).
 * The caller must ensure `data` contains at least
 * (x2 - x1 + 1) * (y2 - y1 + 1) * 2 bytes.
 */
void ili9341_draw(uint16_t x1, uint16_t y1,
                  uint16_t x2, uint16_t y2,
                  const void *data);

/**
 * @brief Block until the last async draw completes.
 */
void ili9341_draw_wait(void);

/**
 * @brief Register a callback invoked when async DMA transfer finishes.
 *        Pass NULL to disable async mode (draw becomes blocking).
 */
void ili9341_register_flush_done_cb(ili9341_flush_done_cb_t cb,
                                    void *user_ctx);

/**
 * @brief Set pixel color format (RGB565 or RGB666).
 */
void ili9341_set_pixel_format(ili9341_pixel_format_t fmt);

/**
 * @brief Set color order: true = BGR (default), false = RGB.
 */
void ili9341_set_bgr(bool bgr);

/**
 * @brief Get current display width in pixels (depends on rotation).
 */
uint16_t ili9341_get_width(void);

/**
 * @brief Get current display height in pixels (depends on rotation).
 */
uint16_t ili9341_get_height(void);

#ifdef __cplusplus
}
#endif
