#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "time.h"
#include "config.h"


#include "esp_err.h"

#define EX_UART_NUM UART_NUM_2
#define U0_TXD_PIN (GPIO_NUM_17)
#define U0_RXD_PIN (GPIO_NUM_18)

#define AMOLED_MAIN_BRIGHTLESS 80
#define AMOLED_SS_BRIGHTLESS 30


#define PROFILES_MAX_COUNT 20

#define COLOR_ARC_100_50      0xff77dd77        // цвета арки
#define COLOR_ARC_49_25       0xffFCE883
#define COLOR_ARC_24_10       0xffFF7514
#define COLOR_ARC_9_0         0xffD3212D

#define COLOR_TXT_DEFAULT     0xFFFFFFFF
#define COLOR_TXT_100_50      0xFFA5D6A7        // цвета цифр процентов
#define COLOR_TXT_49_25       0xFFFFF9C4
#define COLOR_TXT_24_10       0xFFFFA726
#define COLOR_TXT_9_0         0xFFEF5350

#define COLOR_WIFI_DIS        0xff424242
#define COLOR_WIFI_EN         0xFF2962FF
	
#define COLOR_PROF_ALT_TXT    ("#C0CA33 ")				  // цвет выделения текскта в профайле, пробелы ОБЯЗАТЕЛЬНЫ!!!!

#define WGT_MIN               0							  // минимальный вес меньше которого считаем, что на весах ничего нет.

#define TIMER_1SEC_COUNT					1000          // (1 сек)
#define TIMER_STBY_COUNT					1000          // Время проверки нажатия экрана для перехода в Standby (1 сек)
#define TIMER_RETURN_COUNT					1000*10       // Время перехода в Standby  (10 сек)
#define TIMER_REC_COUNT						1000*10       // Время ожидания записи NFC (10 сек)
#define TIMER_UPDATE_PCF_COUNT				1000*60*60    // Время обновления времени с PCF (1 час)
#define TIMER_SYNC_SNTP_COUNT				1000*60*60*5  // Время обновления времени с SNTP (5 час)
#define TIMER_MSG_BOX_DEFAULT_LIVE_TIME     3000		  // Время жизни MessageBox (3 sec)
#define TIMER_NFC_REC_COUNT					1000		  // Отсчет секунд ожидания при записи NFC


#define NFC_REC_WAIT_TIME			10		      // время ожидания метки NFC	


#define TIME_SNTP_SYNC_BIT			BIT1
#define TIME_PCF_SYNC_BIT			BIT2

#define STATE_BIT_READY				BIT0
#define STATE_BIT_SS				BIT1
#define STATE_BIT_PROFILES			BIT2
#define STATE_BIT_REC				BIT3
#define STATE_BIT_CALIB_OFFSET		BIT4
#define STATE_BIT_CALIB_FULLSCALE	BIT5


#define TIME_TO_STBY         60*15          // Время перехода в STBY В СЕК
#define TIME_TO_RETURN       7              // Время перехода в main  В СЕК

#define TASK_MAX_COUNT 10


#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


typedef struct {
	char ssid[32];
	char pass[64];
	uint8_t hasPass;
	uint8_t isConnected;
	uint8_t SNTP_synchronized;
} wifi_t;

typedef struct {
	const char* name;
	uint32_t total;
	uint32_t free;
	uint32_t min_free;
	uint32_t largest_free_block;
	uint32_t caps;
} mem_info_t;

typedef struct {
	TaskHandle_t handle;
	const char* name;
	uint32_t stack_size;
	UBaseType_t priority;
	BaseType_t core_id;
} task_info_t;

typedef struct {
	char id[5]; // ex. A234
	char type[15]; // PLA, PETG, ABS, etc
	char vendor[20]; // ex. eSUN, ERYONE, FDPlast, etc
	char info[15]; // ex. Matte, CF, etc
	char info2[20]; // 1KG(carton)
	uint16_t full_w; // full weight (g)
	uint16_t spool_w; // empty spool weight (g)
	double    density; // density
	double    dia; // filament diam (1.75mm)
} profile_t;

