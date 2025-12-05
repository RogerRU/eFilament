#include "efil.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "crc.h"
#include "../../eFilament/ESP_SW_VS/main/comm.h"
#include "DRIVERS/AD7190.h"
#include "DRIVERS/rc522.h"
#include "DRIVERS/ee24.h"
#include "time.h"
#include "task.h"

/**
 * TASKS ******************
 */
TaskHandle_t xINIT =				NULL;
TaskHandle_t xGetWeight =			NULL;
TaskHandle_t xSEND =				NULL;
TaskHandle_t xRECIVE =				NULL;
TaskHandle_t xADC_Ready =			NULL;
TaskHandle_t xADC_calib_offset =	NULL;
TaskHandle_t xADC_calib_fullscale = NULL;
TaskHandle_t xCalib =				NULL;
TaskHandle_t xNFC =					NULL;

/**
 * QUEUES ******************
 */
QueueHandle_t qSend =				NULL;
QueueHandle_t qRcv =				NULL;
QueueHandle_t qErr =				NULL;
QueueHandle_t qADC_Data_wgt =		NULL;

/**
 * MUTEX ******************
 */

SemaphoreHandle_t muxLVGL =			NULL;
SemaphoreHandle_t muxI2C1  =		NULL;
SemaphoreHandle_t muxAD7190  =		NULL;

// *************************

/**
 * TIMERS ******************
 */
TimerHandle_t tim_Heartbeat;

enum timerID
{
	timID_1sec      = 0,
	timID_heartbeat = 1
};

/**
 * EVENTS ******************
 */
EventGroupHandle_t  evtADC_gr =		NULL;
EventGroupHandle_t  evtCommands =	NULL;
//xADC_Events_t eADC;

/**
 * SENSORS ******************
 */

EE24_HandleTypeDef EEPROM;

int diff_prc;
ADC_cfg_t ADC_conf;
state_t state;
uint8_t rxBuffer[COMM_MAX_LEN] = { 0 };
volatile uint32_t raw_data;
volatile uint8_t is_zero_calib = 0;
volatile uint8_t is_zero_calib_data_rdy = 0;
extern DMA_HandleTypeDef hdma_usart1_rx;

int32_t calc_diff_percent(uint32_t new_wgt, uint32_t old_wgt)
{
	if (old_wgt == 0) {
		// Обработка случая деления на ноль
		if (new_wgt == 0) {
			return 0;  // Оба значения равны 0 - разница 0%
		}
		else {
			return 100;  // Любое изменение от 0 считаем +100%
		}
	}
    
	// Вычисляем разницу с приведением к int64_t чтобы избежать переполнения
	int64_t diff = (int64_t)new_wgt - (int64_t)old_wgt;
    
	// Вычисляем процентное отношение с округлением до ближайшего целого
	int64_t percent = (diff * 100 + (diff < 0 ? -(int64_t)old_wgt / 2 : (int64_t)old_wgt / 2)) / (int64_t)old_wgt;
    
	// Проверяем границы int32_t
	if (percent > INT32_MAX) return INT32_MAX;
	if (percent < INT32_MIN) return INT32_MIN;
    
	return (int32_t)percent;
}
uint32_t weight_adaptive_filter(uint32_t val)
{
	static int32_t filt = 0;
	static uint8_t count = 0;
	const int32_t thrsh = 50;
	const uint8_t n = 5;

	if ((val > filt && (val - filt) > thrsh) || (filt > val && (filt - val) > thrsh)) {
		if (++count >= n) {
			filt = val;
			count = 0;
		}
	}
	else {
		filt = val;
		count = 0;
	}

	return filt;
	
}
uint32_t PeakFilter(uint32_t val, bool init) {
	
	static uint32_t thrsh = 20; // порог
	static uint8_t n = 4; // количество значений
    
	// Состояние фильтра
	static uint32_t filt = 0;
	static uint8_t count = 0;
	static uint8_t initialized = 0;
    
	if (init) {initialized = 0;}
	
	// Инициализация при первом вызове
	if (!initialized) {
		filt = val;
		initialized = 1;
		return filt;
	}
    
	// Алгоритм фильтрации
	if ((val > filt && val - filt > thrsh) || (filt > val && filt - val > thrsh))
	{
		if (++count >= n) {
			filt = val;
			count = 0;
		}
	}
	else {
		filt = val;
		count = 0;
	}
    
	return filt;
}
void eflm_init(void)  // Создание задачи первоначальной инициализации
{
	BaseType_t xReturned;

	HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_SET);

	xReturned = xTaskCreate(
		task_init,
		"Silvia INITIALIZE",
		2 * 128,
		NULL,
		osPriorityNormal,
		&xINIT);
	ADC_IRQ_DIS
}

