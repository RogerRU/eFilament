#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "lvgl.h"

#include "e_filament.h"
#include "DRIVERS/SH6801/ws_esp32s3_amoled_143.h"
#include "UI/ui.h"
#include "UI/styles.h"
#include <UI/screens.h>
#include "UI/actions.h"

#include "esp_psram.h"
#include "task_monitor.h"

#include "color_log.h"
#include "wifi.h"
#include "config.h"

void app_main(void)
{
    
	color_log_init(COLOR_MODE_WHOLE_LINE);       // Цветной терминал
	
	eFil_init_spiffs();                          // инит файловой системы
		
	ESP_LOGE("MEM", "Free heap at start: %d bytes", (int)esp_get_free_heap_size());

	wifi_init_nvs();
    ESP_LOGE("MEM", "Free heap at start: %d bytes", (int)esp_get_free_heap_size());

	if (esp_psram_is_initialized()) {
        printf("PSRAM is enabled, size: %d MB\n", esp_psram_get_size() / (1024 * 1024));
    } else {

    }
    eFil_profiles_load();           // читаем профайлы из файла
    eFil_init_mutex();                          // создаем мьютексы раньше(!!) чем инит дисплея
    amoled_lcd_init();                          // инит дисплея
 // amoled_set_brightness(30);
    lv_lock();
    ui_init();                                  // инит UI
    lv_unlock();
    eFil_init_ui();                             // инит доп настроек UI
    eFil_init();                                // создаем таски, очереди и прочее
	
	amoled_fade(0,80,5);
    
}