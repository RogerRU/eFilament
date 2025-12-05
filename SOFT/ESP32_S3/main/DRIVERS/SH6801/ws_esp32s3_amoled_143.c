#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "lvgl.h"
#include "esp_lcd_sh8601.h"
#include "DRIVERS/TOUCH/touch_bsp.h"
#include "ws_esp32s3_amoled_143.h"

#define GAP_X 0
#define GAP_Y 8
#define ROTATION 90

extern volatile uint32_t touch_count;
extern SemaphoreHandle_t i2c_mutex;

static const char *TAG = "WS_AMOLED";
static SemaphoreHandle_t lvgl_mux = NULL;

static const sh8601_lcd_init_cmd_t sh8601_lcd_init_cmds[] =
{
	{ 0x11, (uint8_t[]){ 0x00 }, 0, 120 },			// Exit Sleep (120ms delay)
	{ 0xC4, (uint8_t[]){ 0x80 }, 1, 10 },			// Panel Control 1
	{ 0x44, (uint8_t[]){ 0x01, 0xD1 }, 2, 0 },
	{ 0x35, (uint8_t[]){ 0x01 }, 1, 10 },
#if ROTATION == 90
	{ 0x36, (uint8_t[]){ 0x60 }, 1, 50 },			// Rotation (0x60 = 90° left)
#endif
	{ 0x63, (uint8_t[]){ 0x00 }, 1, 10 },			// HBM mode brightness (0%)
	
	{ 0x51, (uint8_t[]){ 0x00 }, 1, 0 },			// Brightness (0%)
	{ 0x29, (uint8_t[]){ 0x00 }, 0, 10 },
};

esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_display_t *disp = NULL;
static lv_indev_t *indev_touchpad = NULL;

esp_err_t amoled_set_brightness(uint8_t percent) {
	if (percent > 100) percent = 100;
	if (io_handle == NULL) {return ESP_FAIL;}

	uint8_t brightness = (uint8_t)(0xFF * percent / 100); // Конвертация % в значение 0x00-0xFF
	uint8_t cmd[] = { brightness };
	uint32_t lcd_cmd = 0x51;
	lcd_cmd &= 0xFF;
	lcd_cmd <<= 8;
	lcd_cmd |= 0x02ULL << 24;

	ESP_RETURN_ON_ERROR(
	    esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, cmd, 1),
		TAG,
		"Failed to set brightness");
	vTaskDelay(20);

//	ESP_LOGI(TAG, "Brightness set: %d%% (0x%02X)", percent, brightness);
	return ESP_OK;
}
void amoled_fade(uint8_t start, uint8_t end, uint8_t speed ) {

if (start == end ) return;	
if (!speed) speed = 5;	
	
	if (end > start) {
	
		for (size_t i = start; i < end; i++)
		{
			amoled_set_brightness(i);
			vTaskDelay(speed);
		}
	}
	else {
		for (size_t i = start; i > end; i--)
		{
			amoled_set_brightness(i);
			vTaskDelay(speed);
		}
	}	
	
}

bool amoled_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
#ifndef DEBUG
	//  lv_disp_flush_ready(disp);
#endif // DEBUG

	return false;
}

void amoled_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

	// int offsetx1 = area->x1;
	// int offsetx2 = area->x2;
	// int offsety1 = area->y1;
	// int offsety2 = area->y2;
	lv_draw_sw_rgb565_swap(px_map, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));

	// 1. Преобразование координат
	const int x_start = area->x1;
	const int x_end = area->x2 + 1; // exclusive
	int y_start = area->y1;
	int y_end = area->y2 + 1; // exclusive

	// 2. Коррекция границ
	const int max_height = AMOLED_LCD_V_RES + GAP_Y;
	y_end = (y_end > max_height) ? max_height : y_end;

	// 3. Размеры области
	const int width = x_end - x_start;
	const int height = y_end - y_start;
	// 5. Проверка на необходимость разбиения
	const bool needs_split = (y_end >= 458);
	if (!needs_split)
	{
		// 6. Стандартная отрисовка
		esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, px_map);
	}
	else
	{
		// 7. Разбиение для проблемной зоны
		const int block_sizes[] = { 8, 4, 2, 1 };
		size_t offset = 0;
		int remaining = height;

		for (int i = 0; i < 4 && remaining > 0; i++)
		{
			const int bs = block_sizes[i];
			const int blocks_count = remaining / bs;

			for (int j = 0; j < blocks_count; j++)
			{
				const int current_y = y_start + (j * bs);
				esp_lcd_panel_draw_bitmap(panel_handle,
					x_start,
					current_y,
					x_end,
					current_y + bs,
					px_map + offset);

				offset += bs * width * 2; // 2 bytes per pixel (RGB565)
			}

			remaining %= bs;
			y_start += blocks_count * bs;
		}
	}

	lv_disp_flush_ready(disp);
}

void amoled_lvgl_rounder_cb(lv_event_t *e)
{
	lv_area_t *area = lv_event_get_param(e);

	uint16_t x1 = area->x1;
	uint16_t x2 = area->x2;

	uint16_t y1 = area->y1;
	uint16_t y2 = area->y2;

	// round the start of coordinate down to the nearest 2M number
	area->x1 = (x1 >> 1) << 1;
	area->y1 = (y1 >> 1) << 1;
	// round the end of coordinate up to the nearest 2N+1 number
	area->x2 = ((x2 >> 1) << 1) + 1;
	area->y2 = ((y2 >> 1) << 1) + 1;
}

