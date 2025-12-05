#include "e_filament.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2c.h"
#include "esp_event.h"

#include "esp_task.h"
#include "esp_sntp.h"

#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_spiffs.h"
#include "esp_http_server.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "lvgl.h"

#include "DRIVERS/SH6801/ws_esp32s3_amoled_143.h"
#include "DRIVERS/PCF85063A/PCF85063A.h"
#include "DRIVERS/PCF85063A/timegm.h"

#include "wifi.h"
#include "UI/ui.h"
#include "UI/styles.h"
#include <UI/screens.h>
#include "UI/actions.h"

#include "comm.h"

#define DEBUG_TOUCH 0

#define USE_TASK_MON 0

#if USE_TASK_MON == 1
#include "task_monitor.h"
#endif

#if DEBUG_TOUCH == 1
static lv_obj_t *my_Cir;
#endif

QueueHandle_t queue_rx = NULL;
QueueHandle_t queue_rx_parse = NULL;
QueueHandle_t queue_tx = NULL;
QueueHandle_t queue_update = NULL;
QueueHandle_t qSend = NULL;

TimerHandle_t tim_1sec = NULL; // таймер отсчета времени 1 сек.
TimerHandle_t tim_ssaver = NULL; // таймер скринсейвера.
TimerHandle_t tim_msgbox_simple = NULL; // таймер MessageBox простой.
TimerHandle_t tim_heartbeat = NULL; // таймер heartbeat.
TimerHandle_t tim_return = NULL; // таймер возврата в scr_main
TimerHandle_t tim_rec = NULL; // таймер записи NFC
TimerHandle_t tim_update_time_PCF = NULL; // таймер обновления времени с PCF
TimerHandle_t tim_sync_time_SNTP = NULL; // таймер обновления времени с SNTP
TimerHandle_t tim_msg_box_live = NULL; // таймер жизни messageBox
TimerHandle_t tim_nfc_rec = NULL; // таймер отсчета записи NFC
SemaphoreHandle_t i2c_mutex = NULL; // мьютекс i2c

EventGroupHandle_t ev_time_sync = NULL;
EventGroupHandle_t ev_states = NULL;

EventBits_t  bits_state;

lv_obj_t* msg_box = NULL;

enum timerID
{
	timID_1sec            = 0,
	timID_heartbeat       = 1,
	timID_return          = 2,
	timID_ssaver          = 3,
	timID_update_time_PCF = 4,
	timID_sync_time_SNTP  = 5,
	timID_msg_box_live    = 6,
	timID_nfc_rec         = 7
};

static const char *FS_TAG = "SPIFFS";
static const char *FS_PROFILE = "/storage/eFilament.csv";

wifi_t wifi = { 0 };
profile_t profiles[PROFILES_MAX_COUNT];
state_t state;
const uart_port_t uart_number = UART_NUM_2;

update_t ui_upd;


static char options_buf[2048];	// буфер для профайлов в lv_roller
volatile uint32_t touch_count;

static const char *TAG = "RTC";
struct tm rtcTime = { 0 };
struct tm *newTime = &rtcTime;
time_t now;
int err;

NFC_result_t NFC_result = 0;

void time_get_from_PCF(void)
{
	PCF_DateTime currentRTC = { 0 };
	PCF_GetDateTime(&currentRTC);
	time_set_system(currentRTC.year, currentRTC.month, currentRTC.day, currentRTC.hour, currentRTC.minute, currentRTC.second);

	ESP_LOGI("RTC", "Set time from PCF: %02d:%02d:%02d  %02d.%02d.%d", currentRTC.hour, currentRTC.minute, currentRTC.second, currentRTC.day, currentRTC.month, currentRTC.year);
}
void time_set_system(uint16_t year,
	uint8_t month,
	uint8_t day,
	uint8_t hour,
	uint8_t minute,
	uint8_t second)
{
	struct tm tm = {
		.tm_year = year - 1900,
		// Год с 1900 (2024 → 124)
		.tm_mon = month - 1,
		// Месяц 0-11 (январь = 0)
		.tm_mday = day,
		.tm_hour = hour,
		.tm_min = minute,
		.tm_sec = second
	};

	time_t t = mktime(&tm); // Конвертируем в time_t
	struct timeval sys_now = { .tv_sec = t };
	settimeofday(&sys_now, NULL); // Устанавливаем системное время

//	printf("System time set: %04d-%02d-%02d %02d:%02d:%02d\n",year,	month,day,hour,	minute,	second);
}
void time_set_to_PCF()
{
	time(&now);
	localtime_r(&now, newTime);

	err = PCF_updateRTC(newTime); // Updates RTC with your new timestamp

	if (err == -1)
	{
		ESP_LOGE(TAG, "Failed to update RTC time - Fail during write in registers");
	}
	else if (err == -2)
	{
		ESP_LOGE(TAG, "%s - Failed to update the RTC time - Invalid date parameters", __ASSERT_FUNC);
	}
	else {
	}
}

int config_load()
{
	config_init();
	// Чтение конфигурации из файла
	state.options_count = config_read("/storage/options.cfg");
	if (state.options_count < 0) {
		ESP_LOGE("CONFIG", "Failed to read config file or file doesn't exist.\n");
		return -1;
	}
	if (!state.options_count) {
		ESP_LOGE("CONFIG", "No options found!");
		return 0;
	}

	strncpy(wifi.ssid, config_get_value("SSID"), sizeof(wifi.ssid));
	strncpy(wifi.pass, config_get_value("PASS"), sizeof(wifi.pass));

	if (strlen(wifi.ssid) && strlen(wifi.pass)) wifi.hasPass = 1;

	state.ADC_offset = (uint32_t) config_get_int("ADC_OFFSET", 8465341);
	state.ADC_fullscale = (uint32_t) config_get_int("ADC_FULLSCALE", 202750);

	ESP_LOGI("CONFIG", "Read %d options from config file.", state.options_count);

	return state.options_count;
}

int config_save()
{
	config_set_string("SSID", wifi.ssid);
	config_set_string("PASS", wifi.pass);

	config_set_int("ADC_OFFSET", state.ADC_offset);
	config_set_int("ADC_FULLSCALE", state.ADC_fullscale);

	config_set_string("LAST_PROFILE_ID", state.profile_active_id);

	config_write("/storage/options.cfg");

	return 0;
}
void eFil_init()
{
	if (!wifi.SNTP_synchronized)
	{
		time_set_system(2025, 0, 1, 0, 0, 0);
	}
	PCF_Init();
	time_set_to_PCF();

	eFil_init_uart();
	eFil_init_queues();
	eFil_init_tims();
	eFil_init_events();
	eFil_init_tasks();

	eFil_ui_roller_profiles_set(NULL, NULL);
	state.activ_screen = sys_getLvglObjectFromIndex(SCREEN_ID_MAIN - 1);

	xTimerStart(tim_1sec, portMAX_DELAY);
	xTimerStart(tim_ssaver, portMAX_DELAY);
	xTimerStart(tim_return, portMAX_DELAY);
	xTimerStart(tim_update_time_PCF, portMAX_DELAY);
	xTimerStart(tim_sync_time_SNTP, portMAX_DELAY);

	config_load();
	eFil_make_packet(COMM_TYPE_INFO, COMM_INFO_ADC_REGISTORS, state.ADC_offset, state.ADC_fullscale, NULL, true);
	eFil_profile_set_active(config_get_value("LAST_PROFILE_ID"));
}

/**
 * @brief Инициализация задач
 *
 */