typedef struct
{
	time_t time_now;
	uint32_t abs_wgt;
	uint8_t profiles_num;
	profile_t* profile_active;
	lv_obj_t* activ_screen;
	int curr_prc;
	int cur_wgt;
	int cur_lgt;
	int cur_total;
	uint8_t lcd_brightless;
	uint8_t opt_always_show_total;
	uint8_t rec_countdown;
	char profile_active_id[5];
	uint8_t nfc_rec_count;
	config_item_t options[CONFIG_MAX_OPIONS_NUM];
	int options_count;
	uint32_t ADC_offset;
	uint32_t ADC_fullscale;
	uint32_t ADC_ref_weight;
	uint8_t ADC_calib_prc;
	
	
} state_t;

typedef enum
{
	UI_UPD_PROFILE_HEADER    = 0,
	UI_UPD_PROFILE,
	UI_UPD_TIME,
	UI_UPD_TIME_SEP,
	UI_UPD_LOAD_SCR_MAIN,
	UI_UPD_LOAD_SCR_PROFILES,
	UI_UPD_LOAD_SCR_SSAVER,
	UI_UPD_WIFI_STATUS,
	UI_UPD_NFC_REC_STATUS,
	UI_UPD_NFC_REC_START,
	UI_UPD_NFC_REC_STOP,
	UI_UPD_CALIB_START,
	UI_UPD_CALIB_STOP,
	UI_UPD_CALIB_PROGRESS
		
} update_t;

typedef enum
{
	MSG_BOX_OK  = 0,
	MSG_BOX_ERR
		
} msg_box_t;

typedef enum
{
	NFC_NONE    = 0,
	NFC_OK,
	NFC_ERROR,
	NFC_ABORTED,
	NFC_TIMEOUT
	
} NFC_result_t;


void sys_trim_whitespace(char *str);
double sys_alt_atof(const char *str);
esp_err_t eFil_profiles_load(void);
lv_obj_t *sys_getLvglObjectFromIndex(int32_t index);
void time_set_system(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
void sys_create_task(TaskFunction_t task_func, const char* name, uint32_t stack_size, void* param, UBaseType_t priority, TaskHandle_t* out_handle, BaseType_t core_id);

// INITS --------------
void eFil_init(void);
void eFil_init_ui(void);
void eFil_init_uart(void);
uint8_t eFil_init_spiffs(void);

void eFil_init_tasks(void);
void eFil_init_tims(void);
void eFil_init_queues(void);
 ;
void eFil_init_events(void);
void eFil_init_mutex(void);


// PROFILES --------
uint8_t eFil_profile_set_active(const char *id);
void eFil_profile_calculate(profile_t* profile);
void eFil_ui_roller_profiles_set(const char* filter_type, const char* filter_vendor);
uint8_t eFil_profile_set_zero();
void eFil_profile_update(const char* old_id, profile_t new_profile);
uint8_t eFil_profile_add(profile_t new_profile);
uint8_t eFil_profile_delete(const char* id, bool save);
int eFil_profile_found(const char *id);
profile_t* eFil_profile_found_handle(const char *id);
esp_err_t eFil_profiles_save();
esp_err_t eFil_profiles_load();

void eFil_ui_message_box(msg_box_t typ, const char *text, uint32_t live_time);
void eFil_rec_start(const char *id);
void eFil_rec_stop(NFC_result_t reason);

// CONFIG --------
int config_load();
int config_save();


// TASKS -------
void task_ui_redraw(void *pvParameters);
void task_rx(void *pvParameters);
void task_rx_parse(void *pvParameters);
void task_tx(void *pvParameters);
void task_wifi(void *pvParameters);

// Timers & Events -------------
void cb_timers(TimerHandle_t xTimer);
void action_touch_press(lv_event_t *e);

// Date-time ---------------
void time_get_from_PCF(void);
void time_set_to_PCF(void);

