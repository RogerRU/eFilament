// task_monitor.h
#ifndef TASK_MONITOR_H
#define TASK_MONITOR_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif
	void task_motinor_init(uint32_t stack_size, UBaseType_t priority, BaseType_t core_id, uint32_t print_time);
	void task_monitor_register_task(TaskHandle_t handle, const char* name, uint32_t stack_size, UBaseType_t priority, BaseType_t core_id);
	void task_monitor_print(bool sort_by_core);
	void task_monitor_enable_colored_logs(void);
	
	void task_memory_monitor(void* pvParameters);

#ifdef __cplusplus
}
#endif

#endif // TASK_MONITOR_H