void eflm_init_peripherals(void)
{
	uint8_t all_ok = 1;

	if (!eflm_init_AD7190()){ all_ok = 0; }
	else
	{
		__NOP();
	}
	if (all_ok)
	{
		HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, GPIO_PIN_SET);
	}
}

uint16_t ADC_set_params(void)
{
	uint32_t tmp_reg = 0;
	ADC_IRQ_DIS
	/* ------------- внутреняя калибровка (опционно) --------------------------- */
//	AD7190_Calibrate(AD7190_MODE_CAL_INT_ZERO);
//	osDelay(20);
//	AD7190_Calibrate(AD7190_MODE_CAL_INT_FULL);
//	osDelay(20);

/* ------------- общие установки по ВСЕМ каналам --------------------------- */

	tmp_reg = AD7190_MODE_SEL(AD7190_MODE_CONT)				// режим постоянного измерения
			| AD7190_MODE_CLKSRC(AD7190_CLK_INT)			// внутренний кварц
			| AD7190_MODE_DAT_STA							// доп. байт инфы в результатах
			| AD7190_MODE_RATE(ADC_RATE);					// 480 rate

	ADC_conf.mode_reg = tmp_reg;

	AD7190_SetPower(SET_IDLE);								// переводим в режим ожидания
	osDelay(20);

	AD7190_SetRegisterValue(AD7190_REG_MODE, tmp_reg, 3);
	osDelay(20);
	
	tmp_reg = 0;
	
	tmp_reg = AD7190_CONF_CHAN(AD7190_CH_AIN1P_AIN2M)		// диф. пара для тензодатчика
			| AD7190_CONF_GAIN(AD7190_CONF_GAIN_128)		// усиление для моста тензодатчика ( +-39.06 mV)
		//	| AD7190_CONF_CHOP								// Включаем CHOP Mode
			| AD7190_CONF_UNIPOLAR;							// Юниполярный режим измерения;

	ADC_conf.conf_reg = tmp_reg;

	AD7190_SetRegisterValue(AD7190_REG_CONF, tmp_reg, 3);
	osDelay(20);

	AD7190_SetPower(SET_IDLE);								// переводим в режим ожидания
	if (ADC_conf.offset) {
		AD7190_SetRegisterValue(AD7190_REG_OFFSET, ADC_conf.offset, 3);			// OFFSET для канала тензодатчика из конфига
		osDelay(20);
	}
	else
	{
		AD7190_SetRegisterValue(AD7190_REG_OFFSET, ADC_DEFAULT_ZERO_OFFSET, 3); // OFFSET для канала тензодатчика по умолчанию
		osDelay(20);
		ADC_conf.offset = ADC_DEFAULT_ZERO_OFFSET;
	}
	if (ADC_conf.fullscale) {
		AD7190_SetRegisterValue(AD7190_REG_FULLSCALE, ADC_conf.fullscale, 3); // FULLSCALE для канала тензодатчика из конфига
		osDelay(20);
	}
	else
	{
		AD7190_SetRegisterValue(AD7190_REG_FULLSCALE, ADC_DEFAULT_FULLSCALE, 3); // FULLSCALE для канала тензодатчика из конфига
		osDelay(20);
		ADC_conf.fullscale = ADC_DEFAULT_FULLSCALE;
	}
	return 0;
}
void ADC_start(void)
{
	AD7190_SetRegisterValue(AD7190_REG_MODE, ADC_conf.mode_reg, 3);
	osDelay(20);
	ADC_IRQ_EN				// Влючаем прерывание по пину ADC_rdy
}
uint8_t eflm_init_AD7190(void)
{
	if (AD7190_Init())
	{
		ADC_set_params();
		__NOP();
		return 1;
	}
	else
	{
		__NOP();
		return 0;
	}
}
void eflm_init_tasks(void)
{
	BaseType_t xReturned;

	xReturned = xTaskCreate(task_get_weight,"GetWeight",5 * 128,NULL,osPriorityNormal,&xGetWeight);
	if (xReturned != pdPASS)
	{
		__NOP(); // The task was created. Use the task's handle to delete the task.
	}
	xReturned = xTaskCreate(task_tx,"SEND",3 * 128,NULL,osPriorityNormal,&xSEND);
	xReturned = xTaskCreate(task_rx,"RECEIVE",1 * 128,NULL,osPriorityNormal,&xRECIVE);
	xReturned = xTaskCreate(task_ADC_data_ready,"ADC_data_ready",1 * 128,NULL,osPriorityRealtime,&xADC_Ready);
	xReturned = xTaskCreate(task_ADC_calib_offset,"ADC_calib_offset",2 * 128,NULL,osPriorityHigh,&xADC_calib_offset);
	xReturned = xTaskCreate(task_ADC_calib_fullscale,"ADC_calib_fullscale",2 * 128,NULL,osPriorityHigh,&xADC_calib_fullscale);
	xReturned = xTaskCreate(task_calib,"Scale_calibrate",1 * 128,NULL,osPriorityHigh,&xCalib);
	xReturned = xTaskCreate(task_NFC,"NFC",2 * 128,NULL,osPriorityHigh,	&xNFC);
}

