idf_component_register(
    SRCS "sdcard.c" "file_scanner.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs vfs driver
)