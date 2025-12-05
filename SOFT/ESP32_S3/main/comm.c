#include "comm.h"
#include <stdint.h> 
#include <string.h>

#if defined(PLATFORM_ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#elif defined(PLATFORM_STM32)
#include "FreeRTOS.h"
#include "queue.h"
#endif



extern QueueHandle_t qSend ;


packet_t eFil_make_packet(uint8_t pkt_type, uint8_t data_type, uint32_t data1, uint32_t data2, const char *data3, uint8_t send)
{
	packet_t pkt;
	
	memset(&pkt, 0, sizeof(packet_t));
	pkt.pkt_type = pkt_type;
	pkt.data_type = data_type;
	pkt.data1 = data1;
	pkt.data2 = data2;
	if (data3) strcpy(pkt.data3, data3);
	
	for (uint8_t i = 0; i < COMM_PATTERN_SIZE; i++)
	{
		pkt.pattern[i] = COMM_PATTERN_CHAR;
	}
	
	if (send) xQueueSend(qSend, (void*) &pkt, portMAX_DELAY);
		
	return pkt;
	}

