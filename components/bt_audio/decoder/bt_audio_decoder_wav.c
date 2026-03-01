#include "bt_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE     *s_wav_file        = NULL;
static uint32_t  s_wav_data_offset = 0;
static uint32_t  s_wav_data_size   = 0;
static uint32_t  s_wav_bytes_read  = 0;

static esp_err_t wav_open(const char *path, bt_audio_file_info_t *info)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    uint8_t hdr[44];
    if (fread(hdr, 1, 44, f) != 44) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t audio_fmt   = hdr[20] | (hdr[21] << 8);
    uint16_t channels    = hdr[22] | (hdr[23] << 8);
    uint32_t sample_rate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    uint16_t bits        = hdr[34] | (hdr[35] << 8);

    if (audio_fmt != 1) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    if (channels != BT_AUDIO_CHANNELS || bits != BT_AUDIO_BITS) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    fseek(f, 12, SEEK_SET);
    uint8_t chunk_hdr[8];
    uint32_t data_offset = 0, data_size = 0;

    while (fread(chunk_hdr, 1, 8, f) == 8) {
        uint32_t sz = chunk_hdr[4] | (chunk_hdr[5] << 8)
                    | (chunk_hdr[6] << 16) | (chunk_hdr[7] << 24);
        if (memcmp(chunk_hdr, "data", 4) == 0) {
            data_offset = (uint32_t)ftell(f);
            data_size   = sz;
            break;
        }
        fseek(f, (long)sz, SEEK_CUR);
    }

    if (!data_offset) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    fseek(f, (long)data_offset, SEEK_SET);

    s_wav_file        = f;
    s_wav_data_offset = data_offset;
    s_wav_data_size   = data_size;
    s_wav_bytes_read  = 0;

    info->total_pcm_bytes = data_size;
    return ESP_OK;
}

static int wav_read(uint8_t *pcm_buf, size_t buf_size)
{
    if (!s_wav_file) return 0;

    uint32_t remaining = s_wav_data_size - s_wav_bytes_read;
    if (remaining == 0) return 0;

    size_t to_read = (buf_size < remaining) ? buf_size : remaining;
    size_t got = fread(pcm_buf, 1, to_read, s_wav_file);
    s_wav_bytes_read += (uint32_t)got;
    return (int)got;
}

static esp_err_t wav_seek(uint32_t pcm_offset)
{
    if (!s_wav_file) return ESP_ERR_INVALID_STATE;
    if (pcm_offset > s_wav_data_size) pcm_offset = s_wav_data_size;

    fseek(s_wav_file, (long)(s_wav_data_offset + pcm_offset), SEEK_SET);
    s_wav_bytes_read = pcm_offset;
    return ESP_OK;
}

static void wav_close(void)
{
    if (s_wav_file) { fclose(s_wav_file); s_wav_file = NULL; }
    s_wav_data_offset = 0;
    s_wav_data_size   = 0;
    s_wav_bytes_read  = 0;
}

const bt_audio_decoder_t bt_audio_wav_decoder = {
    .name  = "WAV",
    .open  = wav_open,
    .read  = wav_read,
    .seek  = wav_seek,
    .close = wav_close,
};
