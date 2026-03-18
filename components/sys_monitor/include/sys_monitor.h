#ifndef _SYS_MONITOR_H_
#define _SYS_MONITOR_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief In danh sách task ra serial log dạng bảng.
 */
esp_err_t sys_monitor_print_tasks(void);

/**
 * @brief In thông tin heap ra serial log.
 */
void sys_monitor_print_heap(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MONITOR_H_ */
