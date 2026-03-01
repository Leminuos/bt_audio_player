#include "sdcard.h"

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

static const char *TAG = "sdcard";

/*---------------------------------------------------------------------------
 * Internal state
 *--------------------------------------------------------------------------*/

static bool             s_mounted = false;
static sdmmc_card_t    *s_card    = NULL;

esp_err_t sdcard_init(void)
{
    esp_err_t ret = ESP_OK;

    /*--- 1. Mount configuration ---*/
    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = SDCARD_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
    };

    /*--- 2. SPI bus configuration ---*/
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SDCARD_PIN_MOSI,
        .miso_io_num = SDCARD_PIN_MISO,
        .sclk_io_num = SDCARD_PIN_CLK, 
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        /* Bus might already be initialized (shared with touch, etc.) */
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI bus already initialized, reusing");
            ret = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    /*--- 3. SD SPI device configuration ---*/
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = SDCARD_PIN_CS;
    dev.host_id = SPI3_HOST;

    /*--- 4. Mount filesystem ---*/
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot          = SPI3_HOST;
    host.max_freq_khz  = SDCARD_SPI_FREQ_KHZ;

    ret = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &dev, &mcfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Make sure the SD card is formatted as FAT32");
        }
        return ret;
    }

    s_mounted = true;

    /* Print card info */
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);

    return ESP_OK;
}

esp_err_t sdcard_deinit(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_bus_free(SPI3_HOST);

    s_card    = NULL;
    s_mounted = false;
    ESP_LOGI(TAG, "SD card unmounted");
    return ESP_OK;
}
