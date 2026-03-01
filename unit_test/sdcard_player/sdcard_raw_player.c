#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>  // Thêm để liệt kê thư mục
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"

#define MOUNT_POINT     "/sdcard"

static const char *TAG = "I2S_AUDIO";

// I2S pins for MAX98357
#define I2S_BCLK_PIN    GPIO_NUM_26
#define I2S_LRCK_PIN    GPIO_NUM_25
#define I2S_DOUT_PIN    GPIO_NUM_22

// SD Card pins (SPI mode)
#define SD_MISO_PIN     GPIO_NUM_19
#define SD_MOSI_PIN     GPIO_NUM_23
#define SD_CLK_PIN      GPIO_NUM_18
#define SD_CS_PIN       GPIO_NUM_5

// Audio configuration (must match your RAW file)
#define SAMPLE_RATE     44100   // Change to match your audio file
#define BITS_PER_SAMPLE 16
#define CHANNELS        2       // Stereo

// Buffer size for reading and playing
#define AUDIO_BUFFER_SIZE  (8 * 1024)  // Increased to 8KB for smoother playback

// Volume control (0.0 to 1.0)
#define DEFAULT_VOLUME  0.7     // Reduce to 70% to prevent clipping

static i2s_chan_handle_t tx_handle = NULL;
static sdmmc_card_t *card = NULL;

/**
 * @brief Initialize SD card
 */
esp_err_t sd_card_init(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing SD card (SPI mode)");
    
    // SD card mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // SPI bus configuration
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // SD card slot configuration
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = host.slot;
    
    // Mount filesystem
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Is SD card inserted?");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    // Print card info
    sdmmc_card_print_info(stdout, card);
    
    ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_POINT);
    
    return ESP_OK;
}

void list_files_only(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return;
    }

    struct dirent *entry;
    struct stat entry_stat;
    char full_path[512];

    ESP_LOGI(TAG, "Files in %s:", path);
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (stat(full_path, &entry_stat) == 0) {
            if (!S_ISDIR(entry_stat.st_mode)) {  // Chỉ liệt kê file
                ESP_LOGI(TAG, "  %s (%ld bytes)", entry->d_name, entry_stat.st_size);
            }
        }
    }

    closedir(dir);
}

/**
 * @brief Apply volume control and prevent clipping
 */
void apply_volume_and_limit(int16_t *buffer, size_t samples, float volume)
{
    for (size_t i = 0; i < samples; i++) {
        // Apply volume
        int32_t sample = (int32_t)(buffer[i] * volume);
        
        // Soft clipping to prevent distortion
        if (sample > 32767) {
            sample = 32767;
        } else if (sample < -32768) {
            sample = -32768;
        }
        
        buffer[i] = (int16_t)sample;
    }
}

/**
 * @brief Initialize I2S
 */
esp_err_t i2s_init(void)
{
    esp_err_t ret = ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 8;      // Increase DMA descriptors
    chan_cfg.dma_frame_num = 512;   // Increase DMA frame size
    
    ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(BITS_PER_SAMPLE, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_LRCK_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S standard mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized: %d Hz, %d-bit, %s", 
             SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS == 2 ? "Stereo" : "Mono");
    
    return ESP_OK;
}

/**
 * @brief Play RAW PCM audio file from SD card
 */
