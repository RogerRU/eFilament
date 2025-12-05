#pragma once
#include <stdint.h> 
#include <string.h>

#define COMM_TYPE_COMMAND 'C'			// тип команда
#define COMM_TYPE_DATA 'D'				// тип данные
#define COMM_TYPE_INFO 'I'				// тип инфо


#define COMM_DATA_SENS_WGT							10		// данные весов
#define COMM_DATA_SENS_RFID							20		// данные NFC

#define COMM_COMMAND_RFID_REC						10		// запись NFC
#define COMM_COMMAND_RFID_REC_ABORT					20		// отмена записи NFC
#define COMM_COMMAND_ADC_START_CALIB_OFFSET			30		// калибровка zero offset
#define COMM_COMMAND_ADC_START_CALIB_FULLCASALE		40		// калибровка fullscale


#define COMM_INFO_RFID_REC_OK				100		// запись NFC ok	
#define COMM_INFO_RFID_REC_ERR				101		// запись NFC ошибка
#define COMM_INFO_ADC_REGISTORS				102		// данные регистров ADC (offset, fullscale)
#define COMM_INFO_ADC_CALIB_PRC				103		// данные о процентах процесса калибровки
#define COMM_INFO_ADC_CALIB_ERR				104		// ошибка калибровки
#define COMM_INFO_ADC_OFFSET_CALIB_OK	    105		// калибровка завершена
#define COMM_INFO_ADC_FULLSCALE_CALIB_OK	106		// калибровка завершена


#define COMM_PATTERN_SIZE 3
#define COMM_PATTERN_CHAR '&'

#define BUF_SIZE (1024)
#define READ_BUF_SIZE (BUF_SIZE)

#define COMM_MAX_LEN        200


typedef struct __attribute__((packed))
{
	uint8_t pkt_type;
	uint8_t data_type;
	uint32_t data1;
	uint32_t data2;
	char data3[5];
	char pattern[3];
} packet_t;

packet_t eFil_make_packet(uint8_t pkt_type, uint8_t data_type, uint32_t data1, uint32_t data2, const char *data3, uint8_t send);
