#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "sdcard.h"
#include "bt_audio.h"

static const char *TAG = "bt_test";

#define AUDIO_FILE      SDCARD_MOUNT_POINT "/am_tham_ben_em.raw"
#define BT_DEVICE_NAME  "ESP32_AudioTest"

/* ─── Event group bits ────────────────────────────────────────────────────── */

#define EVT_BIT_CONNECTED       (1 << 0)
#define EVT_BIT_DISCONNECTED    (1 << 1)
#define EVT_BIT_DISCOVERY_DONE  (1 << 2)
#define EVT_BIT_TRACK_FINISHED  (1 << 3)

static EventGroupHandle_t s_evt_group = NULL;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*                        BT EVENT CALLBACK                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void bt_event_handler(const bt_audio_event_t *event)
{
    switch (event->type) {
    case BT_AUDIO_EVT_STATE_CHANGED: {
        if (!event->data) break;
        const bt_audio_evt_state_changed_t *sc = event->data;
        ESP_LOGI(TAG, "[EVT] State: %d → %d", sc->old_state, sc->new_state);

        switch (sc->new_state) {
        case BT_AUDIO_STATE_CONNECTED:
            xEventGroupSetBits(s_evt_group, EVT_BIT_CONNECTED);
            break;
        case BT_AUDIO_STATE_DISCONNECTED:
            xEventGroupSetBits(s_evt_group, EVT_BIT_DISCONNECTED);
            break;
        case BT_AUDIO_STATE_DISCOVERY_DONE:
            xEventGroupSetBits(s_evt_group, EVT_BIT_DISCOVERY_DONE);
            break;
        default: break;
        }
        break;
    }

    case BT_AUDIO_EVT_DISCOVERY_RESULT:
        ESP_LOGI(TAG, "[EVT] New device discovered");
        break;

    case BT_AUDIO_EVT_TRACK_FINISHED:
        ESP_LOGI(TAG, "[EVT] Track finished!");
        xEventGroupSetBits(s_evt_group, EVT_BIT_TRACK_FINISHED);
        break;

    case BT_AUDIO_EVT_DATA_UPDATE:
        /* UI update — không log vì fire quá thường xuyên */
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*                           TEST FUNCTIONS                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Test 1: Init Bluetooth stack
 */
static bool test_bt_init(void)
{
    ESP_LOGI(TAG, "━━━ TEST 1: bt_audio_init ━━━");

    esp_err_t ret = bt_audio_init(BT_DEVICE_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: bt_audio_init returned %s", esp_err_to_name(ret));
        return false;
    }

    bt_audio_register_callback(bt_event_handler);

    if (bt_audio_get_state() != BT_AUDIO_STATE_INIT) {
        ESP_LOGE(TAG, "FAIL: Expected state INIT");
        return false;
    }

    ESP_LOGI(TAG, "PASS: BT initialized");
    return true;
}

/**
 * @brief Test 2: Discovery
 */
static bool test_discovery(void)
{
    ESP_LOGI(TAG, "━━━ TEST 2: Discovery ━━━");

    bt_audio_start_discovery(false);

    EventBits_t bits = xEventGroupWaitBits(s_evt_group,
                                           EVT_BIT_DISCOVERY_DONE,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(15000));

    if (!(bits & EVT_BIT_DISCOVERY_DONE)) {
        ESP_LOGW(TAG, "Discovery timeout — stopping");
        bt_audio_stop_discovery();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    uint16_t count = 0;
    bt_audio_discovery_get_count(&count);
    bt_audio_discovery_print();

    if (count == 0) {
        ESP_LOGW(TAG, "WARN: No audio devices found");
        return false;
    }

    ESP_LOGI(TAG, "PASS: Found %d device(s)", count);
    return true;
}

/**
 * @brief Test 3: Connect
 */
static bool test_connect(void)
{
    ESP_LOGI(TAG, "━━━ TEST 3: Connect ━━━");

    esp_err_t ret = bt_audio_connect_by_index(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: connect returned %s", esp_err_to_name(ret));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_evt_group,
                                           EVT_BIT_CONNECTED | EVT_BIT_DISCONNECTED,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(10000));

    if (bits & EVT_BIT_CONNECTED) {
        bt_audio_device_info_t info;
        if (bt_audio_get_device_info(&info) == ESP_OK)
            ESP_LOGI(TAG, "PASS: Connected to \"%s\" [%s]", info.name, info.bda_str);
        return true;
    }

    ESP_LOGE(TAG, "FAIL: Connection failed or timeout");
    return false;
}

/**
 * @brief Test 4: Volume control
 */
static bool test_volume(void)
{
    ESP_LOGI(TAG, "━━━ TEST 4: Volume Control ━━━");

    bool pass = true;
    uint8_t test_values[] = {0, 25, 50, 75, 100};
    for (int i = 0; i < (int)(sizeof(test_values) / sizeof(test_values[0])); i++) {
        bt_audio_set_volume(test_values[i]);
        uint8_t got = bt_audio_get_volume();
        if (got != test_values[i]) {
            ESP_LOGE(TAG, "FAIL: set %d, got %d", test_values[i], got);
            pass = false;
        }
    }

    /* Clamp test: >100 → 100 */
    bt_audio_set_volume(150);
    if (bt_audio_get_volume() != 100) {
        ESP_LOGE(TAG, "FAIL: Volume >100 not clamped");
        pass = false;
    }

    bt_audio_set_volume(80);
    ESP_LOGI(TAG, "%s: Volume control", pass ? "PASS" : "FAIL");
    return pass;
}

/**
 * @brief Test 5: Playback — phát file và chờ track finished
 */
static bool test_playback(void)
{
    ESP_LOGI(TAG, "━━━ TEST 5: Playback ━━━");

    /* Verify file */
    struct stat st;
    if (stat(AUDIO_FILE, &st) != 0) {
        ESP_LOGE(TAG, "FAIL: File not found: %s", AUDIO_FILE);
        return false;
    }

    ESP_LOGI(TAG, "File: %s (%ld bytes)", AUDIO_FILE, st.st_size);

    /* Play — module tự lo mọi thứ */
    esp_err_t ret = bt_audio_play(AUDIO_FILE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: bt_audio_play returned %s", esp_err_to_name(ret));
        return false;
    }

    bt_audio_set_volume(80);

    /* Log title nếu có */
    const char *title = bt_audio_get_title();
    if (title[0])
        ESP_LOGI(TAG, "Title: %s", title);

    /* Lấy duration để tính timeout */
    bt_audio_playback_pos_t pos;
    bt_audio_get_position(&pos);
    uint32_t timeout_ms = pos.duration_ms + 10000;
    ESP_LOGI(TAG, "Duration: %lu ms, waiting... (timeout=%lu ms)",
             (unsigned long)pos.duration_ms, (unsigned long)timeout_ms);

    /* Log progress mỗi 10s */
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        EventBits_t bits = xEventGroupWaitBits(s_evt_group,
                                               EVT_BIT_TRACK_FINISHED | EVT_BIT_DISCONNECTED,
                                               pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(10000));

        if (bits & EVT_BIT_DISCONNECTED) {
            ESP_LOGE(TAG, "FAIL: Disconnected during playback");
            return false;
        }
        if (bits & EVT_BIT_TRACK_FINISHED) {
            bt_audio_get_position(&pos);
            ESP_LOGI(TAG, "PASS: Track finished at %lu/%lu ms (%u%%)",
                     (unsigned long)pos.position_ms,
                     (unsigned long)pos.duration_ms,
                     pos.progress_pct);
            return true;
        }

        elapsed += 10000;
        bt_audio_get_position(&pos);
        ESP_LOGI(TAG, "  Progress: %lu/%lu ms (%u%%)",
                 (unsigned long)pos.position_ms,
                 (unsigned long)pos.duration_ms,
                 pos.progress_pct);
    }

    ESP_LOGE(TAG, "FAIL: Playback timeout");
    return false;
}

/**
 * @brief Test 6: Pause / Resume
 */
static bool test_pause_resume(void)
{
    ESP_LOGI(TAG, "━━━ TEST 6: Pause / Resume ━━━");

    if (bt_audio_get_state() < BT_AUDIO_STATE_CONNECTED) {
        ESP_LOGW(TAG, "SKIP: Not connected");
        return false;
    }

    /* Play 10s audio */
    esp_err_t ret = bt_audio_play(AUDIO_FILE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: bt_audio_play returned %s", esp_err_to_name(ret));
        return false;
    }

    /* Chờ stream started */
    for (int i = 0; i < 500 && !bt_audio_is_streaming(); i++)
        vTaskDelay(pdMS_TO_TICKS(20));

    /* Phát 3s */
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Pause */
    bt_audio_pause();
    ESP_LOGI(TAG, "Paused. is_paused=%d, state=%d",
             bt_audio_is_paused(), bt_audio_get_state());

    if (!bt_audio_is_paused()) {
        ESP_LOGE(TAG, "FAIL: is_paused should be true");
        bt_audio_stop();
        return false;
    }

    /* Verify position không đổi khi pause */
    bt_audio_playback_pos_t pos1, pos2;
    bt_audio_get_position(&pos1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    bt_audio_get_position(&pos2);

    if (pos2.position_ms != pos1.position_ms) {
        ESP_LOGW(TAG, "WARN: position changed during pause (%lu → %lu ms)",
                 (unsigned long)pos1.position_ms, (unsigned long)pos2.position_ms);
    }

    /* Resume */
    bt_audio_resume();
    ESP_LOGI(TAG, "Resumed. is_paused=%d, state=%d",
             bt_audio_is_paused(), bt_audio_get_state());

    if (bt_audio_is_paused()) {
        ESP_LOGE(TAG, "FAIL: is_paused should be false");
        bt_audio_stop();
        return false;
    }

    /* Phát thêm 3s rồi stop */
    vTaskDelay(pdMS_TO_TICKS(3000));
    bt_audio_stop();

    ESP_LOGI(TAG, "PASS: Pause/Resume OK");
    return true;
}

/**
 * @brief Test 7: Seek
 */
static bool test_seek(void)
{
    ESP_LOGI(TAG, "━━━ TEST 7: Seek ━━━");

    if (bt_audio_get_state() < BT_AUDIO_STATE_CONNECTED) {
        ESP_LOGW(TAG, "SKIP: Not connected");
        return false;
    }

    esp_err_t ret = bt_audio_play(AUDIO_FILE);
    if (ret != ESP_OK) return false;

    for (int i = 0; i < 500 && !bt_audio_is_streaming(); i++)
        vTaskDelay(pdMS_TO_TICKS(20));

    /* Phát 2s */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Seek to 1:00 */
    ESP_LOGI(TAG, "Seek to 1:00");
    bt_audio_seek_ms(60000);

    vTaskDelay(pdMS_TO_TICKS(500));

    bt_audio_playback_pos_t pos;
    bt_audio_get_position(&pos);
    ESP_LOGI(TAG, "Position after seek: %lu ms", (unsigned long)pos.position_ms);

    /* Verify position gần 60000ms (tolerance ±500ms) */
    bool pass = (pos.position_ms >= 59500 && pos.position_ms <= 61000);
    if (!pass) {
        ESP_LOGE(TAG, "FAIL: Position too far from target 60000ms");
    }

    /* Phát thêm 3s rồi stop */
    vTaskDelay(pdMS_TO_TICKS(3000));
    bt_audio_stop();

    ESP_LOGI(TAG, "%s: Seek", pass ? "PASS" : "FAIL");
    return pass;
}

/**
 * @brief Test 8: Disconnect
 */
static bool test_disconnect(void)
{
    ESP_LOGI(TAG, "━━━ TEST 8: Disconnect ━━━");

    esp_err_t ret = bt_audio_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: disconnect returned %s", esp_err_to_name(ret));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_evt_group,
                                           EVT_BIT_DISCONNECTED,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(5000));

    if (bits & EVT_BIT_DISCONNECTED) {
        ESP_LOGI(TAG, "PASS: Disconnected, state=%d", bt_audio_get_state());
        return true;
    }

    ESP_LOGE(TAG, "FAIL: Disconnect timeout");
    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*                              MAIN                                          */
/* ═══════════════════════════════════════════════════════════════════════════ */

void app_main(void)
{
    ESP_LOGI(TAG, "╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   BT Audio Source — Integration Test       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝");

    s_evt_group = xEventGroupCreate();
    if (!s_evt_group) { ESP_LOGE(TAG, "Failed to create event group"); return; }

    if (sdcard_init() != ESP_OK) { ESP_LOGE(TAG, "SD card init failed"); return; }

    struct stat st;
    if (stat(AUDIO_FILE, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", AUDIO_FILE);
        return;
    }
    ESP_LOGI(TAG, "Audio file: %s (%ld bytes)", AUDIO_FILE, st.st_size);

    /* ─── Run tests ─────────────────────────────────────────────────────── */

    int passed = 0, failed = 0, total = 0;

    #define RUN_TEST(fn) do {           \
        total++;                        \
        if (fn()) { passed++; }         \
        else      { failed++; }         \
        vTaskDelay(pdMS_TO_TICKS(500)); \
    } while(0)

    RUN_TEST(test_bt_init);
    RUN_TEST(test_discovery);

    if (bt_audio_get_state() >= BT_AUDIO_STATE_DISCOVERY_DONE) {
        uint16_t count = 0;
        bt_audio_discovery_get_count(&count);
        if (count > 0) {
            RUN_TEST(test_connect);

            if (bt_audio_get_state() == BT_AUDIO_STATE_CONNECTED) {
                RUN_TEST(test_volume);
                RUN_TEST(test_playback);
                RUN_TEST(test_pause_resume);
                RUN_TEST(test_seek);
                RUN_TEST(test_disconnect);
            }
        }
    }

    /* ─── Summary ───────────────────────────────────────────────────────── */

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║            TEST SUMMARY                    ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  Total:  %2d                                ║", total);
    ESP_LOGI(TAG, "║  Passed: %2d                                ║", passed);
    ESP_LOGI(TAG, "║  Failed: %2d                                ║", failed);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════╝");
}
