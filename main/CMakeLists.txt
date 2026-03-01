idf_component_register(
    SRCS
        "main.c"
        "ui_file_list_custom.c"
    
    INCLUDE_DIRS
        "."
    
    REQUIRES
        "display"
        "bt_audio"
        "sdcard"
)
