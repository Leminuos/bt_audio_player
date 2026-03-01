#ifndef _SDCARD_
#define _SDCARD_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * Configuration defaults (CYD pinout)
 *--------------------------------------------------------------------------*/
#define SDCARD_PIN_MOSI     23
#define SDCARD_PIN_MISO     19
#define SDCARD_PIN_CLK      18
#define SDCARD_PIN_CS       5

#define SDCARD_MOUNT_POINT  "/sdcard"
#define SDCARD_MAX_FILES    5
#define SDCARD_SPI_FREQ_KHZ 20000

/*---------------------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------------------*/

/**
 * @brief Initialize and mount SD card.
 *
 * Configures SPI bus and mounts FAT filesystem.
 * Call once at startup.
 *
 * @return ESP_OK on success
 */
esp_err_t sdcard_init();

/**
 * @brief Unmount and deinitialize SD card.
 * @return ESP_OK on success
 */
esp_err_t sdcard_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* _SDCARD_ */
