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