void play_raw_audio_file(const char *filename)
{
    ESP_LOGI(TAG, "Opening file: %s", filename);
    
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename);
        ESP_LOGE(TAG, "Make sure the file exists on SD card!");
        return;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    float duration = (float)file_size / (SAMPLE_RATE * CHANNELS * 2);
    
    ESP_LOGI(TAG, "File info:");
    ESP_LOGI(TAG, "  Size: %ld bytes (%.1f KB)", file_size, file_size / 1024.0f);
    ESP_LOGI(TAG, "  Duration: %.2f seconds", duration);
    ESP_LOGI(TAG, "  Format: %d Hz, %d-bit, %s", 
             SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS == 2 ? "Stereo" : "Mono");
    
    // Allocate buffer
    int16_t *audio_buffer = (int16_t *)malloc(AUDIO_BUFFER_SIZE);
    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        fclose(fp);
        return;
    }
    
    size_t total_bytes_read = 0;
    size_t total_bytes_written = 0;
    uint32_t start_time = xTaskGetTickCount();
    
    ESP_LOGI(TAG, "Starting playback...");
    
    while (1) {
        // Read from file
        size_t bytes_read = fread(audio_buffer, 1, AUDIO_BUFFER_SIZE, fp);
        
        if (bytes_read == 0) {
            // End of file
            break;
        }
        
        total_bytes_read += bytes_read;
        
        // Apply volume control and clipping protection
        size_t samples = bytes_read / sizeof(int16_t);
        apply_volume_and_limit(audio_buffer, samples, DEFAULT_VOLUME);
        
        // Write to I2S
        size_t bytes_written;
        esp_err_t ret = i2s_channel_write(tx_handle, audio_buffer, bytes_read,
                                          &bytes_written, portMAX_DELAY);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(ret));
            break;
        }
        
        total_bytes_written += bytes_written;
        
        // Progress indicator every 100KB
        if (total_bytes_written % (100 * 1024) < AUDIO_BUFFER_SIZE) {
            float progress = (float)total_bytes_written / file_size * 100;
            float elapsed = (xTaskGetTickCount() - start_time) / 1000.0f;
            ESP_LOGI(TAG, "Playing... %.1f%% (%.1f KB / %.1f KB) - %.1fs", 
                     progress, total_bytes_written / 1024.0f, file_size / 1024.0f, elapsed);
        }
    }
    
    uint32_t elapsed_ms = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
    
    ESP_LOGI(TAG, "Playback completed!");
    ESP_LOGI(TAG, "  Total played: %.1f KB in %.2f seconds", 
             total_bytes_written / 1024.0f, elapsed_ms / 1000.0f);
    
    // Cleanup
    free(audio_buffer);
    fclose(fp);
}

/**
 * @brief Audio playback task
 */
void audio_playback_task(void *arg)
{
    ESP_LOGI(TAG, "=== Audio Playback Task Started ===");
    
    // Play RAW audio file from SD card
    // Note: File must be 16-bit PCM, 44100 Hz, Stereo format
    play_raw_audio_file("/sdcard/AM_THA~1.RAW");
    
    ESP_LOGI(TAG, "=== Task Completed ===");
    
    vTaskDelete(NULL);
}

void sdcard_raw_player(void)
{
    ESP_LOGI(TAG, "=== I2S Audio Player - SD Card RAW PCM ===");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "SD Card Wiring:");
    ESP_LOGI(TAG, "  MISO: GPIO%d", SD_MISO_PIN);
    ESP_LOGI(TAG, "  MOSI: GPIO%d", SD_MOSI_PIN);
    ESP_LOGI(TAG, "  CLK:  GPIO%d", SD_CLK_PIN);
    ESP_LOGI(TAG, "  CS:   GPIO%d", SD_CS_PIN);
    ESP_LOGI(TAG, "");
    
    // Initialize SD card
    if (sd_card_init() != ESP_OK) {
        ESP_LOGE(TAG, "SD card initialization failed!");
        ESP_LOGE(TAG, "Please check:");
        ESP_LOGE(TAG, "  1. SD card is inserted");
        ESP_LOGE(TAG, "  2. Wiring is correct");
        ESP_LOGE(TAG, "  3. SD card is formatted (FAT32)");
        return;
    }

    list_files_only(MOUNT_POINT);
    
    // Initialize I2S
    if (i2s_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed!");
        return;
    }
    
    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "");
    
    // Create playback task
    xTaskCreate(audio_playback_task, "audio_playback", 8192, NULL, 5, NULL);
}