void eflm_init_queues(void)
{
	qSend				= xQueueCreate(10, sizeof(packet_t));
	qRcv				= xQueueCreate(10, COMM_MAX_LEN);
	qADC_Data_wgt		= xQueueCreate(1, sizeof(uint32_t));
}

void eflm_init_soft_timers(void)
{
	//	tim_Heartbeat = xTimerCreate ("tim_heartbeat",3000,pdTRUE,(void* const)timID_heartbeat,cb_soft_timers);
	//	xTimerStart(tim_Heartbeat, 0);
}
void eflm_init_mutex(void)
{
	//muxI2C2		= xSemaphoreCreateMutex();
	muxAD7190	= xSemaphoreCreateMutex();
}
void eflm_init_events(void)
{
	evtADC_gr = xEventGroupCreate();
	evtCommands = xEventGroupCreate();
}

void task_init(void * pvParameters)
{
	for (;;)
	{
		eflm_init_peripherals();
		eflm_init_mutex();
		eflm_init_queues();
		eflm_init_soft_timers();
		eflm_init_events();
		eflm_init_tasks();

		xEventGroupSetBits(evtADC_gr, BIT_ADC_WGT_EN);					// разрешаем измерение веса	
		ADC_start();													// Стартуем ADC
		__HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);				// выключаем прерывание на пол пакета (хз, работает и так)
		HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rxBuffer, COMM_MAX_LEN);	// включаем прием данных по UART
		vTaskDelete(NULL);												// удаляем задачу инициализации
	}
}
void task_ADC_data_ready(void* pvParameters)
{
	uint8_t tmpkt[4], noise_bits_cnt;
	uint32_t tmpdt;
	uint8_t adc_status_reg;

	while (1)
	{
		ulTaskNotifyTake(1, portMAX_DELAY);		// ожидание пока придет нотификация из обработчика прерывания от ADS_rdy
		adc_status_reg = 0;

		AD7190_WriteByte(AD7190_COMM_READ | AD7190_COMM_ADDR(AD7190_REG_DATA)); 
		AD7190_ReadBuff(tmpkt, 4);				// читаем данные в массив 3 байта данные 1 байт доп. инфа
		adc_status_reg = tmpkt[3];				// доп. инфа

		if ((adc_status_reg & 0x07) == 0) {		// проверяем с какого канала данные  и ставим количество незначащих битов (в этом проекте только 1 канал)
			noise_bits_cnt = ADC_CH0_NOISE_BITS;
		}
		else {
			noise_bits_cnt = ADC_CH1_NOISE_BITS;
		}

		if ((adc_status_reg & AD7190_STAT_RDY) == 0 && (adc_status_reg & AD7190_STAT_ERR) == 0) 
		{
			tmpdt = (tmpkt[0] << 16) | (tmpkt[1] << 8) | tmpkt[2];	 // Получаем сырое 24-битное значение (прямой двоичный код)
            
			tmpdt = tmpdt >> noise_bits_cnt;						// В униполярном режиме данные уже положительные. Просто сдвигаем на количество незначащих битов
            
			uint32_t value_to_send = (uint32_t)tmpdt;				// uint32_t, а не int32_t т.к. униполярный режим, значения всегда строго положительны
			
			if (value_to_send < ZERO_THRESHOLD) {
				value_to_send = 0;
			}
			
			if (xQueueOverwrite(qADC_Data_wgt, &value_to_send) != pdPASS) {
				__NOP();											// Очередь полная
			}
 			raw_data = tmpdt;										// raw_data - глобальная переменная для отладки		
			if (state.wgt_abs && !raw_data)							// если резко сняли груз вызываем задачу обработки 
			{
				xTaskNotifyGive(xGetWeight);
			}
		}
		else {
			if (adc_status_reg & AD7190_STAT_ERR) {
			__NOP();	// Ошибка преобразования
			}
			__NOP();		// 	данные не готовы							
		}
	//	osDelay(500);
        ADC_IRQ_EN													// включаем прерывание пина AD_rdy
		vTaskDelay(1);
	}
}
void task_ADC_calib_offset(void* pvParameters)
{
	uint32_t temp_data, offset;

	for (;;)
	{
		xEventGroupWaitBits(evtADC_gr, bitWgt_calib_offset,	pdTRUE,	pdFALSE, portMAX_DELAY);		// Ждем бита на калибровку Офсета
		xSemaphoreTake(muxAD7190, portMAX_DELAY);
		ADC_IRQ_DIS																					// запрещаем прерывания 

		AD7190_SetPower(SET_IDLE);																	// ставим в IDLE что бы записать offset
		AD7190_SetRegisterValue(AD7190_REG_OFFSET, ADC_CALIB_ZERO_OFFSET, 3);						// ставим offset = 0
		
		uint32_t command = 0;
		command = (ADC_conf.mode_reg & 0x1fffff) |  AD7190_MODE_SEL(AD7190_MODE_CAL_SYS_ZERO);		// Ставим режим калибровки нуля

		offset = 0;
		is_zero_calib = 1;																		    // поднимаем флаг что калибруем zero для HAL_GPIO_EXTI_Falling_Callback
		HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_RESET);
		uint32_t offs[ADC_CALIB_OFFSET_COUNT] = {0};
		
		for (uint8_t i = 0; i < ADC_CALIB_OFFSET_COUNT; i++)									    // Получаем ADC_CALIB_OFFSET_COUNT значений и считаем среднее далее
		{
			
			AD7190_SetPower(SET_IDLE);																// ставим в IDLE что бы записать offset
			AD7190_SetRegisterValue(AD7190_REG_OFFSET, ADC_CALIB_ZERO_OFFSET, 3);					// каждая итерация с offset = 0
			
			HAL_GPIO_TogglePin(LED_RDY_GPIO_Port, LED_RDY_Pin);
			osDelay(20);
			
			AD7190_SetRegisterValue(AD7190_REG_MODE, command, 3);
			osDelay(20);

			ADC_IRQ_EN

			while(!is_zero_calib_data_rdy) {}														// ждем готовность данных
			is_zero_calib_data_rdy = 0;
			temp_data = AD7190_GetRegisterValue(AD7190_REG_OFFSET, 3);
			offs[i] = temp_data;
			offset += temp_data;
			
			eFil_make_packet(COMM_TYPE_INFO,COMM_INFO_ADC_CALIB_PRC,(uint32_t)((i/(float)(ADC_CALIB_OFFSET_COUNT)) *100), 0, NULL, true);
			
		}

		HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, GPIO_PIN_RESET);

		//======================== получаем нулевое смещение ==============================
		offset /= ADC_CALIB_OFFSET_COUNT; // Было ADC_CALIB_OFFSET_COUNT циклов
		
		float diff = ((offset*1.0f - ADC_conf.offset*1.0f) / (ADC_conf.offset * 1.0f)) * 100.0 ;
		ADC_conf.offset = offset;
		
		AD7190_SetPower(SET_IDLE);
		AD7190_SetRegisterValue(AD7190_REG_OFFSET, offset, 3); // Заносим в регистр новое значение смещения
		osDelay(20);
		AD7190_SetRegisterValue(AD7190_REG_MODE, ADC_conf.mode_reg, 3);
		osDelay(20);

		ADC_conf.offset = offset;
		is_zero_calib = 0;																					// снимаем флаг калибровки оффсета
		eFil_make_packet(COMM_TYPE_INFO, COMM_INFO_ADC_OFFSET_CALIB_OK, ADC_conf.offset, 0, NULL, true);	// пакет об окончании калибровки

		xSemaphoreGive(muxAD7190);

		vTaskDelay(1);
	}
}
void task_ADC_calib_fullscale(void* pvParameters)
{
	uint32_t ADC_rawdata;
	uint32_t sum;
	uint32_t kADC;
//	float kADC_f;

	for (;;)
	{
		xEventGroupWaitBits( evtADC_gr,bitWgt_calib_fullscale,pdTRUE,pdFALSE,portMAX_DELAY); // ждем бита bitWgt_calib_fullscale

		xSemaphoreTake(muxAD7190, portMAX_DELAY);
		ADC_IRQ_DIS

		AD7190_SetPower(SET_IDLE);
		AD7190_SetRegisterValue(AD7190_REG_FULLSCALE, ADC_CALIB_FULLSCALE, 3);
		osDelay(20);

		uint32_t timeToWait = HAL_GetTick();
		HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, OFF);
				
		HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, ON);								// ждем 6 сек., чтобы поставить вес
		while (HAL_GetTick() < timeToWait + 6000) {}
		HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, OFF);

		sum = 0;
		ADC_rawdata = 0;
		uint32_t curr_mode_reg = 0;

