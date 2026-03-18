/*
 * System Monitor — Task & Heap monitoring for ESP32
 * ESP-IDF 5.4+
 */

#include "sys_monitor.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "sys_mon";

/* ─── Helper ──────────────────────────────────────────────────── */

static char task_state_char(eTaskState state)
{
    switch (state) {
    case eRunning:   return 'X';     /* eXecuting */
    case eReady:     return 'R';
    case eBlocked:   return 'B';
    case eSuspended: return 'S';
    case eDeleted:   return 'D';
    default:         return '?';
    }
}

static const char *core_str(UBaseType_t affinity)
{
    if (affinity == tskNO_AFFINITY) return "ANY";
    if (affinity == 0) return "0";
    if (affinity == 1) return "1";
    return "NaN";
}

/* ─── Print functions ────────────────────────────────────────────────────── */

esp_err_t sys_monitor_print_tasks(void)
{
    esp_err_t ret = ESP_OK;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *status_array = malloc(task_count * sizeof(TaskStatus_t));
    if (!status_array) {
        ret = ESP_ERR_NO_MEM;
    }
 
    uint32_t total_run_time = 0;
    task_count = uxTaskGetSystemState(status_array, task_count, &total_run_time);
    if (task_count == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }
 
    ESP_LOGI(TAG, "Total tasks: %lu\n", (unsigned long)task_count);
    printf("┌──────────────────┬───────┬──────┬──────┬────────────┬───────┐\n");
    printf("│ Task             │ State │ Prio │ Core │ Stack Free │ CPU%%  │\n");
    printf("├──────────────────┼───────┼──────┼──────┼────────────┼───────┤\n");
 
    for (UBaseType_t i = 0; i < task_count; i++) {
        const TaskStatus_t *t = &status_array[i];
 
        /* CPU percentage — only meaningful when run-time stats are enabled */
        float cpu_pct = 0.0f;
        if (total_run_time > 0)
            cpu_pct = (float)t->ulRunTimeCounter / (float)total_run_time * 100.0f;

        printf("│ %-16.16s │   %c   │  %2lu  │  %3s │ %7" PRIu32 " B  │ %5.1f │\n",
               t->pcTaskName,
               task_state_char(t->eCurrentState),
               (unsigned long)t->uxCurrentPriority,
               core_str(xTaskGetCoreID(t->xHandle)),
               (uint32_t)t->usStackHighWaterMark * sizeof(StackType_t),
               cpu_pct);
    }
 
    printf("└──────────────────┴───────┴──────┴──────┴────────────┴───────┘\n\n");

exit:
    free(status_array);
    return ret;
}

void sys_monitor_print_heap(void)
{
    size_t free_total   = esp_get_free_heap_size();
    size_t free_min     = esp_get_minimum_free_heap_size();
    size_t largest_blk  = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
 
    ESP_LOGI(TAG, "\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║            Heap Memory Summary               ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  Free (total)     : %8zu bytes           ║\n", free_total);
    printf("║  Largest block    : %8zu bytes           ║\n", largest_blk);
    printf("║  Minimum ever free: %8zu bytes           ║\n", free_min);
    printf("╚══════════════════════════════════════════════╝\n\n");
}
