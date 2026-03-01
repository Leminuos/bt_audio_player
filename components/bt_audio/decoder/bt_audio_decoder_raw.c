#include "bt_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE     *s_raw_file      = NULL;
static uint32_t  s_raw_file_size = 0;

static esp_err_t raw_open(const char *path, bt_audio_file_info_t *info)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    uint32_t size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    s_raw_file      = f;
    s_raw_file_size = size;
    info->total_pcm_bytes = size;

    return ESP_OK;
}

static int raw_read(uint8_t *pcm_buf, size_t buf_size)
{
    if (!s_raw_file) return 0;
    return (int)fread(pcm_buf, 1, buf_size, s_raw_file);
}

static esp_err_t raw_seek(uint32_t pcm_offset)
{
    if (!s_raw_file) return ESP_ERR_INVALID_STATE;
    if (pcm_offset > s_raw_file_size) pcm_offset = s_raw_file_size;
    fseek(s_raw_file, (long)pcm_offset, SEEK_SET);
    return ESP_OK;
}

static void raw_close(void)
{
    if (s_raw_file) { fclose(s_raw_file); s_raw_file = NULL; }
    s_raw_file_size = 0;
}

const bt_audio_decoder_t bt_audio_raw_decoder = {
    .name  = "RAW",
    .open  = raw_open,
    .read  = raw_read,
    .seek  = raw_seek,
    .close = raw_close,
};