//		curr_mode_reg = AD7190_GetRegisterValue(AD7190_REG_MODE, 3); // сохраняем значение регистра MODE
//		uint32_t command = (ADC_conf.mode_reg & 0x1fffff) |  AD7190_MODE_SEL(AD7190_MODE_CONT);
//		AD7190_SetRegisterValue(AD7190_REG_MODE, command, 3);

		AD7190_SetRegisterValue(AD7190_REG_MODE, ADC_conf.mode_reg, 3);
		osDelay(20);

		ADC_IRQ_EN

		for(uint8_t i = 0 ; i <= ADC_CALIB_FULLSCALE_COUNT ; i++)	// ПОЛУЧЕНИЕ ЗНАЧЕНИЯ С НАГРУЗКОЙ 100 раз
		{
			HAL_GPIO_TogglePin(LED_ERR_GPIO_Port, LED_ERR_Pin);

			if (uxQueueMessagesWaiting(qADC_Data_wgt) == 0)
			{
				__NOP();
			}
			xQueueReceive(qADC_Data_wgt, &ADC_rawdata, portMAX_DELAY);
			sum += ADC_rawdata; // сумма
			eFil_make_packet(COMM_TYPE_INFO, COMM_INFO_ADC_CALIB_PRC, (uint32_t)((i * 1.0f / ADC_CALIB_FULLSCALE_COUNT * 1.0f) * 100), 0, NULL, true);
		}

		
		uint32_t adjusted_sum = (uint32_t)(sum / (ADC_CALIB_FULLSCALE_COUNT / 2)); // ВОССТАНАВЛИВАЕМ оригинальную логику с учетом начального коэффициента 0.5 т.к. fullscale = 0x00800000 т.е. 0.5 от полной шкалы
        
		/*
		Расчет коэффициента с использованием 64-битной арифметики
		kADC = (calib_weight * 2^24) / adjusted_sum
		*/
		uint64_t numerator = (uint64_t)ADC_conf.calib_weight * (1ULL << 24);
		kADC = (uint32_t)(numerator / adjusted_sum);
		
		kADC = (kADC * 1053) / 1042;	// опытный доб Коэф, его быть не должно, но без него ошибка ~1% почему - хз
		
