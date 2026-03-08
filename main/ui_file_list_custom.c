#include <string.h>
#include <ctype.h>

#include "ui.h"
#include "file_scanner.h"
#include "bt_audio.h"
#include "esp_err.h"

#define AUDIO_NO_TRACK_PREV     (-1)
#define AUDIO_NO_TRACK_NEXT     (-1)

static int          g_track_cur;
static int          g_track_next;
static int          g_track_prev;
static file_list_t  g_file_list;
static lv_style_t   style_item_default;
static lv_style_t   style_item_pressed;

static void init_file_item_styles(void);
static bool file_is_audio(const char *filename);
static void cb_file_item_clicked(lv_event_t *e);
static void cb_folder_item_clicked(lv_event_t *e);
static lv_obj_t *create_file_item(lv_obj_t *parent, file_entry_t *entry);

void ui_refresh_file_list(const char *dir_path) {
    esp_err_t ret = file_scanner_scan(dir_path, &g_file_list);
    if (ret != ESP_OK) return;

    init_file_item_styles();
    lv_obj_clean(ui_PanelFileList);

    lv_obj_set_flex_flow(ui_PanelFileList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_PanelFileList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(ui_PanelFileList, 2, 0);
    lv_obj_set_style_pad_all(ui_PanelFileList, 4, 0);
    lv_obj_set_style_bg_color(ui_PanelFileList, lv_color_hex(0xFFFFFF), 0);

    if (strcmp(dir_path, "/sdcard") != 0) {
        lv_obj_t *item_back = create_file_item(ui_PanelFileList, NULL);
        lv_obj_add_event_cb(item_back, cb_folder_item_clicked, LV_EVENT_CLICKED, NULL);
    }

    for (int i = 0; i < g_file_list.count; i++) {
        file_entry_t *entry = &g_file_list.items[i];

        if (entry->is_dir) {
            lv_obj_t *item = create_file_item(ui_PanelFileList, entry);
            lv_obj_add_event_cb(item, cb_folder_item_clicked, LV_EVENT_CLICKED, entry);
        } else {
            if (file_is_audio(entry->name)) {
                lv_obj_t *item = create_file_item(ui_PanelFileList, entry);
                lv_obj_add_event_cb(item, cb_file_item_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            }
        }
    }
}

static void init_file_item_styles(void) {
    // --- Style bình thường ---
    lv_style_init(&style_item_default);
    lv_style_set_bg_opa(&style_item_default, LV_OPA_TRANSP);    // nền trong suốt
    lv_style_set_border_width(&style_item_default, 0);          // bỏ border hoàn toàn
    lv_style_set_radius(&style_item_default, 4);                // bo góc nhẹ
    lv_style_set_pad_all(&style_item_default, 8);
    lv_style_set_pad_column(&style_item_default, 8);            // khoảng cách giữa icon và text

    // --- Style khi pressed ---
    lv_style_init(&style_item_pressed);
    lv_style_set_bg_opa(&style_item_pressed, LV_OPA_COVER);
    lv_style_set_bg_color(&style_item_pressed, lv_color_hex(0xCCE8FF));
    lv_style_set_border_width(&style_item_pressed, 1);
    lv_style_set_border_color(&style_item_pressed, lv_color_hex(0x99D1FF));
}

static lv_obj_t *create_file_item(lv_obj_t *parent, file_entry_t *entry) {

    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 40);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_style(item, &style_item_default, LV_STATE_DEFAULT);
    lv_obj_add_style(item, &style_item_pressed, LV_STATE_PRESSED);

    lv_obj_t *icon = lv_label_create(item);
    if (entry == NULL) {
        lv_label_set_text(icon, LV_SYMBOL_LEFT);
    } else if (entry->is_dir) {
        lv_label_set_text(icon, LV_SYMBOL_DIRECTORY);
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFFCC00), 0);
    } else {
        lv_label_set_text(icon, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, lv_color_hex(0x4FC3F7), 0);
    }

    lv_obj_t *label = lv_label_create(item);
    lv_label_set_text(label, entry ? entry->name : ".. (Back)");
    lv_obj_set_flex_grow(label, 1);

    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    if (entry && !entry->is_dir) {
        lv_obj_t *size_lbl = lv_label_create(item);
        char size_str[16];
        if (entry->size >= 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.1fMB",
                     (float)entry->size / (1024 * 1024));
        } else {
            snprintf(size_str, sizeof(size_str), "%luKB",
                     (unsigned long)(entry->size / 1024));
        }
        lv_label_set_text(size_lbl, size_str);
        lv_obj_set_style_text_color(size_lbl, lv_color_hex(0x888888), 0);
    }

    return item;
}

