#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_audio.h"
#include "sdcard.h"

static const char *TAG = "main";

static void demo_task(void *arg)
{
    /* Demo 1: Hiển thị position 10s */
    ESP_LOGI(TAG, "== Playing 10s...");
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        bt_audio_playback_pos_t pos;
        bt_audio_get_position(&pos);
        ESP_LOGI(TAG, "  %lu:%02lu / %lu:%02lu (%u%%)",
                 (unsigned long)(pos.position_ms / 60000),
                 (unsigned long)((pos.position_ms / 1000) % 60),
                 (unsigned long)(pos.duration_ms / 60000),
                 (unsigned long)((pos.duration_ms / 1000) % 60),
                 pos.progress_pct);
    }

    /* Demo 2: Pause / Resume */
    ESP_LOGI(TAG, "== Pause 3s");
    bt_audio_pause();
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "== Resume");
    bt_audio_resume();
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* Demo 3: Volume */
    ESP_LOGI(TAG, "== Volume 50%%");
    bt_audio_set_volume(50);
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "== Volume 100%%");
    bt_audio_set_volume(100);
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Demo 4: Seek đến 1:45 */
    ESP_LOGI(TAG, "== Seek to 1:45");
    bt_audio_seek_ms(105000);

    /* Demo 5: Title */
    const char *title = bt_audio_get_title();
    if (title[0]) {
        ESP_LOGI(TAG, "== Title: %s", title);
    }

    vTaskDelete(NULL);
}

/* ─── BT event handler ───────────────────────────────────────────────────── */

static void on_bt_event(const bt_audio_event_t *evt)
{
    switch (evt->type) {
    case BT_AUDIO_EVT_STATE_CHANGED: {
        const bt_audio_evt_state_changed_t *s = evt->data;
        ESP_LOGI(TAG, "State: %d -> %d", s->old_state, s->new_state);

        switch (s->new_state) {
        case BT_AUDIO_STATE_CONNECTED: {
            bt_audio_device_info_t info;
            if (bt_audio_get_device_info(&info) == ESP_OK)
                ESP_LOGI(TAG, "Connected to: %s (%s)", info.name, info.bda_str);
            
            bt_audio_play("/sdcard/am_tham_ben_em.raw");
            break;
        }
        case BT_AUDIO_STATE_PLAYING:
            xTaskCreate(demo_task, "demo", 4096, NULL, 3, NULL);
            break;
        case BT_AUDIO_STATE_DISCONNECTED:
            bt_audio_start_discovery(true);
            break;
        default:
            break;
        }
        break;
    }

    case BT_AUDIO_EVT_TRACK_FINISHED:
        ESP_LOGI(TAG, "Track finished!");
        /* bt_audio_play("/sdcard/next_song.wav"); */
        break;

    case BT_AUDIO_EVT_DATA_UPDATE: {
        break;
    }

    default:
        break;
    }
}

void app_main(void)
{
    sdcard_init();
    bt_audio_init("esp32_player");
    bt_audio_register_callback(on_bt_event);
    bt_audio_start_discovery(true);
}