//		sum /= (ADC_CALIB_FULLSCALE_COUNT / 2); // Так как начальный к-т = 0.5 (100 * 0.5 = 50)   0x00800000 соответствует 0.5 от полной шкалы
//		kADC_f = (ADC_conf.calib_weight / (float) sum);
//		uint32_t kADC_old = conv_float_to_bin(kADC_f);

		AD7190_SetPower(SET_IDLE);
		osDelay(20);
		AD7190_SetRegisterValue(AD7190_REG_FULLSCALE, kADC, 3);
		osDelay(20);

		xSemaphoreGive(muxAD7190);
		HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, OFF);
		HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, ON);

		ADC_conf.fullscale = kADC;

		AD7190_SetRegisterValue(AD7190_REG_MODE, ADC_conf.mode_reg, 3); // восстанавливаем регистр MODE
		osDelay(20);

		//	EEPROM_conf_write();
		xEventGroupSetBits(evtADC_gr, bitWgt_en);
		eFil_make_packet(COMM_TYPE_INFO, COMM_INFO_ADC_FULLSCALE_CALIB_OK,ADC_conf.fullscale,0,NULL, true);
		
		vTaskDelay(500);
	}
}

void task_get_weight(void* pvParameters)
{
	uint32_t ADC_rawdata, old_wgt;
	uint32_t wgt_mes_delay = WGT_DEFAULT_MES_PERIOD;
	uint8_t wgt_mes_count = 0;							// счетчик измерений 

	for (;;)
	{
		xEventGroupWaitBits(evtADC_gr,BIT_ADC_WGT_EN,pdFALSE,pdFALSE,portMAX_DELAY); // ждем бита BIT_ADC_WGT_EN (разрешение измерения веса т.е НЕ калибровка)
		xSemaphoreTake(muxAD7190, portMAX_DELAY);									// возможно лишнее если есть xEventGroupWaitBits
		
		uint32_t ulNotificationValue;
		BaseType_t xResult = xTaskNotifyWait(0, 0, &ulNotificationValue, pdMS_TO_TICKS(wgt_mes_delay));
		
		if (xQueueReceive(qADC_Data_wgt, &ADC_rawdata, 100))
		{
			if (!ADC_rawdata && old_wgt)		// если резко убрали вес (сняли катушку) 
			{
				state.wgt_abs = PeakFilter(ADC_rawdata, true);	// инит фильтра заново
				wgt_mes_delay = WGT_DEFAULT_MES_PERIOD;				
				wgt_mes_count = 0;
			}
			
			if (ADC_rawdata && old_wgt) // если вес на весах уже был (прошлое и текущее значение не 0)
			{		
				if (wgt_mes_count >= 10)								// увеличим время измерения на через 10 предыдущих
					{
						wgt_mes_delay = WGT_DEFAULT_MES_PERIOD * 10;	// увеличиваем время измерения при стабилизации веса на весах
					}
					else {
						wgt_mes_count++;								
					}
					state.wgt_abs = PeakFilter(ADC_rawdata, false);
			}
			
			if (ADC_rawdata && !old_wgt)									// если веса на весах не было (текущее значение 0)
			{
				wgt_mes_delay = WGT_DEFAULT_MES_PERIOD;
				wgt_mes_count = 0;
				state.wgt_abs = PeakFilter(ADC_rawdata, true);				// инит фильтра заново
			}
			
			
			if (old_wgt != state.wgt_abs)
			{
				eFil_make_packet(COMM_TYPE_DATA, COMM_DATA_SENS_WGT, state.wgt_abs,0,NULL, true);
			}
			old_wgt = state.wgt_abs;
		}
		xSemaphoreGive(muxAD7190);
	}
}

