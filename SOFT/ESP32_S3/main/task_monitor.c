// task_monitor.c
#include "task_monitor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TASK_MAX_COUNT 22

static const char* TAG = "TaskMonitor";

typedef struct {
	TaskHandle_t handle;
	const char* name;
	uint32_t stack_size;
	UBaseType_t priority;
	BaseType_t core_id;
} task_info_t;

static task_info_t task_info[TASK_MAX_COUNT];
static int task_count = 0;
static bool color_logs_enabled = false;

#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RED     "\033[31m"
#define COLOR_PURPLE  "\033[35m"

void task_motinor_init(uint32_t stack_size, UBaseType_t priority, BaseType_t core_id,uint32_t print_time) {
	
	TaskHandle_t monitor_task_handle = NULL;
	BaseType_t result = xTaskCreatePinnedToCore(task_memory_monitor, "task_mem_mon", stack_size, NULL, priority, &monitor_task_handle, core_id);	
	if (result == pdPASS)
	{
		task_monitor_register_task(monitor_task_handle, "task_mem_mon", stack_size, priority, core_id);
		ESP_LOGE("TASK CREATE", "TASK %s CREATE Free heap size: %d", "task_mem_mon", (int)xPortGetFreeHeapSize());
	}
	else {
		ESP_LOGE("TASK CREATE", "Failed to create task: %s", "task_mem_mon");
		ESP_LOGE("TASK CREATE", "Free heap size: %d", (int)xPortGetFreeHeapSize());
	}
	
};

static const char* get_color_by_percent(uint8_t percent_remain) {
	if (percent_remain >= 30) return COLOR_GREEN;
	if (percent_remain >= 10) return COLOR_YELLOW;
	return COLOR_RED;
}

static const char* get_color_by_state(eTaskState state) {
	switch (state) {
	case eRunning:   return COLOR_GREEN;
	case eReady:     return COLOR_YELLOW;
	case eBlocked:   return COLOR_PURPLE;
	case eSuspended:
	case eDeleted:   return COLOR_RED;
	default:         return "";
	}
}

void task_monitor_enable_colored_logs(void) {
	color_logs_enabled = true;
}

void task_monitor_register_task(TaskHandle_t handle, const char* name, uint32_t stack_size, UBaseType_t priority, BaseType_t core_id) {
	if (task_count >= TASK_MAX_COUNT) {
		ESP_LOGW(TAG, "Too many tasks registered");
		return;
	}

	task_info[task_count++] = (task_info_t) {
		.handle = handle,
		.name = name,
		.stack_size = stack_size,
		.priority = priority,
		.core_id = core_id
	};
}

static void print_memory_type(uint32_t caps, const char* label) {
	multi_heap_info_t info;
	heap_caps_get_info(&info, caps);

	size_t total = info.total_free_bytes + info.total_allocated_bytes;
	size_t free = info.total_free_bytes;
	size_t min = info.minimum_free_bytes;
	size_t largest = info.largest_free_block;

	uint8_t remain_percent = (total > 0) ? (free * 100 / total) : 0;
	const char* color = color_logs_enabled ? get_color_by_percent(remain_percent) : "";
	const char* reset = color_logs_enabled ? COLOR_RESET : "";

	printf("%-14s: Total: %6lu KB, Free: %6lu KB, Min: %6lu KB, Largest: %6lu KB (%s%3u%%%s)\n",
		label,
		(unsigned long)(total / 1024),
		(unsigned long)(free / 1024),
		(unsigned long)(min / 1024),
		(unsigned long)(largest / 1024),
		color,
		remain_percent,
		reset);
}

void task_monitor_print(bool sort_by_core) {
	
	TaskStatus_t *status_array;
	uint32_t total_stack = 0;
	
	uint32_t uxArraySize = uxTaskGetNumberOfTasks();
	status_array = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
	
	if (status_array != NULL)
	{
		uxArraySize = uxTaskGetSystemState( status_array,
			uxArraySize,
			NULL);
	}	

	UBaseType_t task_count_local = uxTaskGetSystemState(status_array, 20, NULL);

	if (sort_by_core) {
		for (int i = 0; i < task_count_local - 1; ++i) {
			for (int j = i + 1; j < task_count_local; ++j) {
				BaseType_t core_i = -1, core_j = -1;
				for (int k = 0; k < task_count; k++) {
					if (task_info[k].handle == status_array[i].xHandle) core_i = task_info[k].core_id;
					if (task_info[k].handle == status_array[j].xHandle) core_j = task_info[k].core_id;
				}
				if (core_i > core_j) {
					TaskStatus_t temp = status_array[i];
					status_array[i] = status_array[j];
					status_array[j] = temp;
				}
			}
		}
	}
	
	
	printf("\n%-16s | %-20s | Prio  | Total  | Stack rem | Rem %%| Core\n", "Name", "State");
	printf("-----------------+----------------------+-------+--------+-----------+------+------\n");

	for (int i = 0; i < task_count_local; i++) {
		TaskStatus_t* t = &status_array[i];
		const char* state = "Unknown";
		switch (t->eCurrentState) {
		case eRunning: state = "Running"; break;
		case eReady: state = "Ready"; break;
		case eBlocked: state = "Blocked"; break;
		case eSuspended: state = "Suspended"; break;
		case eDeleted: state = "Deleted"; break;
		default: break;
		}

		uint32_t total = 0;
		int core = -1;
		for (int j = 0; j < task_count; j++) {
			if (task_info[j].handle == t->xHandle) {
				total = task_info[j].stack_size * sizeof(StackType_t);
				core = task_info[j].core_id;
				break;
			}
		}
		if (total == 0) total = configMINIMAL_STACK_SIZE * sizeof(StackType_t);
		
		total_stack += total;
		
		uint32_t remain = t->usStackHighWaterMark * sizeof(StackType_t);
		uint8_t percent = (total > 0) ? (remain * 100 / total) : 0;

		const char* color = color_logs_enabled ? get_color_by_percent(percent) : "";
		const char* state_color = color_logs_enabled ? get_color_by_state(t->eCurrentState) : "";
		const char* reset = color_logs_enabled ? COLOR_RESET : "";

		printf("%-16s | %s%-20s%s | %5u | %6lu | %9lu | %s%3u%%%s | %d\n",
			t->pcTaskName,
			state_color,
			state,
			reset,
			(unsigned)t->uxCurrentPriority,
			(unsigned long)total,
			(unsigned long)remain,
			color,
			percent,
			reset,
			(core == tskNO_AFFINITY) ? -1 : core);
	}
	printf("\nTotal tasks num:  %6lu Total stack:  %6lu KB \n", (unsigned long)uxArraySize, (unsigned long)total_stack / 1024);
		
	printf("\nMemory Usage:\n");
	print_memory_type(MALLOC_CAP_INTERNAL, "Internal RAM");
	print_memory_type(MALLOC_CAP_SPIRAM, "SPIRAM");
	print_memory_type(MALLOC_CAP_DMA, "DMA RAM");
	print_memory_type(MALLOC_CAP_8BIT, "8-bit RAM");
	print_memory_type(MALLOC_CAP_32BIT, "32-bit RAM");
	
	vPortFree(status_array);
}

void task_memory_monitor(void* pvParameters) {
	while (1) {
	
		task_monitor_print(1);
		vTaskDelay(pdMS_TO_TICKS(10000)); // Пауза 10 сек
	}

		
}
