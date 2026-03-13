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
#include "freertos/event_groups.h"

static const char* TAG = "main";

#define UI_EVENT_BT_DISCOVERY_DONE      BIT0
#define UI_EVENT_BT_DEVICE_CONNECTED    BIT1
#define UI_EVENT_BT_TRACK_FINISHED      BIT3
#define UI_EVENT_BT_DEVICE_DISCONNECTED BIT4
#define UI_EVENT_BT_VOLUME_CHANGE       BIT5
#define UI_EVENT_BT_ALL                 BIT0 | BIT1 | BIT3 | BIT4 | BIT5

static EventGroupHandle_t s_audio_event_group;
static TaskHandle_t s_audio_task;

extern bool ui_is_loop;
extern bool ui_is_finish;
extern bool ui_is_playing;
extern void ui_bt_start_scan(void);
extern void ui_bt_stop_scan(void);
extern void ui_refresh_file_list(const char *dir_path);
extern esp_err_t ui_audio_next_track();

/* Runs on LVGL task context — all lv_* calls are thread-safe here */
static void ui_handle_bt_events(void *arg)
{
    EventBits_t bits = (EventBits_t)(uintptr_t)arg;

    if (bits & UI_EVENT_BT_DISCOVERY_DONE) {
        ui_bt_stop_scan();
    }

    if (bits & UI_EVENT_BT_DEVICE_CONNECTED) {
        lv_scr_load_anim(ui_explorer, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);

        bt_audio_device_info_t info = {0};
        if (bt_audio_get_device_info(&info) == ESP_OK) {
            lv_label_set_text_fmt(ui_ExplorerLabel, "Connected to: %s", info.name);
            ESP_LOGI(TAG, "Connected to: %s (%s)", info.name, info.bda_str);
        }

        ui_refresh_file_list("/sdcard");
    }

    if (bits & UI_EVENT_BT_TRACK_FINISHED) {
        if (ui_is_loop) bt_audio_seek(0);
        else {
            if (ui_audio_next_track() != ESP_OK) {
                ui_is_finish  = true;
                ui_is_playing = false;
                lv_label_set_text(ui_lblBtnPlayPause, LV_SYMBOL_PLAY);
            }
        }
    }

    if (bits & UI_EVENT_BT_DEVICE_DISCONNECTED) {
        ui_bt_start_scan();
        lv_scr_load_anim(ui_bt_select, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    }

    if (bits & UI_EVENT_BT_VOLUME_CHANGE) {
        uint8_t vol = bt_audio_get_volume();
        lv_slider_set_value(ui_sliderVolume, vol, LV_ANIM_OFF);
    }
}

static void ui_audio_task(void* param)
{
    (void) param;

    for ( ; ; ) {
        EventBits_t bits = xEventGroupWaitBits(s_audio_event_group,
                                  UI_EVENT_BT_ALL,
                                  pdTRUE,
                                  pdFALSE,
                                  portMAX_DELAY);

        display_run_on_ui(ui_handle_bt_events, (void *)(uintptr_t)bits);
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

static void ui_startup_hook(void *arg)
{
    ui_init();
    ui_bt_start_scan();
}

void app_main(void)
{
    s_audio_event_group = xEventGroupCreate();
    bt_audio_init("esp32_player");
    bt_audio_register_callback(on_bt_event);
    ESP_ERROR_CHECK(sdcard_init());
    ESP_ERROR_CHECK(display_init());

    xTaskCreatePinnedToCore(ui_audio_task, "audio_task", 4096, NULL, 6, &s_audio_task, 1);

    display_run_on_ui_sync(ui_startup_hook, NULL);
}