void task_tx(void* pvParameters)
{
	packet_t data;

	for (;;)
	{
		if (uxQueueMessagesWaiting(qSend))
		{
			xQueueReceive(qSend, &data, portMAX_DELAY);
			HAL_UART_Transmit_DMA(&huart1, (uint8_t*) &data, sizeof(packet_t));
		}
		vTaskDelay(50);
	}
}

void task_rx(void* pvParameters)
{
	uint8_t rx_data[COMM_MAX_LEN];
	packet_t data;
	
	for (;;)
	{
		xQueueReceive(qRcv, &rx_data, portMAX_DELAY);
		memcpy(&data, rx_data, sizeof(packet_t));
		
		if (data.pkt_type == COMM_TYPE_COMMAND )
		{
			switch (data.data_type)
			{
			case COMM_COMMAND_RFID_REC:				// команда режима записи NFC
				
				strcpy(state.NFC_data_w,data.data3);
				state.NFC_write_mode = true;
				HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, GPIO_PIN_RESET);
				HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_SET);
				HAL_TIM_Base_Start_IT(&htim6);
			break;
			case COMM_COMMAND_RFID_REC_ABORT:		// команда отмены режима записи NFC
				
				state.NFC_write_mode = false;
				HAL_TIM_Base_Stop_IT(&htim6);
				HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_RESET);
				break;	
				
			case COMM_COMMAND_ADC_START_CALIB_OFFSET:	// команда калибровки OFFSET
				
				xEventGroupSetBits(evtCommands, BIT_COMMAND_CALIB_OFFSET);
				break;	
				
			case COMM_COMMAND_ADC_START_CALIB_FULLCASALE:	// команда калибровки FULLSCALE
				
				xEventGroupSetBits(evtCommands, BIT_COMMAND_CALIB_FULLSCALE);
				ADC_conf.calib_weight = data.data1;
				break;	
				
			default:
				break;
			}
		}
		if (data.pkt_type == COMM_TYPE_INFO)	
		{
			switch (data.data_type)
			{
			case COMM_INFO_ADC_REGISTORS:		// пришли данные из конфига по offset и fullscale
				
				if (data.data1) ADC_conf.offset = data.data1;
				if (data.data2) ADC_conf.fullscale = data.data2;
				ADC_set_params();
				ADC_start();
				
			default:
				break;
			}
		
		}
		vTaskDelay(10);
	}
}
void task_calib(void* pvParameters)
{
	EventBits_t bits;
	
	for (;;)
	{
	bits = xEventGroupWaitBits(evtCommands, BIT_COMMAND_CALIB_OFFSET | BIT_COMMAND_CALIB_FULLSCALE, pdTRUE, pdFALSE, portMAX_DELAY); // Ждем битов на калибровку 	
	
		if (bits & BIT_COMMAND_CALIB_OFFSET)
		{ 
			xEventGroupClearBits(evtADC_gr, bitWgt_en);
			xEventGroupSetBits(evtADC_gr, bitWgt_calib_offset);
			xEventGroupSetBits(evtADC_gr, bitWgt_en);
		}
		
		if (bits & BIT_COMMAND_CALIB_FULLSCALE)
		{ 
			xEventGroupClearBits(evtADC_gr, bitWgt_en);
			xEventGroupSetBits(evtADC_gr, bitWgt_calib_fullscale);
			xEventGroupSetBits(evtADC_gr, bitWgt_en);
		}
		vTaskDelay(500);
	}
}
void task_NFC(void* pvParameters)
{
	uint8_t blockAddr;
	uint8_t RC_size;
	uint8_t sectorKeyA[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	uint8_t status;
	uint8_t	str[MFRC522_MAX_LEN];
	uint8_t sn[4];
	char	buff[64];
	
	memset(state.NFC_data_w, 0, sizeof(state.NFC_data_w));
		
	for (;;)
	{
		MFRC522_Init();
		osDelay(20);
		status = MI_ERR;

		status = MFRC522_Request(PICC_REQIDL, str); // Look for the card, return type
		if (status == MI_OK) {
			__NOP();					// тут должны быть обработчики, но их нет (лень-матушка)
		}
		else {
			__NOP();
		}
		status = MFRC522_Anticoll(sn); // Anti-collision, return the card's 4-byte serial number
		if (status == MI_OK) {
			__NOP();
		}
		else {
			__NOP();
		}
		RC_size = MFRC522_SelectTag(sn); // Election card, return capacity
		if (RC_size != 0) {
			__NOP();
		}
		else {
			__NOP();
		}
		status = MFRC522_Auth(PICC_AUTHENT1A, NFC_BLOCK_TO_WRITE, sectorKeyA, sn); // Card reader
		if (status == MI_OK) {
			// Read data
			if (!state.NFC_write_mode) // если режим чтения
				{	
				memset(state.NFC_data_r, 0, sizeof(state.NFC_data_r));
				status = MFRC522_Read(NFC_BLOCK_TO_WRITE, state.NFC_data_r);
				if (status == MI_OK) {
					
					eFil_make_packet(COMM_TYPE_DATA, COMM_DATA_SENS_RFID, 0, 0, state.NFC_data_r, true);
					HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_SET);
					osDelay(100);
					HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_RESET);
				}
			}
			else			// Если режим записи
			{
				status = MFRC522_Write(NFC_BLOCK_TO_WRITE, state.NFC_data_w);

				if (status == MI_OK)
				{
					HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, GPIO_PIN_RESET);
					HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_SET);
					osDelay(100);
					HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, GPIO_PIN_RESET);
					HAL_GPIO_WritePin(LED_RDY_GPIO_Port, LED_RDY_Pin, GPIO_PIN_SET);
					
					eFil_make_packet(COMM_TYPE_INFO, COMM_INFO_RFID_REC_OK, 0, 0, NULL, true);
					state.NFC_write_mode = false;
					HAL_TIM_Base_Stop_IT(&htim6);					// стоп мигалка диодом
				}
			}
		}
		else {
			__NOP();
		}
		
		MFRC522_Halt();
		MFRC522_AntennaOff();
		vTaskDelay(1000);
	}
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)

