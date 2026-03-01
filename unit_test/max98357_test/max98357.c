#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "audio_data.h"  // Raw audio data for "Xin chào"

static const char *TAG = "I2S_AUDIO";

// Cấu hình chân I2S cho MAX98357
#define I2S_BCLK_PIN    GPIO_NUM_26  // Bit Clock (BCLK)
#define I2S_LRCK_PIN    GPIO_NUM_25  // Left/Right Clock (LRC/WS)
#define I2S_DOUT_PIN    GPIO_NUM_22  // Data Out (DIN)

// Audio configuration (must match audio_data.h)
#define SAMPLE_RATE     AUDIO_SAMPLE_RATE      // Use sample rate from audio_data.h
#define BITS_PER_SAMPLE 16                     // Bit depth
#define CHANNELS        AUDIO_CHANNELS         // Use channels from audio_data.h

// Configuration for sine wave test (optional)
#define TEST_FREQ       440          // Test frequency (440Hz = note A)
#define TEST_DURATION_SEC 3          // Test duration (seconds)
#define AMPLITUDE       10000        // Test amplitude (0-32767)

static i2s_chan_handle_t tx_handle = NULL;

/**
 * @brief Khởi tạo I2S cho MAX98357
 */
esp_err_t i2s_init(void)
{
    esp_err_t ret = ESP_OK;

    // Cấu hình I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Tự động xóa DMA buffer khi underflow
    
    ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Cấu hình I2S standard mode (cho MAX98357)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(BITS_PER_SAMPLE, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // MAX98357 không cần MCLK
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

    // Enable I2S channel
    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized successfully - BCLK:%d, LRCK:%d, DOUT:%d", 
             I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);
    
    return ESP_OK;
}

/**
 * @brief Play raw audio data - "Xin chào" greeting
 */
void play_xin_chao_audio(void)
{
    size_t bytes_written;
    
    ESP_LOGI(TAG, "Playing 'Xin chào' greeting...");
    ESP_LOGI(TAG, "Duration: %.2f seconds, Samples: %d", 
             AUDIO_DURATION_SEC, AUDIO_DATA_SIZE);
    
    // Play the audio data
    esp_err_t ret = i2s_channel_write(tx_handle, 
                                      audio_xin_chao, 
                                      AUDIO_DATA_SIZE * sizeof(int16_t), 
                                      &bytes_written, 
                                      portMAX_DELAY);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write audio data: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Audio playback completed! Bytes written: %d", bytes_written);
    
    // Wait for audio to finish playing
    vTaskDelay(pdMS_TO_TICKS((int)(AUDIO_DURATION_SEC * 1000) + 500));
}

/**
 * @brief Generate sine wave test (simple test tone)
 * @param buffer Buffer to store audio data
 * @param samples Number of samples to generate
 * @param frequency Audio frequency (Hz)
 */
void generate_sine_wave(int16_t *buffer, size_t samples, float frequency)
{
    float phase_step = 2.0f * M_PI * frequency / SAMPLE_RATE;
    static float phase = 0.0f;

    for (size_t i = 0; i < samples; i += CHANNELS) {
        int16_t sample = (int16_t)(AMPLITUDE * sinf(phase));
        
        // Stereo: cả 2 channel đều phát cùng tín hiệu
        buffer[i] = sample;      // Left channel
        buffer[i + 1] = sample;  // Right channel
        
        phase += phase_step;
        if (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
}

/**
 * @brief Audio playback task
 */
void audio_playback_task(void *arg)
{
    ESP_LOGI(TAG, "=== Audio Playback Task Started ===");
    
    // Play "Xin chào" greeting
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "--- Playing 'Xin chào' audio ---");
    play_xin_chao_audio();
    
    // Wait 1 second between samples
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Then play test tone
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "--- Playing Test Tone: %dHz for %d seconds ---", 
             TEST_FREQ, TEST_DURATION_SEC);
    
    const size_t buffer_size = 1024;  // Number of samples in buffer
    int16_t *audio_buffer = (int16_t *)malloc(buffer_size * sizeof(int16_t));
    
    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio buffer");
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_written;
    size_t total_samples = SAMPLE_RATE * CHANNELS * TEST_DURATION_SEC;
    size_t samples_sent = 0;

    while (samples_sent < total_samples) {
        // Generate audio data
        generate_sine_wave(audio_buffer, buffer_size, TEST_FREQ);

        // Send data via I2S
        esp_err_t ret = i2s_channel_write(tx_handle, audio_buffer, 
                                          buffer_size * sizeof(int16_t), 
                                          &bytes_written, 
                                          portMAX_DELAY);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(ret));
            break;
        }

        samples_sent += buffer_size;
        
        // Log progress every second
        if (samples_sent % (SAMPLE_RATE * CHANNELS) == 0) {
            ESP_LOGI(TAG, "Test tone: %d/%d seconds", 
                     samples_sent / (SAMPLE_RATE * CHANNELS), TEST_DURATION_SEC);
        }
    }

    ESP_LOGI(TAG, "Test tone completed!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== All Audio Playback Completed ===");
    
    free(audio_buffer);
    vTaskDelete(NULL);
}

void audio_test(void)
{
    ESP_LOGI(TAG, "=== I2S Audio Playback with MAX98357 ===");
    ESP_LOGI(TAG, "Configuration: %dHz, %d-bit, %s", 
             SAMPLE_RATE, BITS_PER_SAMPLE, 
             CHANNELS == 2 ? "Stereo" : "Mono");

    // Initialize I2S
    if (i2s_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed, program stopped!");
        return;
    }

    // Create audio playback task
    xTaskCreate(audio_playback_task, "audio_playback", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "System ready!");
}
