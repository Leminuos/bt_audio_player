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
