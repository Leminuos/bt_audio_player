#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#include "file_scanner.h"
#include "esp_log.h"

static const char *TAG = "file_scanner";

esp_err_t file_scanner_scan(const char *dir_path, file_list_t *list) {
    DIR *dir                = NULL;
    struct dirent *entry    = NULL;
    struct stat st          = {0};
    
    memset(list, 0, sizeof(file_list_t));
    strncpy(list->dir, dir_path, MAX_PATH_LEN - 1);

    dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return ESP_FAIL;
    }

    while ((entry = readdir(dir)) != NULL && list->count < MAX_FILES) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcasecmp(entry->d_name, "System Volume Information") == 0) continue;
        if (strcasecmp(entry->d_name, "$RECYCLE.BIN") == 0) continue;
        if (strcasecmp(entry->d_name, "RECYCLER") == 0) continue;
        if (strcasecmp(entry->d_name, "Thumbs.db") == 0) continue;
        if (strcasecmp(entry->d_name, "desktop.ini") == 0) continue;

        char full_path[MAX_PATH_LEN] = {0};
        int ret = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        if (ret < 0 || ret >= sizeof(full_path)) {
            ESP_LOGW(TAG, "Path truncated or error");
            continue;
        }

        if (stat(full_path, &st) == 0) {
            bool is_dir = S_ISDIR(st.st_mode);

            file_entry_t *item = &list->items[list->count];
            strncpy(item->name, entry->d_name, MAX_NAME_LEN - 1);
            strncpy(item->path, full_path, MAX_PATH_LEN - 1);
            item->is_dir = is_dir;
            item->size = is_dir ? 0 : st.st_size;

            list->count++;
        }
    }

    closedir(dir);

    for (int i = 0; i < list->count - 1; i++) {
        for (int j = i + 1; j < list->count; j++) {
            bool swap = false;

            if (!list->items[i].is_dir && list->items[j].is_dir) {
                swap = true;
            }
            else if (list->items[i].is_dir == list->items[j].is_dir) {
                if (strcasecmp(list->items[i].name, list->items[j].name) > 0) {
                    swap = true;
                }
            }

            if (swap) {
                file_entry_t tmp = list->items[i];
                list->items[i] = list->items[j];
                list->items[j] = tmp;
            }
        }
    }

    ESP_LOGI(TAG, "Scanned %s: %d items found", dir_path, list->count);
    return ESP_OK;
}