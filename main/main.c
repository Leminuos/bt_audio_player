#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "display.h"
#include "sdcard.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bt_audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main";

extern void ui_refresh_file_list(const char *dir_path);

static void on_bt_event(const bt_audio_event_t *evt)
{
    switch (evt->type) {
    case BT_AUDIO_EVT_STATE_CHANGED: {
        const bt_audio_evt_state_changed_t *s = evt->data;
        ESP_LOGI(TAG, "State: %d -> %d", s->old_state, s->new_state);

        switch (s->new_state) {
        case BT_AUDIO_STATE_CONNECTED: {
            bt_audio_device_info_t info;
            if (bt_audio_get_device_info(&info) == ESP_OK) {
                ESP_LOGI(TAG, "Connected to: %s (%s)", info.name, info.bda_str);
            }

            break;
        }

        case BT_AUDIO_STATE_DISCONNECTED:
            bt_audio_start_discovery(true);
            break;

        default:
            break;
        }
        break;
    }

    case BT_AUDIO_EVT_DATA_UPDATE: {
        // char time_elapsed[12] = {0};
        // bt_audio_playback_pos_t p = {0};
        // bt_audio_get_position(&p);
        // snprintf(time_elapsed, sizeof(time_elapsed), "%02ld:%02ld",
        //     ((p.position_ms / 1000) / 60),
        //     ((p.position_ms / 1000) % 60));
        // ESP_LOGI(TAG, "Time duration: %s", time_elapsed);
        // lv_label_set_text(ui_lblTimeElapsed, time_elapsed);
        // lv_slider_set_value(ui_sliderProgress, p.progress_pct, LV_ANIM_ON);
        break;
    }
    
    case BT_AUDIO_EVT_TRACK_FINISHED:
        lv_label_set_text(ui_lblBtnPlayPause, LV_SYMBOL_PLAY);
        break;

    default:
        break;
    }
}

void app_main(void)
{
    
    bt_audio_init("esp32_player");
    bt_audio_register_callback(on_bt_event);
    bt_audio_start_discovery(true);
    ESP_ERROR_CHECK(sdcard_init());
    ESP_ERROR_CHECK(display_init());

    if (display_port_lock(0)) {
        ui_init();
        ui_refresh_file_list("/sdcard");
        display_port_unlock();
    } else {
        ESP_LOGE("app", "Failed to lock LVGL for screen creation");
    }
}
