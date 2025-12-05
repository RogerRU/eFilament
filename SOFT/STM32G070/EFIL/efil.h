#include <stdint.h>
#include "i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tim.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "cmsis_os.h"
#include <stdio.h>
#include "usart.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
//#include "../SILVIA_ESP_UI/main/SILVIA/comm.h"



void eflm_init_tasks(void);
void eflm_init_queues(void);
void eflm_init_mutex(void);
void eflm_init_events(void);
void eflm_init_soft_timers(void);
void eflm_init_peripherals(void);
uint8_t eflm_init_AD7190(void);
uint8_t eflm_init_EEPROM(void);
uint32_t weight_adaptive_filter(uint32_t raw_weight);


#define ADC_IRQ_EN	EXTI->IMR1 |= (1 << 3); EXTI->EMR1 |= (1 << 3);		// Включить прерывание по пину AD_RDY
#define ADC_IRQ_DIS EXTI->IMR1 &= ~(1 << 3); EXTI->EMR1 &= ~(1 << 3);	// Выключить прерывание по пину AD_RDY

#define ADC_RATE (120)													// Rate // 4.92 МГц / (480 + 1) ≈ 10 Гц

#define ADC_CH0_NOISE_BITS	4											// Канал -0- количество бит шумов
#define ADC_CH1_NOISE_BITS 1											// Канал -1- количество бит шумов

#define ADC_CALIB_OFFSET_COUNT 30										// количество итераций при калибровки нулевого смещения
#define ADC_CALIB_FULLSCALE_COUNT 100									// количество итераций при калибровки полной шкалы

#define ADC_DEFAULT_ZERO_OFFSET 8449132									// Смещение по умолчанию
#define ADC_DEFAULT_FULLSCALE   67632									// К-т полной шкалы по умолчанию

#define ADC_CALIB_ZERO_OFFSET 0x00000000								// Смещение по умолчанию для калибровки
#define ADC_CALIB_FULLSCALE   0x00800000								// К-т полной шкалы по умолчанию для калибровки

#define WGT_DEFAULT_MES_PERIOD 1000										// частота измерения

#define ADC_CH_SCALES 0

#define BIT_ADC_WGT_EN (1ul << 1)
#define ADC_DEF_CALIB_WGT 1053.0f

#define ADC_CALIBRATE_OFFSET_ON		1													// если 1 то калибровка offset при старте 
#define ADC_CALIBRATE_FULLSCALE_ON	1													// если 1 то калибровка fullscale при старте

#define ZERO_THRESHOLD 20												// погрешность около нуля 

#define ON 1
#define OFF 0

#define NFC_BLOCK_TO_WRITE 4											// номер блока для записи NFC

typedef struct {

	uint32_t offset;
	uint32_t fullscale;
	uint32_t conf_reg;
	uint32_t mode_reg;
	float kADCf;
	uint32_t kADC;
	float tare;
	uint32_t calib_weight;

} ADC_cfg_t;


typedef struct {
	uint8_t state;
	uint32_t wgt_tare;
	uint32_t wgt_abs;
	uint32_t wgt;
	uint32_t wgt_measurement_time;
	char NFC_data_w[16];				// буфер данных для записи NFC
	char NFC_data_r[16];
	uint8_t NFC_write_mode;
} state_t;

typedef enum
{
	bitADC_ready           = (1ul << 0),
	bitWgt_en              = (1ul << 1),
	bitPSI_en              = (1ul << 2),
	bitWgt_calib_offset    = (1ul << 3),
	bitWgt_calib_fullscale = (1ul << 4),
	bitSingle_mode         = (1ul << 5),
	bitCont_mode           = (1ul << 6)


} xADC_Events_t;

typedef enum
{
	BIT_COMMAND_CALIB_OFFSET		         = (1ul << 0),
	BIT_COMMAND_CALIB_FULLSCALE              = (1ul << 1)
//	bitPSI_en              = (1ul << 2),
//	bitWgt_calib_offset    = (1ul << 3),
//	bitWgt_calib_fullscale = (1ul << 4),
//	bitSingle_mode         = (1ul << 5),
//	bitCont_mode           = (1ul << 6)
} command_Events_t;

int32_t calc_diff_percent(uint32_t new_wgt, uint32_t old_wgt);

void eflm_init(void);
void eflm_init_peripherals(void);
void task_init(void * pvParameters);  
void task_get_weight(void* pvParameters);
void task_tx(void* pvParameters);
void task_rx(void* pvParameters);
void task_ADC_data_ready(void* pvParameters);
void task_ADC_calib_offset(void* pvParameters);
void task_ADC_calib_fullscale(void* pvParameters);
void task_ADC_start(void);
void task_calib(void* pvParameters);
void task_NFC(void* pvParameters);

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin);