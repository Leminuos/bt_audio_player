# ESP32 Bluetooth Audio Source — API Guide

## Tổng quan

Module `bt_audio` phát audio qua Bluetooth A2DP trên ESP32. ESP32 đóng vai trò **A2DP Source**, gửi audio PCM đến loa/tai nghe Bluetooth (A2DP Sink).

Module tự quản lý reader task nội bộ — dev chỉ cần gọi `bt_audio_play(path)` và module lo toàn bộ việc đọc file, decode, buffer, stream qua BT.

**Platform:** ESP-IDF 5.4+ · ESP32-CYD (ESP32-2432S028R)

## Kiến trúc

### Decoder Abstraction

Module sử dụng decoder interface để hỗ trợ nhiều format audio:

```
bt_audio_play("/sdcard/song.wav")
    │
    ├── find_decoder(".wav") → bt_audio_wav_decoder
    ├── decoder->open()      → parse header, lấy metadata
    ├── reader task          → decoder->read() loop → stream buffer
    └── A2DP data callback   → lấy từ stream buffer → SBC encode → BT
```

Built-in decoders: WAV (PCM 16-bit stereo 44100 Hz), raw PCM (headerless).

Thêm decoder mới (MP3, FLAC, ...):

```c
const bt_audio_decoder_t my_mp3_decoder = {
    .name  = "MP3",
    .open  = mp3_open,      // parse ID3, init decoder
    .read  = mp3_read,      // decode frame → output PCM
    .seek  = mp3_seek,      // NULL nếu không hỗ trợ
    .close = mp3_close,
};

const char *ext[] = {"mp3"};
bt_audio_register_decoder(&my_mp3_decoder, ext, 1);
```

Decoder `read()` phải output PCM signed 16-bit LE, stereo, 44100 Hz. Nếu source khác format, decoder tự convert.

### State Machine

| State | Mô tả |
|-------|--------|
| `INIT` | BT stack đã init, chưa kết nối |
| `DISCOVERING` | Đang quét thiết bị |
| `DISCOVERY_DONE` | Quét xong |
| `CONNECTING` | Đang kết nối |
| `CONNECTED` | Đã kết nối, sẵn sàng phát |
| `PLAYING` | Đang phát audio |
| `PAUSED` | Tạm dừng |
| `DISCONNECTED` | Mất kết nối |

### Event System

Callback được gọi từ BT task context — return nhanh, không block.

| Event | Mô tả | Payload |
|-------|--------|---------|
| `STATE_CHANGED` | State thay đổi | `bt_audio_evt_state_changed_t` |
| `DISCOVERY_RESULT` | Phát hiện thiết bị BT | NULL |
| `TRACK_FINISHED` | Phát xong track, A2DP auto stop | NULL |
| `DATA_UPDATE` | A2DP vừa lấy data — dùng để update UI | `bt_audio_playback_pos_t` |

## API Reference

### Lifecycle

```c
esp_err_t bt_audio_init(const char *device_name);
void      bt_audio_register_callback(bt_audio_event_cb_t callback);
```

`bt_audio_init()` khởi tạo toàn bộ BT stack (NVS, controller, Bluedroid, A2DP, GAP, SSP) và đăng ký built-in WAV/raw decoders. Gọi **một lần duy nhất**.

### Discovery & Connection

```c
void      bt_audio_start_discovery(bool auto_connect);
void      bt_audio_stop_discovery(void);
void      bt_audio_discovery_print(void);
esp_err_t bt_audio_discovery_get_count(uint16_t *count);
esp_err_t bt_audio_discovery_get_results(uint16_t *count, bt_audio_discovered_dev_t *devs);
esp_err_t bt_audio_connect_by_index(int index);
esp_err_t bt_audio_connect(const uint8_t bda[6]);
esp_err_t bt_audio_disconnect(void);
```

`auto_connect=true`: tự kết nối thiết bị audio đầu tiên tìm thấy.
`auto_connect=false`: lưu danh sách, user chọn thủ công qua `connect_by_index()`.

### Playback

```c
esp_err_t bt_audio_play(const char *path);
void      bt_audio_pause(void);
void      bt_audio_resume(void);
void      bt_audio_stop(void);
void      bt_audio_seek_ms(uint32_t position_ms);
```