void eFil_init_tasks()
{
#if USE_TASK_MON == 1
	TaskHandle_t h;
	task_motinor_init(1024 * 4, 12, 1, 20000);
	task_monitor_enable_colored_logs();
	// регистрируем все известные системные задачи
	h = xTaskGetHandle("LVGL");
	if (h) task_monitor_register_task(h, "LVGL", AMOLED_LVGL_TASK_STACK_SIZE, AMOLED_LVGL_TASK_PRIORITY, -1);

	h = xTaskGetHandle("esp_timer");
	if (h) task_monitor_register_task(h, "esp_timer", ESP_TASK_TIMER_STACK, ESP_TASK_TIMER_PRIO, CONFIG_ESP_TIMER_TASK_AFFINITY);

	h = xTaskGetHandle("swdraw");
	if (h) task_monitor_register_task(h, "swdraw", LV_DRAW_THREAD_STACK_SIZE, LV_THREAD_PRIO_HIGH, -1);

	h = xTaskGetHandle("wifi");
	if (h) task_monitor_register_task(h, "wifi", 6656, 23, 0); // WIFI task

	h = xTaskGetHandle("tiT");
	if (h) task_monitor_register_task(h, "tiT", 3584, 18, 0); // TCP-IP task
	
	h = xTaskGetHandle("sys_evt");
	if (h) task_monitor_register_task(h, "sys_evt", 3584, 20, 0); // system Event task
	
	h = xTaskGetHandle("httpd");
	if (h) task_monitor_register_task(h, "httpd", 8192, 20, 0); // system Event task
	
	
#endif

	// Создаем и регистрируем юзерские задачи
	sys_create_task(task_rx, "task_rx", 1024 * 3, NULL, 12, NULL, -1);
	sys_create_task(task_rx_parse, "task_rx_parse", 1024 * 6, NULL, 12, NULL, -1);
	sys_create_task(task_tx, "task_tx", 1024 * 3, NULL, 12, NULL, -1);
	sys_create_task(task_ui_redraw, "task_ui_redraw", 1024 * 6, NULL, 12, NULL, 1); // CPU1
	sys_create_task(task_wifi, "task_wifi", 1024 * 6, NULL, 12, NULL, 0); // CPU0

	ESP_LOGW("CREATE TASK", "Free heap size: %d", (int)xPortGetFreeHeapSize());
	ESP_LOGW("CREATE TASK", "Minimum Free heap size: %d", (int)xPortGetMinimumEverFreeHeapSize());
}

void eFil_init_queues(void)
{
	queue_rx_parse = xQueueCreate(10, COMM_MAX_LEN);
	queue_tx = xQueueCreate(10, COMM_MAX_LEN);
	queue_update = xQueueCreate(10, sizeof(update_t));
	qSend = xQueueCreate(10, sizeof(packet_t));
}

void eFil_init_tims()
{
	// tim_1sec = xTimerCreate("tim_1sec", 1000, pdTRUE, timID_1sec, cb_timers);
	tim_ssaver = xTimerCreate("tim_ssaver", TIMER_STBY_COUNT, pdTRUE, (void *const)timID_ssaver, cb_timers);
	tim_1sec = xTimerCreate("tim_1sec", TIMER_1SEC_COUNT, pdTRUE, (void *const)timID_1sec, cb_timers);
	tim_return = xTimerCreate("tim_return", TIMER_RETURN_COUNT, pdTRUE, (void *const)timID_return, cb_timers);
	tim_rec = xTimerCreate("tim_rec", TIMER_1SEC_COUNT, pdTRUE, (void *const)timID_return, cb_timers);
	tim_update_time_PCF = xTimerCreate("tim_update_time_PCF", TIMER_UPDATE_PCF_COUNT, pdTRUE, (void *const)timID_update_time_PCF, cb_timers);
	tim_sync_time_SNTP = xTimerCreate("tim_sync_time_SNTP", TIMER_SYNC_SNTP_COUNT, pdTRUE, (void *const)timID_sync_time_SNTP, cb_timers);
	tim_msg_box_live = xTimerCreate("tim_msg_box_live", TIMER_MSG_BOX_DEFAULT_LIVE_TIME, pdFALSE, (void *const)timID_msg_box_live, cb_timers);
	tim_nfc_rec = xTimerCreate("tim_msg_box_live", TIMER_NFC_REC_COUNT, pdTRUE, (void *const)timID_nfc_rec, cb_timers);
	// tim_heartbeat = xTimerCreate("tim_heartbeat", 3000, pdTRUE, (void *const)timID_heartbeat, cb_timers);
}
void eFil_init_events()
{
	ev_time_sync = xEventGroupCreate();
	ev_states	= xEventGroupCreate();
}

void eFil_init_mutex()
{
	i2c_mutex = xSemaphoreCreateMutex();
	if (i2c_mutex == NULL)
	{
		ESP_LOGE("MUTEX", "Failed to create I2C mutex");
		abort();
	}
}

void eFil_init_uart()
{
	static const char *TAG = "INIT";
	uart_config_t uart_config = {
		.baud_rate = 115200,
		// 460800
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	// Configure UART parameters
	ESP_ERROR_CHECK(uart_driver_install(uart_number, BUF_SIZE * 2, BUF_SIZE * 2, COMM_MAX_LEN, &queue_rx, 0));

	ESP_ERROR_CHECK(uart_param_config(uart_number, &uart_config));

	ESP_ERROR_CHECK(uart_set_pin(uart_number, U0_TXD_PIN, U0_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)); // Set UART pins(TX: IO4, RX: IO5, RTS: IO18, CTS: IO19)

	if (queue_rx)
	{
		ESP_LOGI(TAG, "Uart2_queue: %d ", 1);
	}

	// Set uart pattern detect function.
	uart_enable_pattern_det_baud_intr(EX_UART_NUM, COMM_PATTERN_CHAR, COMM_PATTERN_SIZE, 9, 0, 0);
	// Reset the pattern queue length to record at most 20 pattern positions.
	uart_pattern_queue_reset(EX_UART_NUM, 128);
}

void sys_create_task(TaskFunction_t task_func, const char* name, uint32_t stack_size, void* param, UBaseType_t priority, TaskHandle_t* out_handle, BaseType_t core_id)
{
	TaskHandle_t local_handle = NULL;
	BaseType_t pin_core = (core_id == 0 || core_id == 1) ? core_id : tskNO_AFFINITY;

	BaseType_t result = xTaskCreatePinnedToCore(task_func, name, stack_size, param, priority, &local_handle, pin_core);
	if (result == pdPASS)
	{
#if USE_TASK_MON == 1
		task_monitor_register_task(local_handle, name, stack_size, priority, pin_core);
#endif
		ESP_LOGI("TASK CREATE", "TASK %s CREATE Free heap size: %d", name, (int)xPortGetFreeHeapSize());
	}
	else {
		ESP_LOGE("TASK CREATE", "Failed to create task: %s", name);
		ESP_LOGE("TASK CREATE", "Free heap size: %d", (int)xPortGetFreeHeapSize());
	}
}
/// @brief Remove whitespaces
/// @param str
void sys_trim_whitespace(char *str)
{
	if (str == NULL || *str == '\0')
		return;

	char *start = str;
	char *end = str + strlen(str) - 1;

	while (isspace((unsigned char)*start)) // Пропускаем пробелы в начале
	{
		start++;
	}

	while (end > start && isspace((unsigned char)*end)) // Пропускаем пробелы в конце
	{
		end--;
	}
	if (start != str) // Переносим обрезанную строку в начало (если были пробелы слева)
	{
		memmove(str, start, end - start + 1);
	}
	str[end - start + 1] = '\0'; // Записываем новый null-терминатор
}

/// @brief Use "," as separator
/// @param str
/// @return
double sys_alt_atof(const char *str)
{
	int whole = 0, frac = 0, frac_digits = 0;
	int is_negative = 0;
	const char *p = str;

	// Обработка знака
	if (*p == '-')
	{
		is_negative = 1;
		p++;
	}
	else if (*p == '+')
	{
		p++;
	}

	// Целая часть
	while (*p >= '0' && *p <= '9')
	{
		whole = whole * 10 + (*p - '0');
		p++;
	}

	// Дробная часть (если есть разделитель , или .)
	if (*p == ',' || *p == '.')
	{
		p++;
		while (*p >= '0' && *p <= '9')
		{
			frac = frac * 10 + (*p - '0');
			frac_digits++;
			p++;
		}
	}

	double result = whole + (frac / pow(10, frac_digits));
	return is_negative ? -result : result;
}
lv_obj_t *sys_getLvglObjectFromIndex(int32_t index)
{
	if (index == -1)
	{
		return 0;
	}
	return ((lv_obj_t **)&objects)[index];
}
uint8_t eFil_init_spiffs()
{
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/storage",
		.max_files = 5,
		.partition_label = NULL,
		.format_if_mount_failed = true
	};

	esp_err_t result = esp_vfs_spiffs_register(&conf);

	if (result != ESP_OK)
	{
		ESP_LOGE(FS_TAG, "Failed to init SPIFFS (%s)", esp_err_to_name(result));
		return 0;
	}
	else
	{
		size_t tolal, used;
		result = esp_spiffs_info(conf.partition_label, &tolal, &used);

		if (result != ESP_OK)
		{
			ESP_LOGE(FS_TAG, "Failed to get SPIFFS info (%s)", esp_err_to_name(result));
			return 0;
		}
		else
		{
			ESP_LOGI(FS_TAG, "Partition size: total: %d, used: %d", tolal, used);
			return 1;
		}
	}
}
esp_err_t eFil_profiles_save() {
	FILE* file = fopen(FS_PROFILE, "w");
	if (file == NULL) {
		ESP_LOGE(TAG, "Failed to open file for writing: %s", FS_PROFILE);
		return ESP_FAIL;
	}

	// Записываем заголовок
	if (fprintf(file, "//#ID;#TYPE;#VENDOR;#INFO;#INFO2;#FULL_W;#SPOOL_W;#DENSITY;#DIAM\n") < 0) {
		ESP_LOGE(TAG, "Failed to write header");
		fclose(file);
		return ESP_FAIL;
	}
    
	// Записываем профили
	for (int i = 0; i < state.profiles_num; i++) {
		profile_t* p = &profiles[i];
        
		int written = fprintf(file,
			"%s;%s;%s;%s;%s;%d;%d;%.2f;%.2f\n",
			p->id,
			p->type,
			p->vendor,
			p->info,
			p->info2,
			p->full_w,
			p->spool_w,
			p->density,
			p->dia);
        
		if (written < 0) {
			ESP_LOGE(TAG, "Failed to write profile %d", i);
			fclose(file);
			return ESP_FAIL;
		}
	}

	// Синхронизируем с файловой системой
	fflush(file);
	fsync(fileno(file));
    
	fclose(file);
    
	ESP_LOGI(TAG, "Successfully saved %d profiles to %s", state.profiles_num, FS_PROFILE);
	return ESP_OK;
}

