## Issue list

> **Commit: "Fix issue race condition"**

Đây là race condition trong `bt_audio_play()`.

**Nguyên nhân:**

Nhìn vào thứ tự thực thi trong `bt_audio_src.c:949-953`:

```c
ret = bt_init_resource_playback();   // ← Tạo reader task ở đây
if (ret != ESP_OK) { dec->close(); return ret; }

s_decoder = dec;   // ← s_decoder được gán SAU khi task đã chạy
```

Trong `bt_init_resource_playback()`, reader task được tạo tại line 306 và bắt đầu chạy ngay lập tức trên Core 1. Task đó chạy đến line 243:

```c
int rd = s_decoder->read(buf, READ_CHUNK_SIZE);
```

Lúc này `s_decoder` vẫn còn là `NULL` vì `bt_audio_play()` chưa kịp gán `s_decoder = dec`. Truy cập `NULL->read` (offset 8) → `EXCVADDR: 0x00000008` → **LoadProhibited crash**.

**Giải pháp:**

Gán `s_decoder` trước khi tạo task

> **Commit: "Fix issue audio popping"**

**Nguyên nhân:**

Khi buffer thiếu data (`got < len`), callback sẽ thêm một padding silence zero vào một phần PCM thật. Sự nhảy đột ngột từ giá trị PCM xuống 0 tạo ra chuỗi không ổn định → popping.

**Giải pháp**

Khi underrun xảy ra, thì thực hiện xoá cờ `FLAG_PREFILLED` để output silence hoàn toàn cho đến khi fill đầy lại buffer.

Ngoài ra, thực hiện tăng kích thước buffer và số byte trong một lần đọc.

```c
#define BUF_SIZE            (24 * 1024)
#define PREFILL_SIZE        (20 * 1024)
#define READ_CHUNK_SIZE     1024
```

> **Commit: "Fix issue wdt trigger at bt_reader_task"**

**Nguyên nhân:**

Sau khi đọc hết file (`rd <= 0`), task quay lại vòng lặp và tiếp tục gọi `fread()` liên tục mà không có `vTaskDelay` nào → IDLE1 không bao giờ được chạy → watchdog triggered.

**Giải pháp:**

Thêm cờ `FLAG_READER_EOF`
- **Set:** Trong `bt_reader_task` khi `rd <= 0`
- **Clear:** Trong `bt_audio_play()`, `bt_audio_seek()`, `bt_audio_stop()` (bất cứ nơi nào có thể restart hoặc reposition file).

Khi end of file thì block với `vTaskDelay(50)` cho đến khi:
  - `FLAG_STOP_READER` được set (khi `bt_audio_stop()`) → task thoát
  - `FLAG_READER_EOF` bị clear → task tiếp tục đọc

> **Commit: "Increase the priority of the reader_task task"**

**File:** [bt_audio_src.c:314-316](./components/bt_audio/bt_audio_src.c#L314-L316), [hardware.h:40](./components/display/include/hardware.h#L40), [main.c:174](./main/main.c#L174)

**Nguyên nhân:**

- `reader_task` (prio **5**) chạy trên **Core 1**
- `lvgl_task` (prio **5**) chạy trên **Core 1** — round-robin → chia CPU
- `audio_task` (prio **6**) chạy trên **Core 1** — **preempt** `reader_task` mỗi lần có event.

Trong đó: `reader_task` là một task đọc sdcard để gửi cho pcm data cho bt stack
  -> Điều này là realtime.
  -> Task này cần có độ ưu tiên cao hơn lvgl để tránh việc khi lvgl render thì bị cạn buffer do `reader_task` không được chạy.

**Giải pháp:**

Tăng độ ưu tiên của `reader_task`

```c
#define READER_TASK_PRIO    (7)

xTaskCreatePinnedToCore(bt_reader_task, "reader_task",
                        READER_TASK_STACK, NULL,
                        READER_TASK_PRIO, &s_reader_task, 1);
```

> **Commit: "Memory leak in bt_audio_check_bonded_devices"**

**File:** [bt_audio_src.c:499-513](./components/bt_audio/bt_audio_src.c#L499-L513)

```c
static bool bt_audio_check_bonded_devices(uint8_t* bda) {
    esp_bd_addr_t *dev_list = malloc(dev_num * sizeof(esp_bd_addr_t));
    // ...
    for (int i = 0; i < dev_num; i++) {
        if (memcmp(bda, dev_list[i], 6) == 0) {
            return true;   // ← LEAK: không free(dev_list)!
        }
    }
    free(dev_list);
    return false;
}
```

**Nguyên nhân:**

Khi tìm thấy device đã bond, `return true` mà **không `free(dev_list)`**. Mỗi lần discovery tìm thấy bonded device = leak `dev_num × 6` bytes.

> **Commit: "Actually clear prefill when reaching the threshold"**

**File:** [bt_audio_src.c:387-396](./components/bt_audio/bt_audio_src.c#L387-L396)

```c
if ((int32_t)got < len) {
    memset(out + got, 0, len - got);
    if (!(flags & FLAG_END_OF_STREAM)) {
        bt_flag_clear(FLAG_PREFILLED);   // ← Clear prefill flag
    }
}
```

**Vấn đề:**

- Khi xảy ra underrun (dù chỉ thiếu **1 byte**), code clear `FLAG_PREFILLED`
- Callback tiếp theo thấy `!FLAG_PREFILLED` → output silence
- Reader phải fill lại 20KB (113ms đọc SD card) trước khi audio tiếp tục
- Kết quả: 1 underrun nhỏ → khoảng lặng dài 100-200ms nghe rất rõ

**Giải pháp:** 

Thay vì clear prefilled ngay, dùng threshold thấp hơn:

```c
if ((int32_t)got < len) {
    memset(out + got, 0, len - got);
    
    /* Chỉ clear prefilled khi buffer thực sự cạn kiệt (< 2KB) */
    if (!(flags & FLAG_END_OF_STREAM)) {
        if (xStreamBufferBytesAvailable(s_stream_buf) < 2048)
        {
            bt_flag_clear(FLAG_PREFILLED);
        }
    }
}
```

> **Commit: "Issue when change track"**

Trong `bt_audio_play()` khi chuyển track (đang phát bài A → play bài B):

```c
// line 952: Pause reader
bt_flag_set(FLAG_PAUSED);

// line 964: Open bài mới
dec->open(path, &s_file_info);

// line 968-970: Reset state
s_decoder = dec;
atomic_store(&s_bytes_played, 0);
atomic_store(&s_flags, 0);   // ← Clear all flags

// line 972: Reuse existing task + buffer
bt_init_resource_playback(); // ← Returns ESP_OK immediately
```

> [!CAUTION]
> Stream buffer không bị reset khi chuyển bài! Old PCM data từ bài A vẫn nằm trong buffer. Reader bắt đầu ghi data bài B vào phía sau. Callback sẽ đọc: `[old data bài A] → [new data bài B]` → glitch tại điểm chuyển.

**Giải pháp:**

Thêm `xStreamBufferReset()` trước khi bắt đầu bài mới.