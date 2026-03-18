#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "display.h"
#include "sdcard.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bt_audio.h"
#include "sys_monitor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char* TAG = "main";

#define UI_EVENT_BT_DISCOVERY_DONE      BIT0
#define UI_EVENT_BT_DEVICE_CONNECTED    BIT1  
#define UI_EVENT_BT_CLOCK_TICK          BIT2
#define UI_EVENT_BT_TRACK_FINISHED      BIT3
#define UI_EVENT_BT_DEVICE_DISCONNECTED BIT4
#define UI_EVENT_BT_VOLUME_CHANGE       BIT5
#define UI_EVENT_BT_ALL                 BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT5

static TaskHandle_t s_audio_task;
static EventGroupHandle_t s_audio_event_group;
static esp_timer_handle_t s_clock_timer_handle;

extern bool ui_is_loop;
extern bool ui_is_finish;
extern bool ui_is_playing;
extern void ui_bt_start_scan(void);
extern void ui_bt_stop_scan(void);
extern void ui_refresh_file_list(const char *dir_path);
extern esp_err_t ui_audio_next_track();

static void ui_audio_task(void* param)
{
    (void) param;
    EventBits_t bits = 0;
    static int clock_ticks = 0;

    for ( ; ; ) {
        bits = xEventGroupWaitBits(s_audio_event_group,
                                  UI_EVENT_BT_ALL,
                                  pdTRUE,
                                  pdFALSE,
                                  portMAX_DELAY);

        if (bits & UI_EVENT_BT_DISCOVERY_DONE) {
            if (display_port_lock(100)) {
                ui_bt_stop_scan();
                display_port_unlock();
            }
        }

        if (bits & UI_EVENT_BT_DEVICE_CONNECTED) {
            if (display_port_lock(100)) {
                lv_scr_load_anim(ui_explorer, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                ui_refresh_file_list("/sdcard");
                display_port_unlock();
            }
        }

        if (bits &  UI_EVENT_BT_CLOCK_TICK) {
            ++clock_ticks;

            // Print debug info every 10 seconds
            if (clock_ticks % 10 == 0) {
                sys_monitor_print_heap();
            }
        }

        if (bits & UI_EVENT_BT_TRACK_FINISHED) {
            if (ui_is_loop) bt_audio_seek(0);
            else {
                if (display_port_lock(100)) {
                    if (ui_audio_next_track() != ESP_OK) {
                        ui_is_finish  = true;
                        ui_is_playing = false;
                        lv_label_set_text(ui_lblBtnPlayPause, LV_SYMBOL_PLAY);
                    }

                    display_port_unlock();
                }
            }
        }

        if (bits & UI_EVENT_BT_DEVICE_DISCONNECTED) {
            if (display_port_lock(100)) {
                ui_bt_start_scan();
                lv_scr_load_anim(ui_bt_select, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
                display_port_unlock();
            }
        }

        if (bits & UI_EVENT_BT_VOLUME_CHANGE) {
            if (display_port_lock(100)) {
                uint8_t vol = bt_audio_get_volume();
                lv_slider_set_value(ui_sliderVolume, vol, LV_ANIM_OFF);
                display_port_unlock();
            }
        }
    }
}

static void on_bt_event(const bt_audio_event_t *evt)
{
    switch (evt->type) {
    case BT_AUDIO_EVT_STATE_CHANGED: {
        const bt_audio_evt_state_changed_t *s = evt->data;
        ESP_LOGI(TAG, "State: %d -> %d", s->old_state, s->new_state);

        switch (s->new_state) {
        case BT_AUDIO_STATE_DISCOVERY_DONE:
            xEventGroupSetBits(s_audio_event_group, UI_EVENT_BT_DISCOVERY_DONE);
            break;

        case BT_AUDIO_STATE_CONNECTED: {
            bt_audio_device_info_t info = {0};
            if (bt_audio_get_device_info(&info) == ESP_OK) {
                lv_label_set_text_fmt(ui_ExplorerLabel, "Connected to: %s", info.name);
                ESP_LOGI(TAG, "Connected to: %s (%s)", info.name, info.bda_str);
            }

            xEventGroupSetBits(s_audio_event_group, UI_EVENT_BT_DEVICE_CONNECTED);
            break;
        }

        case BT_AUDIO_STATE_PLAYING:
            break;

        case BT_AUDIO_STATE_PAUSED:
            break;

        case BT_AUDIO_STATE_DISCONNECTED:
            xEventGroupSetBits(s_audio_event_group, UI_EVENT_BT_DEVICE_DISCONNECTED);
            break;

        default:
            break;
        }
        break;
    }
    
    case BT_AUDIO_EVT_TRACK_FINISHED:
        xEventGroupSetBits(s_audio_event_group, UI_EVENT_BT_TRACK_FINISHED);
        break;

    case BT_AUDIO_EVT_VOLUME_CHANGE:
        xEventGroupSetBits(s_audio_event_group, UI_EVENT_BT_VOLUME_CHANGE);
        break;

    default:
        break;
    }
}

static void clock_timer_callback(void* arg)
{
    (void) arg;
    xEventGroupSetBits(s_audio_event_group, UI_EVENT_BT_CLOCK_TICK);
}

void app_main(void)
{
    esp_timer_create_args_t clock_timer_args = {
        .callback = clock_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &s_clock_timer_handle);
    esp_timer_start_periodic(s_clock_timer_handle, 1000000);

    s_audio_event_group = xEventGroupCreate();
    bt_audio_init("esp32_player");
    bt_audio_register_callback(on_bt_event);
    ESP_ERROR_CHECK(sdcard_init());
    ESP_ERROR_CHECK(display_init());

    xTaskCreatePinnedToCore(ui_audio_task, "audio_task", 4096, NULL, 6, &s_audio_task, 1);

    if (display_port_lock(0)) {
        ui_init();
        ui_bt_start_scan();
        display_port_unlock();
    } else {
        ESP_LOGE("app", "Failed to lock LVGL for screen creation");
    }
}