{
	if (GPIO_Pin == AD_IRQ_Pin) {
		if (!is_zero_calib) {
			BaseType_t xHigherPriorityTaskWoken;
			xHigherPriorityTaskWoken = pdFALSE;
			vTaskNotifyGiveFromISR(xADC_Ready, &xHigherPriorityTaskWoken);	// Если прерывание действительно на AD_RDY от AD7190 то разблокируем задачу по получению данных
			portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);				// и немедленно, при выходе, передаем ей управление уведомлением
		}
		else
		{
			is_zero_calib_data_rdy = 1;
		}
		ADC_IRQ_DIS															// Запрещаем прерывание на AD_RDY от AD7190 пока не обработаем данные
	}
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {

	BaseType_t xHigherPriorityTaskWoken;
	xHigherPriorityTaskWoken = pdFALSE;
	HAL_GPIO_TogglePin(LED_ERR_GPIO_Port, LED_ERR_Pin);

	if (huart->Instance == USART1) {
		for (int i = 0; i < COMM_MAX_LEN - 2; ++i)
		{
			if ((rxBuffer[i] == COMM_PATTERN_CHAR)&&(rxBuffer[i + 1] == COMM_PATTERN_CHAR)&&(rxBuffer[i + 2] == COMM_PATTERN_CHAR)&&(i + 2 < COMM_MAX_LEN)) // находим паттерн
			{
				xQueueSendFromISR(qRcv, rxBuffer, &xHigherPriorityTaskWoken);
				memset(rxBuffer, 0x00, COMM_MAX_LEN);
				break;
			}
		}
		HAL_UARTEx_ReceiveToIdle_DMA(&huart1, (uint8_t*)rxBuffer, COMM_MAX_LEN);
		//__HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);				// Запрещаем прерывание на половину приема
	}
	return;
}
// Обработчик ошибок UART
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART1) {
		__HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF); // Сбрасываем флаги ошибок: Overrun, Noise, Framing, Parity
        
		HAL_UART_DMAStop(huart);
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rxBuffer, COMM_MAX_LEN);	// перевключаем DMA
	}
}
