#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "esp_err.h"

#define MAX_FILES       10
#define MAX_NAME_LEN    64
#define MAX_PATH_LEN    128

typedef struct {
    char name[MAX_NAME_LEN];
    char path[MAX_PATH_LEN];
    bool is_dir;
    size_t size;
} file_entry_t;

typedef struct {
    int count;
    char dir[MAX_PATH_LEN];
    file_entry_t items[MAX_FILES];
} file_list_t;

esp_err_t file_scanner_scan(const char *dir_path, file_list_t *list);

#ifdef __cplusplus
}
#endif