esp_err_t eFil_profiles_load()
{
	memset(profiles, 0x0, sizeof(profiles));

	FILE *file = fopen(FS_PROFILE, "r");
	if (file == NULL)
	{
		ESP_LOGE(FS_TAG, "Failed to open file: %s", FS_PROFILE);
		return ESP_FAIL;
	}

	char line[256];

	profile_t tmp;
	ESP_LOGW(FS_TAG, "Starting read profiles file: %s", FS_PROFILE);
	uint8_t i = 0;
	while (fgets(line, sizeof(line), file) != NULL)
	{
		memset(&tmp, 0x0, sizeof(tmp));
		line[strcspn(line, "\n")] = '\0'; // Удаляем символ новой строки
		line[strcspn(line, "\r")] = '\0'; // Удаляем символ возврата каретки
		if (strstr(line, "//") || !strlen(line))
			continue; // Пролпускаем заголовок файла (т.е. там есть //)

		char *sp;
		// ID
		sp = strtok(line, ";");
		sys_trim_whitespace(sp);
		if (strlen(sp) <= sizeof(tmp.id) && sp)
		{
			strcpy(tmp.id, sp);
		}
		else
		{
			continue;
		}
		// TYPE
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (strlen(sp) <= sizeof(tmp.type) && sp)
		{
			strcpy(tmp.type, sp);
		}
		else
		{
			continue;
		}
		// VENDOR
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (strlen(sp) <= sizeof(tmp.vendor) && sp)
		{
			strcpy(tmp.vendor, sp);
		}
		else
		{
			continue;
		}
		// INFO
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (strlen(sp) <= sizeof(tmp.info) && sp)
		{
			strcpy(tmp.info, sp);
		}
		else
		{
			continue;
		}
		// INFO2
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (strlen(sp) <= sizeof(tmp.info2) && sp)
		{
			strcpy(tmp.info2, sp);
		}
		else
		{
			continue;
		}
		// FULL_W
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (sp)
		{
			tmp.full_w = atoi(sp);
		}
		else
		{
			continue;
		}
		// SPOOL_W
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (sp)
		{
			tmp.spool_w = atoi(sp);
		}
		else
		{
			continue;
		}
		// DENCITY
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (sp)
		{
			tmp.density = sys_alt_atof(sp);
		}
		else
		{
			continue;
		}
		// DIA
		sp = strtok(NULL, ";");
		sys_trim_whitespace(sp);
		if (sp)
		{
			tmp.dia = sys_alt_atof(sp);
		}
		else
		{
			continue;
		}

		memcpy(&profiles[i], &tmp, sizeof(profile_t));
		i++;
	}
	state.profiles_num = i;
	fclose(file);
	return ESP_OK;
}
void eFil_ui_message_box(msg_box_t typ, const char *text, uint32_t live_time)
{
	char buf[64];
	lv_lock();
	msg_box = lv_msgbox_create(lv_screen_active());

	if (typ == MSG_BOX_OK)
	{
		add_style_style_msg_box_ok(msg_box);
		lv_snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s", text);
	}
	if (typ == MSG_BOX_ERR)
	{
		add_style_style_msg_box_err(msg_box);
		lv_snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", text);
	}

	lv_msgbox_add_text(msg_box, buf);
	lv_unlock();

	live_time ? xTimerChangePeriod(tim_msg_box_live, live_time, 0) : xTimerChangePeriod(tim_msg_box_live, TIMER_MSG_BOX_DEFAULT_LIVE_TIME, 0);

	xTimerStart(tim_msg_box_live, 0);
}
void task_wifi(void *pvParameters)
{
	update_t upd;
	vTaskDelay(pdMS_TO_TICKS(7000));
	ESP_LOGI("WIFI_TASK", "Starting WiFi...");
	wifi_init_sta(); // Ждем подключения к WiFi
	vTaskDelay(pdMS_TO_TICKS(1000));

	bool connected = wifi_is_connected();
	if (connected) {
		wifi_sntp_sync_time(); // Синхронизируем время по SNTP
		start_webserver(); // Запускаем веб-сервер
	}
	else {
		wifi.isConnected = false;
	}

	while (1)
	{
		// Мониторинг состояния Wi-Fi
		connected = wifi_is_connected();
		if (wifi.isConnected != connected) {
			wifi.isConnected = connected;
			upd = UI_UPD_WIFI_STATUS;
			xQueueSend(queue_update, &upd, portMAX_DELAY);
		}

		// Проверка флагов синхронизации времени
		EventBits_t bits = xEventGroupGetBits(ev_time_sync);
		if (bits & TIME_PCF_SYNC_BIT) {
			ESP_LOGI("WIFI_TASK", "Time sync by PCF");
			xEventGroupClearBits(ev_time_sync, TIME_PCF_SYNC_BIT);
			if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
			{
				time_get_from_PCF();
				xSemaphoreGive(i2c_mutex);

				upd = UI_UPD_TIME;
				xQueueSend(queue_update, &upd, 0);
				state.time_now = now;
			}
		}
		if (bits & TIME_SNTP_SYNC_BIT) {
			ESP_LOGI("WIFI_TASK", "Time sync by SNTP");
			xEventGroupClearBits(ev_time_sync, TIME_SNTP_SYNC_BIT);
			wifi_sntp_sync_time();
			upd = UI_UPD_TIME;
			xQueueSend(queue_update, &upd, 0);
		}

//	uint32_t free_stack = uxTaskGetStackHighWaterMark(NULL);
//  ESP_LOGI("WIFI TASK", "Free stack: %u bytes", uxTaskGetStackHighWaterMark(NULL) );

		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}
void task_tx(void *pvParameters)
{
	packet_t data;

	for (;;)
	{
		if (xQueueReceive(qSend, &data, portMAX_DELAY) == pdTRUE)
		{
			uart_write_bytes(UART_NUM_2, (const char*)&data, sizeof(packet_t));
			ESP_LOGE("TX", "Packet send");
		}
	}
}

void task_rx(void *pvParameters)
{
	const char *TAG = "RX_TASK";
	uart_event_t event;
	size_t buffered_size;
	uint8_t *dtmp = (uint8_t *)malloc(READ_BUF_SIZE);
	static uint32_t counter = 0;
	// int length = 0;

	for (;;)
	{
		// Waiting for UART event.
		if (xQueueReceive(queue_rx, (void *)&event, (TickType_t)portMAX_DELAY))
		{
			bzero(dtmp, READ_BUF_SIZE);
			//	ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);
			switch (event.type)
			{
			case UART_DATA:

				break;

			case UART_FIFO_OVF: // Event of HW FIFO overflow detected
				ESP_LOGI(TAG, "hw fifo overflow");
				uart_flush_input(EX_UART_NUM);
				xQueueReset(queue_rx);
				break;

			case UART_BUFFER_FULL: // Event of UART ring buffer full
				ESP_LOGI(TAG, "ring buffer full");
				uart_flush_input(EX_UART_NUM);
				xQueueReset(queue_rx);
				break;
				// Event of UART RX break detected
			case UART_BREAK:
				ESP_LOGI(TAG, "uart rx break");
				break;
				// Event of UART parity check error
			case UART_PARITY_ERR:
				ESP_LOGE(TAG, "uart parity error");
				break;
				// Event of UART frame error
			case UART_FRAME_ERR:
				ESP_LOGE(TAG, "uart frame error");
				break;
				// UART_PATTERN_DET
			case UART_PATTERN_DET:

				uint32_t timeout = 100; // Макс. 100 мс
				while (buffered_size < sizeof(packet_t) && timeout--)
				{
					vTaskDelay(1 / portTICK_PERIOD_MS);
					uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
				}
				// uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);

				int pos = uart_pattern_pop_pos(EX_UART_NUM);
				counter++;
				// ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
				if (pos == -1 || buffered_size != sizeof(packet_t)) // проверяем если принятое не соответствует размеру структуры пакета
				{
					// There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
					// record the position. We should set a larger queue size.
					// As an example, we directly flush the rx buffer here.
					ESP_LOGE(TAG, "Wrong packet size: %d, must be: %d ", (int)buffered_size, (int)(sizeof(packet_t)));
					uart_read_bytes(EX_UART_NUM, dtmp, buffered_size, 100 / portTICK_PERIOD_MS); // Читаем пакет без CRC
					ESP_LOG_BUFFER_HEXDUMP("PARSE", dtmp, buffered_size, ESP_LOG_INFO);

					uart_flush_input(EX_UART_NUM);
				}
				else
				{
					uart_read_bytes(EX_UART_NUM, dtmp, buffered_size, 100 / portTICK_PERIOD_MS); // Читаем пакет без CRC
					//ESP_LOG_BUFFER_HEXDUMP("PARSE", dtmp ,buffered_size,ESP_LOG_INFO);
					xQueueSend(queue_rx_parse, (void *)dtmp, (TickType_t)0);
					// ESP_LOGI(TAG, "read: %d bytes", buffered_size);
					// ESP_LOGI(TAG, "Counter: %d ", (int) counter);
					uart_flush_input(EX_UART_NUM);
				}
				break;
				// Others
			default:
				//			ESP_LOGI(TAG, "uart event type: %d", event.type);
				break;
			}
		}
		// vTaskDelay(20);
	}
	free(dtmp);
	dtmp = NULL;
	vTaskDelete(NULL);
}

void task_rx_parse(void *pvParameters)
{
	uint8_t rx_data[COMM_MAX_LEN];
	packet_t data;
	update_t upd;
	for (;;)
	{
		if (xQueueReceive(queue_rx_parse, (void *)&rx_data, (TickType_t)portMAX_DELAY))
		{
			memcpy(&data, &rx_data, sizeof(packet_t));
			if (data.pkt_type == COMM_TYPE_DATA) {
				//	ESP_LOGW("PARSE","Packet data arrive!");
				switch (data.data_type)
				{
				case COMM_DATA_SENS_WGT:

					ESP_LOGI("RX_PARSE", "Weight: %d gr.", (int)data.data1);
					if (state.activ_screen != objects.main) {
						xTimerReset(tim_ssaver, portMAX_DELAY);
						if (state.activ_screen == sys_getLvglObjectFromIndex(SCREEN_ID_SSAVER - 1))
						{
							upd = UI_UPD_LOAD_SCR_MAIN;
							xQueueSend(queue_update, &upd, portMAX_DELAY);
						}
					}
					else {
						xTimerReset(tim_ssaver, portMAX_DELAY);
					}
					state.abs_wgt = data.data1;
					eFil_profile_calculate(state.profile_active);
					touch_count = 0;

					break;
				case COMM_DATA_SENS_RFID:

					if (state.activ_screen == sys_getLvglObjectFromIndex(SCREEN_ID_SSAVER - 1))
					{
						upd = UI_UPD_LOAD_SCR_MAIN;
						xEventGroupClearBits(ev_states, STATE_BIT_PROFILES | STATE_BIT_SS);
						bits_state = xEventGroupGetBits(ev_states);
						xQueueSend(queue_update, &upd, portMAX_DELAY);
					}

					ESP_LOGI("RX_PARSE", "NFC read. ID:  %s ", data.data3);
					if (!strlen(data.data3))
					{
						eFil_ui_message_box(MSG_BOX_ERR, "NFC tag is EMPTY!", 0);
						break;
					}

					char buf[64];
					if (eFil_profile_found(data.data3) != -1) {
						lv_snprintf(buf, sizeof(buf), "Profile [%s] is set!", data.data3);
						eFil_ui_message_box(MSG_BOX_OK, buf, 0);
						eFil_profile_set_active(data.data3);
					}
					else
					{
						lv_snprintf(buf, sizeof(buf), "Profile %s NOT found", data.data3);
						eFil_ui_message_box(MSG_BOX_ERR, buf, 0);
					}

					break;

				default:
					break;
				}
			}
			if (data.pkt_type == COMM_TYPE_INFO) {
				ESP_LOGW("PARSE", "Packet INFO arrive!");
				switch (data.data_type)
				{
				case COMM_INFO_RFID_REC_OK:
					eFil_rec_stop(NFC_OK);
					xEventGroupClearBits(ev_states, STATE_BIT_REC);
					bits_state = xEventGroupGetBits(ev_states);
					break;
				case COMM_INFO_RFID_REC_ERR:
					eFil_rec_stop(NFC_ERROR);
					xEventGroupClearBits(ev_states, STATE_BIT_REC);
					bits_state = xEventGroupGetBits(ev_states);
					break;
				case COMM_INFO_ADC_OFFSET_CALIB_OK:
					state.ADC_offset = data.data1;
					config_save();
					xEventGroupClearBits(ev_states, STATE_BIT_CALIB_OFFSET);
					bits_state = xEventGroupGetBits(ev_states);
					upd = UI_UPD_CALIB_STOP;
					xQueueSend(queue_update, &upd, 0);
					break;
				case COMM_INFO_ADC_FULLSCALE_CALIB_OK:
					state.ADC_fullscale = data.data1;
					config_save();
					xEventGroupClearBits(ev_states, STATE_BIT_CALIB_FULLSCALE);
					bits_state = xEventGroupGetBits(ev_states);
					upd = UI_UPD_CALIB_STOP;
					xQueueSend(queue_update, &upd, 0);

					break;
				case COMM_INFO_ADC_CALIB_PRC:

					state.ADC_calib_prc = data.data1;
					upd = UI_UPD_CALIB_PROGRESS;
					xQueueSend(queue_update, &upd, 0);

					break;

				default:
					break;
				}
			}
		}
	}

	vTaskDelay(50);
}

void eFil_init_ui()
{
	lv_lock();
#if DEBUG_TOUCH == 1
	my_Cir = lv_obj_create(lv_scr_act());
	lv_obj_set_scrollbar_mode(my_Cir, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_size(my_Cir, 42, 42);
	lv_obj_set_pos(my_Cir, 466 / 2, 466 / 2);
	lv_obj_set_style_bg_color(my_Cir, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
	lv_obj_set_style_radius(my_Cir, LV_RADIUS_CIRCLE, 0);
#endif
	lv_label_set_recolor(objects.txt_filament_type, true);
	lv_label_set_recolor(objects.txt_info_lgt, true);
	lv_label_set_recolor(objects.txt_info_total, true);
	lv_label_set_recolor(objects.txt_info_wgt, true);
	lv_label_set_recolor(objects.txt_btn_set, true);
	lv_label_set_recolor(objects.txt_btn_rec, true);

	lv_label_set_text(objects.txt_wifi, LV_SYMBOL_WIFI);
	lv_obj_set_style_text_color(objects.txt_wifi, lv_color_hex(COLOR_WIFI_DIS), LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_label_set_text(objects.txt_btn_set, LV_SYMBOL_OK " SET PROFILE");
	lv_label_set_text(objects.txt_btn_rec, LV_SYMBOL_SAVE " REC NFC");

	lv_unlock();
}
void eFil_ui_roller_profiles_set(const char* filter_type, const char* filter_vendor)
{
	if (!state.profiles_num) {
		lv_roller_set_options(objects.roll_profile, "No profiles", LV_ROLLER_MODE_NORMAL);
		return;
	}

	options_buf[0] = '\0';
	char* current_pos = options_buf;
	size_t remaining = sizeof(options_buf);
	int option_count = 0;

	for (int i = 0; i < state.profiles_num; i++) {
		// Применяем фильтры (с проверкой на NULL)
		if (filter_type != NULL && strcmp(profiles[i].type, filter_type) != 0) continue;
		if (filter_vendor != NULL && strcmp(profiles[i].vendor, filter_vendor) != 0) continue;

		// Формируем строку опции
		const char* format = (state.profile_active != NULL && 
		                     strcmp(profiles[i].id, state.profile_active->id) == 0) 
						   ? "\xEF\x81\x8B %s (%s) %s %s\n" 
						   : "%s (%s) %s %s\n";
        
		int len = snprintf(current_pos,
			remaining,
			format,
			profiles[i].type,
			profiles[i].info,
			profiles[i].vendor,
			profiles[i].info2);
        
		if (len < 0 || (size_t)len >= remaining) {
			// Буфер переполнен
			ESP_LOGW(TAG, "Roller options buffer overflow");
			break;
		}
	
		current_pos += len;
		remaining -= len;
		option_count++;
	}

	if (option_count == 0) {
		lv_roller_set_options(objects.roll_profile, "No matching profiles", LV_ROLLER_MODE_NORMAL);
	}
	else {
		// Убираем последний символ \n
		if (current_pos > options_buf) {
			*(current_pos - 1) = '\0';
		}
		lv_roller_set_options(objects.roll_profile, options_buf, LV_ROLLER_MODE_NORMAL);
		
		lv_lock();
		lv_roller_set_options(objects.roll_profile, options_buf, LV_ROLLER_MODE_NORMAL);
		lv_roller_set_selected(objects.roll_profile, 0, LV_ANIM_OFF); // сброс выбора
		lv_roller_set_visible_row_count(objects.roll_profile, 3);
		lv_unlock();
	}

	
}

void task_ui_redraw(void *pvParameters)
{
	update_t upd;
	uint8_t prc, prc_fl;
	time_t now;

	for (;;)
	{
		/* code */
		xQueueReceive(queue_update, &upd, portMAX_DELAY);
		lv_lock();

		switch (upd)
		{
		case UI_UPD_PROFILE:

			if (state.cur_wgt == -1)
			{
				lv_label_set_text(objects.txt_prc_dec, "--");
				lv_obj_set_style_text_color(objects.txt_prc_dec, lv_color_hex(COLOR_TXT_DEFAULT), LV_PART_MAIN | LV_STATE_DEFAULT);
				lv_label_set_text(objects.txt_prc_fl, "-");
				lv_obj_set_style_text_color(objects.txt_prc_fl, lv_color_hex(COLOR_TXT_DEFAULT), LV_PART_MAIN | LV_STATE_DEFAULT);

				lv_arc_set_value(objects.arc_main, 0);

				lv_label_set_text(objects.txt_info_lgt, "- m");
				lv_label_set_text(objects.txt_info_wgt, " - g");
				if (state.opt_always_show_total)
				{
					lv_label_set_text_fmt(objects.txt_info_total, "%d g", state.cur_total);
				}
				else
				{
					lv_label_set_text(objects.txt_info_total, "- g");
				}

				lv_obj_add_flag(objects.txt_ss_prc_dec, LV_OBJ_FLAG_HIDDEN);
				lv_obj_add_flag(objects.txt_ss_prc_fl, LV_OBJ_FLAG_HIDDEN);
				lv_obj_add_flag(objects.txt_ss_prc_label, LV_OBJ_FLAG_HIDDEN);

				break;
			}
			else
			{
				prc = state.curr_prc / 10;
				prc_fl = state.curr_prc % 10;
				if (prc >= 100)
				{
					prc = 99;
					prc_fl = 9;
				}

				lv_label_set_text_fmt(objects.txt_prc_dec, "%u", prc);
				lv_label_set_text_fmt(objects.txt_prc_fl, "%u", prc_fl);

				lv_arc_set_value(objects.arc_main, state.curr_prc / 10);

				lv_label_set_text_fmt(objects.txt_info_lgt, "%d m", state.cur_lgt);
				lv_label_set_text_fmt(objects.txt_info_wgt, "%d g", state.cur_wgt);

				lv_label_set_text_fmt(objects.txt_info_total, "%d g", state.cur_total);

				lv_obj_remove_flag(objects.txt_ss_prc_dec, LV_OBJ_FLAG_HIDDEN);
				lv_obj_remove_flag(objects.txt_ss_prc_fl, LV_OBJ_FLAG_HIDDEN);
				lv_obj_remove_flag(objects.txt_ss_prc_label, LV_OBJ_FLAG_HIDDEN);

				lv_label_set_text_fmt(objects.txt_ss_prc_dec, "%u", prc);
				lv_label_set_text_fmt(objects.txt_ss_prc_fl, "%u", prc_fl);
			}
			switch (prc)
			{
			case 50 ... 100:
				lv_obj_set_style_arc_color(objects.arc_main, lv_color_hex(COLOR_ARC_100_50), LV_PART_INDICATOR | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_dec, lv_color_hex(COLOR_TXT_100_50), LV_PART_MAIN | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_fl, lv_color_hex(COLOR_TXT_100_50), LV_PART_MAIN | LV_STATE_DEFAULT);

				break;
			case 25 ... 49:
				lv_obj_set_style_arc_color(objects.arc_main, lv_color_hex(COLOR_ARC_49_25), LV_PART_INDICATOR | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_dec, lv_color_hex(COLOR_TXT_49_25), LV_PART_MAIN | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_fl, lv_color_hex(COLOR_TXT_49_25), LV_PART_MAIN | LV_STATE_DEFAULT);

				break;
			case 10 ... 24:
				lv_obj_set_style_arc_color(objects.arc_main, lv_color_hex(COLOR_ARC_24_10), LV_PART_INDICATOR | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_dec, lv_color_hex(COLOR_TXT_24_10), LV_PART_MAIN | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_fl, lv_color_hex(COLOR_TXT_24_10), LV_PART_MAIN | LV_STATE_DEFAULT);
				break;
			case 0 ... 9:
				lv_obj_set_style_arc_color(objects.arc_main, lv_color_hex(COLOR_ARC_9_0), LV_PART_INDICATOR | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_dec, lv_color_hex(COLOR_TXT_9_0), LV_PART_MAIN | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_prc_fl, lv_color_hex(COLOR_TXT_9_0), LV_PART_MAIN | LV_STATE_DEFAULT);
				break;
			default:
				break;
			}

			break;
		case UI_UPD_PROFILE_HEADER:

			char txt[50] = { 0 };

			strcat(txt, state.profile_active->type);
			strcat(txt, " (");
			strcat(txt, COLOR_PROF_ALT_TXT);
			strcat(txt, state.profile_active->info);
			strcat(txt, "#)");

			//char buf[64];
			//memset(buf,0x0,sizeof(buf));
			//lv_snprintf(buf, sizeof(buf), "%s %s", text);
			lv_label_set_text(objects.txt_filament_type, txt);

			if (strlen(state.profile_active->vendor) + strlen(state.profile_active->info2) > 19)
			{
				lv_obj_set_style_text_font(objects.txt_filament_vendor_pack, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
			}
			else {
				lv_obj_set_style_text_font(objects.txt_filament_vendor_pack, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
			}
			lv_label_set_text_fmt(objects.txt_filament_vendor_pack, "%s %s", state.profile_active->vendor, state.profile_active->info2);
			break;

		case UI_UPD_LOAD_SCR_MAIN:
			loadScreen(SCREEN_ID_MAIN);
			state.activ_screen = sys_getLvglObjectFromIndex(SCREEN_ID_MAIN - 1);
			xEventGroupClearBits(ev_states, STATE_BIT_PROFILES | STATE_BIT_SS);
			bits_state = xEventGroupGetBits(ev_states);

			amoled_set_brightness(AMOLED_MAIN_BRIGHTLESS);

			break;

		case UI_UPD_LOAD_SCR_PROFILES:
			loadScreen(SCREEN_ID_PROFILE_SEL);
			state.activ_screen = sys_getLvglObjectFromIndex(SCREEN_ID_PROFILE_SEL - 1);
			break;
		case UI_UPD_LOAD_SCR_SSAVER:

			loadScreen(SCREEN_ID_SSAVER);
			state.activ_screen = sys_getLvglObjectFromIndex(SCREEN_ID_SSAVER - 1);
			amoled_fade(AMOLED_MAIN_BRIGHTLESS, AMOLED_SS_BRIGHTLESS, 7);

			break;
		case UI_UPD_WIFI_STATUS:

			if (wifi.isConnected)
				lv_obj_set_style_text_color(objects.txt_wifi, lv_color_hex(COLOR_WIFI_EN), LV_PART_MAIN | LV_STATE_DEFAULT);
			else
				lv_obj_set_style_text_color(objects.txt_wifi, lv_color_hex(COLOR_WIFI_DIS), LV_PART_MAIN | LV_STATE_DEFAULT);

			break;
		case UI_UPD_TIME:

			now = time(NULL);
			struct tm timeinfo;
			time(&now);
			localtime_r(&now, &timeinfo);
			lv_label_set_text_fmt(objects.txt_time_h, "%02u", timeinfo.tm_hour);
			lv_label_set_text_fmt(objects.txt_time_m, "%02u", timeinfo.tm_min);

			lv_label_set_text_fmt(objects.txt_ss_h, "%02u", timeinfo.tm_hour);
			lv_label_set_text_fmt(objects.txt_ss_m, "%02u", timeinfo.tm_min);

			lv_label_set_text_fmt(objects.txt_ss_date, "%02u.%02u.%u", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + TM_YEAR_BASE);
			break;

		case UI_UPD_TIME_SEP:
			now = time(NULL);
			if (now % 2 == 0) {
				lv_obj_set_style_text_color(objects.txt_time_sep, lv_color_hex(0xfafafa), LV_PART_MAIN | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_ss_sep, lv_color_hex(0xfafafa), LV_PART_MAIN | LV_STATE_DEFAULT);
			}
			else {
				lv_obj_set_style_text_color(objects.txt_time_sep, lv_color_hex(0x0), LV_PART_MAIN | LV_STATE_DEFAULT);
				lv_obj_set_style_text_color(objects.txt_ss_sep, lv_color_hex(0x0), LV_PART_MAIN | LV_STATE_DEFAULT);
			}

			break;

		case UI_UPD_NFC_REC_STATUS:

			if (state.nfc_rec_count) {
				lv_arc_set_value(objects.arc_rec, state.nfc_rec_count);
			}
			else {
				eFil_rec_stop(NFC_TIMEOUT);
			}

			break;
		case UI_UPD_NFC_REC_START:

			lv_arc_set_value(objects.arc_rec, NFC_REC_WAIT_TIME);
			lv_obj_remove_flag(objects.arc_rec, LV_OBJ_FLAG_HIDDEN);
			lv_obj_remove_flag(objects.cont_set_rec, LV_OBJ_FLAG_SCROLLABLE);
			break;

		case UI_UPD_NFC_REC_STOP:

			lv_arc_set_value(objects.arc_rec, 0);
			lv_obj_add_flag(objects.arc_rec, LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_flag(objects.cont_set_rec, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_scroll_by(objects.cont_set_rec, 400, 0, LV_ANIM_OFF);

			switch (NFC_result)
			{
			case NFC_OK:
				eFil_ui_message_box(MSG_BOX_OK, "NFC tag saved!", 0);
				break;
			case NFC_ERROR:
				eFil_ui_message_box(MSG_BOX_ERR, "NFC save error!", 0);
				break;
			case NFC_ABORTED:
				eFil_ui_message_box(MSG_BOX_ERR, "NFC abortrd!", 0);
				break;
			case NFC_TIMEOUT:
				eFil_ui_message_box(MSG_BOX_ERR, "NFC timeout!", 0);
				break;

			default:

				break;
			}
			NFC_result = NFC_NONE;
			break;
		case UI_UPD_CALIB_START:

			lv_arc_set_value(objects.arc_calibrate, 0);
			lv_label_set_text_fmt(objects.txt_calib_prc, "%d%%", 0);

			if (bits_state & STATE_BIT_CALIB_OFFSET) {
				lv_label_set_text(objects.txt_calib_target, "ZERO  OFFSET");
				lv_obj_add_flag(objects.txt_calib_ref_wgt, LV_OBJ_FLAG_HIDDEN);
				lv_textarea_set_text(objects.area_txt_calib_info, "Please, remove the filament spool and do not touch the holder until calibration is complete!");
			}
			if (bits_state & STATE_BIT_CALIB_FULLSCALE) {
				lv_label_set_text(objects.txt_calib_target, "FULLSCALE");
				lv_label_set_text_fmt(objects.txt_calib_ref_wgt, "Reference weight: %d g", (int) state.ADC_ref_weight);
				lv_obj_remove_flag(objects.txt_calib_ref_wgt, LV_OBJ_FLAG_HIDDEN);
				lv_textarea_set_text(objects.area_txt_calib_info, "Please, set filament spool and do not touch the holder until calibration is complete!");
			}

			loadScreen(SCREEN_ID_CALIBRATE);
			state.activ_screen = sys_getLvglObjectFromIndex(SCREEN_ID_CALIBRATE - 1);
			xEventGroupClearBits(ev_states, STATE_BIT_PROFILES | STATE_BIT_SS);
			bits_state = xEventGroupGetBits(ev_states);
			break;
		case UI_UPD_CALIB_STOP:
			loadScreen(SCREEN_ID_MAIN);
			state.activ_screen = sys_getLvglObjectFromIndex(SCREEN_ID_MAIN - 1);
			xEventGroupClearBits(ev_states, STATE_BIT_PROFILES | STATE_BIT_SS);
			bits_state = xEventGroupGetBits(ev_states);
			state.ADC_calib_prc = 0;
			eFil_ui_message_box(MSG_BOX_OK, "Calibrate complete!", 0);

			break;
		case UI_UPD_CALIB_PROGRESS:

			lv_arc_set_value(objects.arc_calibrate, state.ADC_calib_prc);
			lv_label_set_text_fmt(objects.txt_calib_prc, "%d%%", state.ADC_calib_prc);

			break;
		default:
			break;
		}
		lv_unlock();
		// uint32_t free_stack = uxTaskGetStackHighWaterMark(NULL);
		// printf("Free stack: %u bytes\n", (int)free_stack);
 //       ESP_LOGI("REDRAW TASK", "Free stack: %u bytes", uxTaskGetStackHighWaterMark(NULL) );
 //       lv_task_handler();

		vTaskDelay(50);
	}
}
int eFil_profile_found(const char *id)
{
	for (size_t i = 0; i < state.profiles_num; i++)
	{
		if (strcmp(profiles[i].id, id) == 0)
		{
			return i;
		}
	}

	return -1;
}
profile_t* eFil_profile_found_handle(const char *id)
{
	for (size_t i = 0; i < state.profiles_num; i++)
	{
		if (strcmp(profiles[i].id, id) == 0)
		{
			return &profiles[i];
		}
	}
	return NULL;
}
uint8_t eFil_profile_set_active(const char *id)
{
	update_t upd;
	for (size_t i = 0; i < state.profiles_num; i++)
	{
		if (strcmp(profiles[i].id, id) == 0)
		{
			state.profile_active = &profiles[i];
			strncpy(state.profile_active_id, profiles[i].id, sizeof(state.profile_active_id) - 1);
			upd = UI_UPD_PROFILE_HEADER;
			xQueueSend(queue_update, &upd, portMAX_DELAY);
			eFil_profile_calculate(state.profile_active);
			eFil_ui_roller_profiles_set(NULL, NULL);
			ESP_LOGI("PROFILE", "Active profile set: %s (%s) %s %s [ID: %s (%d)]", profiles[i].type, profiles[i].info, profiles[i].vendor, profiles[i].info2, profiles[i].id, (int)i);
			return 1;
		}
	}
	if (state.profiles_num) {
		state.profile_active = &profiles[0];
		strncpy(state.profile_active_id, profiles[0].id, sizeof(state.profile_active_id) - 1);
		upd = UI_UPD_PROFILE_HEADER;
		xQueueSend(queue_update, &upd, portMAX_DELAY);
		ESP_LOGW("PROFILE", "Profile [%s] not found, active profile set: %s (%s) %s %s [ID: %s (%d)]", id, profiles[0].type, profiles[0].info, profiles[0].vendor, profiles[0].info2, profiles[0].id, 0);
		return 1;
	}
	ESP_LOGE("PROFILE", "PROFILES EMPTY!!!");
	return 0;
}
uint8_t eFil_profile_set_zero(){
	
	
	profile_t zero_profile = {"0000","NO PROFILE","-","-","-",0,0,0.0f,0.0f};
	profiles[0] = zero_profile;
	state.profiles_num = 1;
	eFil_profile_set_active("0000");
return 1;		
		
}
void eFil_profile_calculate(profile_t *profile)
{
	update_t upd;

	if (strcmp(profile->id, "0000") == 0) {
		state.cur_total = -1;
		state.cur_wgt = -1;
		state.curr_prc = -1;
		state.cur_lgt = -1;
		upd = UI_UPD_PROFILE;
		xQueueSend(queue_update, &upd, portMAX_DELAY);
		return;
		
	}
	
	if ((state.abs_wgt >= WGT_MIN) && (state.abs_wgt >= state.profile_active->spool_w) && profile->density > 0 && profile->dia > 0)
	{
		state.cur_total = state.abs_wgt;
		state.cur_wgt = state.cur_total - profile->spool_w;
		state.curr_prc = (uint32_t)((float)state.cur_wgt / (float)profile->full_w * 1000.0f);
		state.cur_lgt = (uint32_t)(state.cur_wgt / (profile->density * M_PI * (powf(profile->dia, 2) / 4)));
	}
	else
	{
		state.cur_total = state.opt_always_show_total ? state.abs_wgt : 0;
		state.cur_wgt = -1;
		state.curr_prc = -1;
		state.cur_lgt = -1;
	}
	upd = UI_UPD_PROFILE;
	xQueueSend(queue_update, &upd, portMAX_DELAY);
}
void eFil_profile_update(const char* old_id, profile_t new_profile) {
	
	int id_num = eFil_profile_found(old_id);
	if (id_num != -1) {
	memset(&profiles[id_num],0,sizeof(profile_t));
	profiles[id_num].density = new_profile.density;
	profiles[id_num].dia = new_profile.dia;
	profiles[id_num].full_w = new_profile.full_w;
	strncpy(profiles[id_num].id,new_profile.id,sizeof(new_profile.id));
	strncpy(profiles[id_num].info, new_profile.info, sizeof(new_profile.info));
	strncpy(profiles[id_num].info2, new_profile.info2, sizeof(new_profile.info2));
	profiles[id_num].spool_w = new_profile.spool_w;
	strncpy(profiles[id_num].type, new_profile.type, sizeof(new_profile.type));
	strncpy(profiles[id_num].vendor, new_profile.vendor, sizeof(new_profile.vendor));
	}
	if (strcmp(state.profile_active_id, old_id) == 0) {
		eFil_profile_set_active(new_profile.id);
	}
	eFil_ui_roller_profiles_set(NULL, NULL);
	eFil_profiles_save();
	
}
uint8_t eFil_profile_add(profile_t new_profile) {
	
	
	
	int id_num = eFil_profile_found(new_profile.id);
	
	if (id_num == -1) {
		uint8_t new_num = state.profiles_num;
		if (new_num > PROFILES_MAX_COUNT) {return 1;}
		
		memset(&profiles[new_num], 0, sizeof(profile_t));
		profiles[new_num].density = new_profile.density;
		profiles[new_num].dia = new_profile.dia;
		profiles[new_num].full_w = new_profile.full_w;
		strncpy(profiles[new_num].id, new_profile.id, sizeof(new_profile.id));
		strncpy(profiles[new_num].info, new_profile.info, sizeof(new_profile.info));
		strncpy(profiles[new_num].info2, new_profile.info2, sizeof(new_profile.info2));
		profiles[new_num].spool_w = new_profile.spool_w;
		strncpy(profiles[new_num].type, new_profile.type, sizeof(new_profile.type));
		strncpy(profiles[new_num].vendor, new_profile.vendor, sizeof(new_profile.vendor));
		
		state.profiles_num++;
	}
	else {
		return 2;
	}
	if (eFil_profile_found("0000") != -1) {
		eFil_profile_delete("0000", false);
	} // удаляем нулевой профайл если он есть
	eFil_ui_roller_profiles_set(NULL, NULL);
	eFil_profiles_save();
	return 0;
}
uint8_t eFil_profile_delete(const char* id, bool save) {
	
	uint8_t del_isActive = 0;
	uint8_t index = eFil_profile_found(id);
	
	if (index < 0 || index >= state.profiles_num) {
		return 1;		// профайл не найден
	}

	// Проверяем, не пытаемся ли удалить активный профиль
	if (state.profile_active == &profiles[index]) {
		ESP_LOGW(TAG, "Attempt to delete active profile '%s'", profiles[index].id);
     	del_isActive = 1;
	}

	// Сохраняем ID для логов
	char deleted_id[5];
	strncpy(deleted_id, profiles[index].id, sizeof(deleted_id));

	// Сдвигаем элементы
	for (int i = index; i < state.profiles_num - 1; i++) {
		profiles[i] = profiles[i + 1];
	}

	state.profiles_num--;
	memset(&profiles[state.profiles_num], 0, sizeof(profile_t));
	
	if (del_isActive && state.profiles_num) (eFil_profile_set_active(profiles[0].id));
	
	if (!state.profiles_num) {
		
		eFil_profile_set_zero();	// ставим "нулевой" пустой профиль и делаем его активным
	}
	
	eFil_ui_roller_profiles_set(NULL,NULL);
	
	ESP_LOGI(TAG, "Profile '%s' deleted. Total: %d", deleted_id, state.profiles_num);
	if (save) {
	eFil_profiles_save();
		}
	return 0;
}
void eFil_rec_start(const char *id)
{
	update_t upd;

	bits_state = xEventGroupSetBits(ev_states, STATE_BIT_REC);

	eFil_make_packet(COMM_TYPE_COMMAND, COMM_COMMAND_RFID_REC, 0, 0, id, true);

	xTimerStop(tim_return, 100);
	xTimerStart(tim_nfc_rec, 100);
	state.nfc_rec_count = NFC_REC_WAIT_TIME;

	upd = UI_UPD_NFC_REC_START;
	xQueueSend(queue_update, &upd, portMAX_DELAY);
}
void eFil_rec_stop(NFC_result_t reason)
{
	update_t upd;

	xTimerStop(tim_nfc_rec, 100);
	xTimerStart(tim_return, 100);
	state.nfc_rec_count = 0;

	switch (reason)
	{
	case NFC_OK:

		break;
	case NFC_ERROR:
		break;
	case NFC_ABORTED:
		eFil_make_packet(COMM_TYPE_COMMAND, COMM_COMMAND_RFID_REC_ABORT, 0, 0, NULL, true);
		break;
	case NFC_TIMEOUT:
		eFil_make_packet(COMM_TYPE_COMMAND, COMM_COMMAND_RFID_REC_ABORT, 0, 0, NULL, true);
		break;
	default:
		break;
	}

	NFC_result = reason;
	upd = UI_UPD_NFC_REC_STOP;
	xQueueSend(queue_update, &upd, portMAX_DELAY);
}
void action_touch_press(lv_event_t *e)
{
	lv_obj_t *target = lv_event_get_target(e);
	lv_event_code_t event_code = lv_event_get_code(e);
	//  lv_point_t p;
	update_t upd;

#if DEBUG_TOUCH == 1
	if (target == objects.panel_touch)
	{
		switch (event_code)
		{
		case LV_EVENT_PRESSING:

			lv_indev_t *indev = lv_indev_get_act();
			lv_indev_get_point(indev, &p);

			lv_lock();
			lv_label_set_text_fmt(objects.txt_coord, "X:%d Y:%d", (int)p.x, (int)p.y);
			lv_obj_set_pos(my_Cir, (int)p.x, (int)p.y);
			lv_unlock();

			break;
		default:
			break;
		}
	}

	if (target == objects.cnt_test)
	{
		switch (event_code)
		{
		case LV_EVENT_SHORT_CLICKED:
			//	lv_lock();
			lv_indev_t *indev = lv_indev_get_act();
			lv_indev_get_point(indev, &p);

			lv_lock();
			lv_label_set_text_fmt(objects.txt_coord2, "X:%d Y:%d", (int)p.x, (int)p.y);
			lv_obj_set_pos(my_Cir, (int)p.x, (int)p.y);
			lv_unlock();

			break;
		default:
			break;
		}
	}
#endif

	if (target == objects.cont_profile) // нажата область Профайла
	{
		switch (event_code)
		{
		case LV_EVENT_CLICKED:
			xTimerStart(tim_return, portMAX_DELAY);
			upd = UI_UPD_LOAD_SCR_PROFILES;
			xQueueSend(queue_update, &upd, portMAX_DELAY);
			break;
		default:
			break;
		}
	}

	if (target == objects.btn_prof_set) // нажата кнопка SET
	{
		switch (event_code)
		{
		case LV_EVENT_CLICKED:

			upd = UI_UPD_LOAD_SCR_MAIN;
			xQueueSend(queue_update, &upd, portMAX_DELAY);

			uint32_t prof_id_num = lv_roller_get_selected(objects.roll_profile);
			if (prof_id_num <= state.profiles_num)
			{
				eFil_profile_set_active(profiles[prof_id_num].id);
				memset(&state.profile_active_id, 0x0, sizeof(state.profile_active_id));
				strncpy(state.profile_active_id, profiles[prof_id_num].id, sizeof(state.profile_active_id) - 1);
				//  state.profile_active_id[sizeof(state.profile_active_id) - 1] = '\0'; // Гарантируем null-terminated
			}

			break;
		default:
			break;
		}
	}
	if (target == objects.btn_prof_rec)
	{
		switch (event_code)
		{
		case LV_EVENT_CLICKED:

			if (state.nfc_rec_count) {
				eFil_rec_stop(NFC_ABORTED);
			}
			else {
				eFil_rec_start(profiles[lv_roller_get_selected(objects.roll_profile)].id);
			}
			break;
		default:
			break;
		}
	}

	if (target == objects.cont_ss) // нажатие на экран в ScreenSaver
	{
		switch (event_code)
		{
		case LV_EVENT_PRESSED:

			upd = UI_UPD_LOAD_SCR_MAIN;
			xQueueSend(queue_update, &upd, portMAX_DELAY);
			break;

		default:
			break;
		}
	}

	if (target == objects.chk_fil_type) // нажатие чекбокс Тип филамента (фильтр)
	{
		switch (event_code)
		{
		case LV_EVENT_VALUE_CHANGED:

			eFil_ui_roller_profiles_set(profiles[lv_roller_get_selected(objects.roll_profile)].type, NULL);
			//			eFil_message_box(MSG_BOX_OK," NFC record success!",0);
			break;

		default:
			break;
		}
	}
}

void cb_timers(TimerHandle_t xTimer)
{
	uint32_t tim = (uint32_t)pvTimerGetTimerID(xTimer);
	update_t upd;
	switch (tim)
	{
	case timID_1sec:
		/* code */
		touch_count++;

		time_t now = time(NULL);
		if (difftime(now, state.time_now) > 60)
		{
			upd = UI_UPD_TIME;
			xQueueSend(queue_update, &upd, 0);
			state.time_now = now;
		}

		upd = UI_UPD_TIME_SEP;
		xQueueSend(queue_update, &upd, 0);

		break;

	case timID_return:

		if ((touch_count >= TIME_TO_RETURN) && (state.activ_screen == sys_getLvglObjectFromIndex(SCREEN_ID_PROFILE_SEL - 1)))
		{
			upd = UI_UPD_LOAD_SCR_MAIN;
			xEventGroupClearBits(ev_states, STATE_BIT_PROFILES);
			bits_state = xEventGroupGetBits(ev_states);
			xQueueSend(queue_update, &upd, portMAX_DELAY);
		}
		break;
	case timID_ssaver:

		if ((touch_count >= TIME_TO_STBY) && !(bits_state & STATE_BIT_SS))
		{
			bits_state = xEventGroupSetBits(ev_states, STATE_BIT_SS);
			upd = UI_UPD_LOAD_SCR_SSAVER;
			xQueueSend(queue_update, &upd, portMAX_DELAY);
		}
		break;
	case timID_update_time_PCF:

		xEventGroupSetBits(ev_time_sync, TIME_PCF_SYNC_BIT);

		break;
	case timID_sync_time_SNTP:

		xEventGroupSetBits(ev_time_sync, TIME_SNTP_SYNC_BIT);

		break;
	case timID_msg_box_live:

		if (msg_box != NULL) {
			lv_msgbox_close(msg_box);
		}
		break;
	case timID_nfc_rec:

		if (state.nfc_rec_count) {
			state.nfc_rec_count--;
			upd = UI_UPD_NFC_REC_STATUS;
			xQueueSend(queue_update, &upd, portMAX_DELAY);
		}

		break;

	default:
		break;
	}
}