static bool file_is_audio(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return false;

    char ext[8] = {0};
    int len = strlen(dot + 1);
    if (len >= sizeof(ext)) return false;

    for (int i = 0; i < len; i++) {
        ext[i] = tolower((unsigned char)dot[1 + i]);
    }

    // So sánh với các định dạng hỗ trợ
    return (strcmp(ext, "mp3") == 0 ||
            strcmp(ext, "wav") == 0 ||
            strcmp(ext, "raw") == 0 ||
            strcmp(ext, "aac") == 0);
}

static void cb_folder_item_clicked(lv_event_t *e) {
    file_entry_t *entry = (file_entry_t *)lv_event_get_user_data(e);

    if (entry == NULL) {
        char parent[MAX_PATH_LEN];
        strncpy(parent, g_file_list.dir, MAX_PATH_LEN);
        char *last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
        }
        ui_refresh_file_list(parent);
    } else {
        ui_refresh_file_list(entry->path);
    }
}

extern bool ui_is_loop;
extern bool ui_is_finish;
extern bool ui_is_playing;

static int audio_track_prev(int track_cur) {
    for (int i = track_cur - 1; i >= 0; --i) {
        file_entry_t *e = &g_file_list.items[i];
        if (e && !e->is_dir && file_is_audio(e->name)) return i;
    }

    return AUDIO_NO_TRACK_PREV;
}

static int audio_track_next(int track_cur) {
    for (int i = track_cur + 1; i < g_file_list.count; ++i) {
        file_entry_t *e = &g_file_list.items[i];
        if (e && !e->is_dir && file_is_audio(e->name)) return i;
    }

    return AUDIO_NO_TRACK_NEXT;
}

static void ui_refresh_player(file_entry_t *entry)
{
    ui_is_playing = true;
    ui_is_loop    = false;
    ui_is_finish  = false;

    uint8_t vol = bt_audio_get_volume();
    lv_label_set_text(ui_lblSongName, entry->name);
    lv_label_set_text(ui_lblTimeElapsed, "00:00");
    lv_label_set_text(ui_lblBtnPlayPause, LV_SYMBOL_PAUSE);
    lv_slider_set_value(ui_sliderProgress, 0, LV_ANIM_OFF);
    lv_slider_set_value(ui_sliderVolume, vol, LV_ANIM_ON);
    lv_obj_set_style_opa(ui_btn_prev, (g_track_prev != AUDIO_NO_TRACK_PREV) ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(ui_btn_next, (g_track_next != AUDIO_NO_TRACK_NEXT) ? LV_OPA_COVER : LV_OPA_40, 0);

    ESP_ERROR_CHECK(bt_audio_play(entry->path));

    bt_audio_playback_pos_t p = {0};
    bt_audio_get_position(&p);
    lv_label_set_text_fmt(ui_lblTimeTotal, "%02ld:%02ld",
                        ((p.duration_ms / 1000) / 60),
                        ((p.duration_ms / 1000) % 60));
}

static void cb_file_item_clicked(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    file_entry_t *entry = &g_file_list.items[idx];
    if (!entry) return;

    g_track_cur  = idx;
    g_track_next = audio_track_next(g_track_cur);
    g_track_prev = audio_track_prev(g_track_cur);

    /* Gửi absolute volume */
    bt_audio_set_volume(18);

    // Cập nhật UI player
    ui_is_loop = false;
    ui_refresh_player(entry);

    // Chuyển screen
    lv_scr_load_anim(ui_audioplayer, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

esp_err_t ui_audio_prev_track() {
    if (g_track_prev == AUDIO_NO_TRACK_PREV) return ESP_FAIL;

    file_entry_t *entry = &g_file_list.items[g_track_prev];
    if (!entry) return ESP_FAIL;

    g_track_next = g_track_cur;
    g_track_cur  = g_track_prev;
    g_track_prev = audio_track_prev(g_track_cur);

    // Cập nhật UI player
    ui_refresh_player(entry);

    return ESP_OK;
}

esp_err_t ui_audio_next_track() {
    if (g_track_next == AUDIO_NO_TRACK_NEXT) return ESP_FAIL;

    file_entry_t *entry = &g_file_list.items[g_track_next];
    if (!entry) return ESP_FAIL;

    g_track_prev = g_track_cur;
    g_track_cur  = g_track_next;
    g_track_next = audio_track_next(g_track_cur);

    // Cập nhật UI player
    ui_refresh_player(entry);
    
    return ESP_OK;
}
