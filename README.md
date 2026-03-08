> **Commit: "Fix issue race condition"**

Đây là race condition trong `bt_audio_play()`.

***Nguyên nhân:***

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

**Fix:**

Gán `s_decoder` trước khi tạo task