void amoled_lvgl_touch_cb(lv_indev_t *drv, lv_indev_data_t *data)
{
	uint16_t tp_x;
	uint16_t tp_y;

	uint8_t win = 0;
	if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
		win = getTouch(&tp_x, &tp_y);
		xSemaphoreGive(i2c_mutex);
	}

	if (win)
	{
#if ROTATION > 0
		// 1. Трансформация для поворота 90°
		int16_t new_x = tp_y;
		int16_t new_y = AMOLED_LCD_H_RES - tp_x - 1 - GAP_Y;

		data->point.y = new_y;
		data->point.x = new_x;

		data->point.x = LV_CLAMP(0, data->point.x, AMOLED_LCD_H_RES - 1);
		data->point.y = LV_CLAMP(0, data->point.y, AMOLED_LCD_V_RES - GAP_Y - 1);

#else
		data->point.x = tp_x;
		data->point.y = tp_y;
#endif
		data->state = LV_INDEV_STATE_PRESSED;
		touch_count = 0;
	}
	else
	{
		data->state = LV_INDEV_STATE_RELEASED;
	}
}

void amoled_increase_lvgl_tick(void *arg)
{
	lv_tick_inc(AMOLED_LVGL_TICK_PERIOD_MS); /* Tell LVGL how many milliseconds has elapsed */
}

void amoled_lvgl_port_task(void *arg)
{
	ESP_LOGI(TAG, "Starting LVGL task");
	uint32_t task_delay_ms = AMOLED_LVGL_TASK_MAX_DELAY_MS;
	while (1)
	{
		task_delay_ms = lv_timer_handler();
		if (task_delay_ms > AMOLED_LVGL_TASK_MAX_DELAY_MS)
		{
			task_delay_ms = AMOLED_LVGL_TASK_MAX_DELAY_MS;
		}
		else if (task_delay_ms < AMOLED_LVGL_TASK_MIN_DELAY_MS)
		{
			task_delay_ms = AMOLED_LVGL_TASK_MIN_DELAY_MS;
		}
		vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
	}
}
void amoled_lcd_init()
{
	ESP_LOGI("DISPLAY", "Free heap before init: %d", (int)esp_get_free_heap_size());

	ESP_LOGI(TAG, "Initialize SPI bus");
	const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(AMOLED_PIN_NUM_LCD_PCLK,
		AMOLED_PIN_NUM_LCD_DATA0,
		AMOLED_PIN_NUM_LCD_DATA1,
		AMOLED_PIN_NUM_LCD_DATA2,
		AMOLED_PIN_NUM_LCD_DATA3,
		AMOLED_LCD_H_RES * AMOLED_LCD_V_RES * LCD_BIT_PER_PIXEL / 6);

	ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

	ESP_LOGI(TAG, "Install panel IO");

	const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(AMOLED_PIN_NUM_LCD_CS,
		amoled_notify_lvgl_flush_ready,
		disp);
	sh8601_vendor_config_t vendor_config = {
		.flags = {
		.use_qspi_interface = 1,
	},
	};
	// Attach the LCD to the SPI bus
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

	const esp_lcd_panel_dev_config_t panel_config = {
		.reset_gpio_num = AMOLED_PIN_NUM_LCD_RST,
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
		.bits_per_pixel = LCD_BIT_PER_PIXEL,
		.vendor_config = &vendor_config,
	};

	vendor_config.init_cmds = sh8601_lcd_init_cmds;
	vendor_config.init_cmds_size = sizeof(sh8601_lcd_init_cmds) / sizeof(sh8601_lcd_init_cmds[0]);

	ESP_LOGI(TAG, "Install SH8601 panel driver");
	ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
	ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));

	//   ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, GAP_X,6 ));  // NOT good with LVGL
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

	lv_init();
	disp = lv_display_create(AMOLED_LCD_H_RES, AMOLED_LCD_V_RES);
	lv_display_set_user_data(disp, panel_handle);
	lv_display_set_flush_cb(disp, amoled_lvgl_flush_cb);
	lv_display_add_event_cb(disp, amoled_lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
	lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
#if ROTATION == 90

	lv_display_set_physical_resolution(disp, 466, 466 - GAP_Y); // Учитываем смещение
	lv_display_set_offset(disp, 0, GAP_Y); // Correct Y-offset

#endif
	amoled_touch_init(); // Touch initialization

	ESP_LOGI(TAG, "Initialize LVGL library");
	void *buf1 = NULL;

	void *buf2 = NULL;

	int buffer_size = (466 * 20 * 2); //sizeof(lv_color_t)
//	int mm = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
	buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
	assert(buf1);
	buf2 = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
	assert(buf2);

	// initialize LVGL draw buffers
	lv_display_set_buffers(disp, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

	ESP_LOGI(TAG, "Install LVGL tick timer");

	// Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
	const esp_timer_create_args_t lvgl_tick_timer_args = {
		.callback = &amoled_increase_lvgl_tick,
		.name = "lvgl_tick"
	};
	esp_timer_handle_t lvgl_tick_timer = NULL;
	ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, AMOLED_LVGL_TICK_PERIOD_MS * 1000));

	ESP_LOGW("TAG", " Display Initialized! ");

	lvgl_mux = xSemaphoreCreateMutex();
	assert(lvgl_mux);

	xTaskCreate(amoled_lvgl_port_task, "LVGL", AMOLED_LVGL_TASK_STACK_SIZE, NULL, AMOLED_LVGL_TASK_PRIORITY, NULL);
}

void amoled_touch_init()
{
	Touch_Init();
	indev_touchpad = lv_indev_create();

	lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev_touchpad, amoled_lvgl_touch_cb);
}