`bt_audio_play()` tự detect decoder theo extension, mở file, parse metadata, tạo reader task, start A2DP. Nếu đang phát bài khác sẽ stop trước rồi play bài mới.

Khi track phát hết tự nhiên, module auto stop A2DP và fire `TRACK_FINISHED`.

`bt_audio_seek_ms()` seek cả file lẫn stream buffer. Hoạt động khi đang play hoặc pause.

### Volume

```c
void    bt_audio_set_volume(uint8_t volume_pct);   // 0–100
uint8_t bt_audio_get_volume(void);
```

Software volume với logarithmic curve -40dB. 0 = mute, 100 = 0dB.

### Status

```c
esp_err_t        bt_audio_get_position(bt_audio_playback_pos_t *pos);
bt_audio_state_t bt_audio_get_state(void);
esp_err_t        bt_audio_get_device_info(bt_audio_device_info_t *info);
bool             bt_audio_is_paused(void);
bool             bt_audio_is_streaming(void);
const char      *bt_audio_get_title(void);
```

`bt_audio_get_title()` trả về tên bài hát từ file metadata (WAV: LIST/INFO INAM tag). Rỗng nếu file không có metadata.

### Decoder Registration

```c
esp_err_t bt_audio_register_decoder(const bt_audio_decoder_t *decoder,
                                    const char *const extensions[],
                                    size_t ext_count);
```

WAV (`.wav`) và raw PCM (`.pcm`, `.raw`) đã đăng ký sẵn trong `bt_audio_init()`. Gọi thêm cho custom decoders.

## Workflow điển hình

### Auto-connect & Play

```c
void on_bt_event(const bt_audio_event_t *evt)
{
    switch (evt->type) {
    case BT_AUDIO_EVT_STATE_CHANGED: {
        const bt_audio_evt_state_changed_t *s = evt->data;
        if (s->new_state == BT_AUDIO_STATE_CONNECTED)
            bt_audio_play("/sdcard/song.wav");
        if (s->new_state == BT_AUDIO_STATE_DISCONNECTED)
            bt_audio_start_discovery(true);
        break;
    }
    case BT_AUDIO_EVT_TRACK_FINISHED:
        // Load bài tiếp hoặc dừng
        break;
    case BT_AUDIO_EVT_DATA_UPDATE: {
        const bt_audio_playback_pos_t *pos = evt->data;
        // Update UI progress bar
        break;
    }
    default: break;
    }
}

void app_main(void)
{
    bt_audio_init("MyESP32");
    bt_audio_register_callback(on_bt_event);
    bt_audio_start_discovery(true);
}
```

### Manual Discovery & Connect

```c
bt_audio_start_discovery(false);
// Đợi DISCOVERY_DONE...
bt_audio_discovery_print();
bt_audio_connect_by_index(0);
```

### Playback Control

```c
bt_audio_play("/sdcard/music/song.wav");

bt_audio_pause();
bt_audio_resume();
bt_audio_seek_ms(105000);   // Seek đến 1:45
bt_audio_set_volume(50);

bt_audio_stop();
bt_audio_play("/sdcard/music/next.wav");
```

### Custom Decoder (MP3)

```c
// Implement 4 functions theo bt_audio_decoder_t interface
const bt_audio_decoder_t mp3_decoder = {
    .name = "MP3", .open = mp3_open, .read = mp3_read,
    .seek = mp3_seek, .close = mp3_close,
};

// Đăng ký sau init
const char *ext[] = {"mp3"};
bt_audio_register_decoder(&mp3_decoder, ext, 1);

// Play bình thường — auto detect
bt_audio_play("/sdcard/song.mp3");
```

## Cấu hình nội bộ

| Constant | Giá trị | Mô tả |
|----------|---------|--------|
| `STREAM_BUF_SIZE` | 8 KB | StreamBuffer size |
| `PREFILL_SIZE` | 4 KB | Ngưỡng bắt đầu phát |
| `READ_CHUNK` | 512 B | Chunk size cho reader task |
| `READER_STACK` | 4 KB | Stack size reader task |

Có thể điều chỉnh tùy theo heap khả dụng của board. BT Classic + Bluedroid chiếm đáng kể RAM, cần kiểm tra `esp_get_free_heap_size()` trước khi tăng buffer.