idf_component_register(
    SRCS
        "bt_audio_src.c"
        "bt_audio_snk.c"
        "decoder/bt_audio_decoder_raw.c"
        "decoder/bt_audio_decoder_wav.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash driver
)