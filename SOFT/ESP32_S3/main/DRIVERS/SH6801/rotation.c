#include "rotation.h"
#include "ws_esp32s3_amoled_143.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_sh8601.h"
#include "esp_task.h"

#include <string.h>


#define EVENT_BIT_0 (1 << 0)
esp_lcd_panel_handle_t panel_handle = NULL;

void lcd_bus_event_init(lcd_bus_event_t *event)
{
    event->handle = xEventGroupCreateStatic(&event->buffer);
}


void lcd_bus_event_delete(lcd_bus_event_t *event)
{
    xEventGroupSetBits(event->handle, EVENT_BIT_0);
    vEventGroupDelete(event->handle);
}

void lcd_bus_event_wait(lcd_bus_event_t *event)
{
    xEventGroupWaitBits(event->handle, EVENT_BIT_0, pdFALSE, pdTRUE, portMAX_DELAY);
}


bool lcd_bus_event_isset(lcd_bus_event_t *event)
{
    return (bool)(xEventGroupGetBits(event->handle) & EVENT_BIT_0);
}


bool lcd_bus_event_isset_from_isr(lcd_bus_event_t *event)
{
    return (bool)(xEventGroupGetBitsFromISR(event->handle) & EVENT_BIT_0);
}


void lcd_bus_event_set(lcd_bus_event_t *event)
{
    xEventGroupSetBits(event->handle, EVENT_BIT_0);
}


void lcd_bus_event_clear(lcd_bus_event_t *event)
{
    xEventGroupClearBits(event->handle, EVENT_BIT_0);
}


void lcd_bus_event_clear_from_isr(lcd_bus_event_t *event)
{
    xEventGroupClearBitsFromISR(event->handle, EVENT_BIT_0);
}


void lcd_bus_event_set_from_isr(lcd_bus_event_t *event)
{
    xEventGroupSetBitsFromISR(event->handle, EVENT_BIT_0, pdFALSE);
}


void lcd_bus_lock_acquire(lcd_bus_lock_t *lock)
{
    xSemaphoreTake(lock->handle, portMAX_DELAY);
}


void lcd_bus_lock_release(lcd_bus_lock_t *lock)
{
    xSemaphoreGive(lock->handle);
}


void lcd_bus_lock_release_from_isr(lcd_bus_lock_t *lock)
{
    xSemaphoreGiveFromISR(lock->handle, pdFALSE);
}


void lcd_bus_lock_init(lcd_bus_lock_t *lock)
{
    lock->handle = xSemaphoreCreateBinaryStatic(&lock->buffer);
    xSemaphoreGive(lock->handle);
}


void lcd_bus_lock_delete(lcd_bus_lock_t *lock)
{
    vSemaphoreDelete(lock->handle);
}

#define RGB_BUS_ROTATION_0    (0)
#define RGB_BUS_ROTATION_90   (1)
#define RGB_BUS_ROTATION_180  (2)
#define RGB_BUS_ROTATION_270  (3)

#define _LCD_MIN(v1, v2)     (v1) <= (v2) ? (v1) : (v2)
#define _LCD_MAX(v1, v2)     (v1) >= (v2) ? (v1) : (v2)

#define _LCD_UNUSED(x)    (void)(x)


static void rotate_24bpp(uint8_t *src, uint8_t *dst, uint32_t x_start, uint32_t y_start,
                    uint32_t x_end, uint32_t y_end, uint32_t dst_width, uint32_t dst_height,
                    uint8_t rotate);

static void copy_pixels(void *dst, void *src, uint32_t x_start, uint32_t y_start,
                    uint32_t x_end, uint32_t y_end, uint32_t dst_width, uint32_t dst_height,
                    uint8_t rotate);

static bool flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    rotation_task_t *params = (rotation_task_t *)user_ctx;
    lcd_bus_event_set_from_isr(&params->swap_bufs);
    return true;
}


static void task_init_lcd(esp_lcd_panel_io_handle_t panel_io)
{
    esp_lcd_panel_io_tx_param(panel_io, 0x11, (uint8_t[]){0x00}, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_panel_io_tx_param(panel_io, 0xC4, (uint8_t[]){0x80}, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // set the dolor depth to 24bit
    //esp_lcd_panel_io_tx_param(panel_io, LCD_CMD_COLMOD, (uint8_t[]){0x77}, 1);

    esp_lcd_panel_io_tx_param(panel_io, 0x44, (uint8_t[]){0x01, 0xD1}, 2);
    esp_lcd_panel_io_tx_param(panel_io, 0x35, (uint8_t[]){0x01}, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_lcd_panel_io_tx_param(panel_io, 0x63, (uint8_t[]){0x00}, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_lcd_panel_io_tx_param(panel_io, 0x51, (uint8_t[]){0xFF}, 1);
    esp_lcd_panel_io_tx_param(panel_io, 0x29, (uint8_t[]){0x00}, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}


void lcd_bus_copy_task(void *params_in) {
    // we handle the init if the display in this task so the interrupts take place
    // on the core this task is running on.
    rotation_task_t *params = (rotation_task_t *)params_in;

    params->err = 0;

    // spi_bus_config_t buscfg = {
    //     .sclk_io_num = AMOLED_PIN_NUM_LCD_PCLK,
    //     .data0_io_num = AMOLED_PIN_NUM_LCD_DATA0,
    //     .data1_io_num = AMOLED_PIN_NUM_LCD_DATA1,
    //     .data2_io_num = AMOLED_PIN_NUM_LCD_DATA2,
    //     .data3_io_num = AMOLED_PIN_NUM_LCD_DATA3,
    //     .max_transfer_sz = (
    //         AMOLED_LCD_H_RES *
    //         AMOLED_LCD_V_RES *
    //         lv_color_format_get_size(lv_display_get_color_format(params->disp)))
    // };


    spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(AMOLED_PIN_NUM_LCD_PCLK,
                                                                 AMOLED_PIN_NUM_LCD_DATA0,
                                                                 AMOLED_PIN_NUM_LCD_DATA1,
                                                                 AMOLED_PIN_NUM_LCD_DATA2,
                                                                 AMOLED_PIN_NUM_LCD_DATA3,
                                                                 lv_color_format_get_size(lv_display_get_color_format(params->disp)));

    params ->err = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    if (params->err != 0) {
        lcd_bus_lock_release(&params->init_lock);

        return;
    }

    // esp_lcd_panel_io_spi_config_t io_config = {
    //     .cs_gpio_num = AMOLED_PIN_NUM_LCD_CS,
    //     .dc_gpio_num = -1,
    //     .spi_mode = 0,
    //     .pclk_hz = 20 * 1000 * 1000,
    //     .trans_queue_depth = 10,
    //     .on_color_trans_done = flush_ready,
    //     .user_ctx = params->disp,
    //     .lcd_cmd_bits = 8,
    //     .lcd_param_bits = 8,
    // };

    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(AMOLED_PIN_NUM_LCD_CS,
                                                                                flush_ready,
                                                                                params->disp);
    sh8601_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    // Attach the LCD to the SPI bus
    params ->err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &params->io_handle);

    if (params->err != 0) {
        lcd_bus_lock_release(&params->init_lock);

        return;
    }

const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = AMOLED_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

  //  vendor_config.init_cmds = sh8601_lcd_init_cmds;
  //  vendor_config.init_cmds_size = sizeof(sh8601_lcd_init_cmds) / sizeof(sh8601_lcd_init_cmds[0]);

 //   ESP_LOGI("LCD", "Install SH8601 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(params->io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));



    task_init_lcd(params->io_handle);

    // we only need to set the  ram address on the display a single time
    // because we are going to be doing full frame updates.
    esp_lcd_panel_io_tx_param(params->io_handle, LCD_CMD_CASET, (uint8_t[]) {
        0,
        0,
        ((AMOLED_LCD_H_RES - 1) >> 8) & 0xFF,
        (AMOLED_LCD_H_RES - 1) & 0xFF,
    }, 4);

    esp_lcd_panel_io_tx_param(params->io_handle, LCD_CMD_RASET, (uint8_t[]) {
        0,
        0,
        ((AMOLED_LCD_V_RES - 1) >> 8) & 0xFF,
        (AMOLED_LCD_V_RES - 1) & 0xFF,
    }, 4);

    uint8_t *idle_fb;
    bool last_update;

    lcd_bus_lock_acquire(&params->copy_lock);
    lcd_bus_lock_release(&params->init_lock);

    bool exit = lcd_bus_event_isset(&params->copy_task_exit);
    while (!exit) {
        lcd_bus_lock_acquire(&params->copy_lock);

        if (params->update_brightness) {
            // wait until current buffer transmit is done.
            lcd_bus_event_wait(&params->swap_bufs);
            params->update_brightness = 0;
            if (params->percent > 100) params->percent = 100;

            esp_lcd_panel_io_tx_param(params->io_handle, 0x02005100, (uint8_t[]){0xFF * params->percent / 100}, 1);
            vTaskDelay(20);
            exit = lcd_bus_event_isset(&params->copy_task_exit);
            continue;
        }

        last_update = params->last_update;

        idle_fb = params->idle_fb;

        copy_pixels(
            (void *)idle_fb, (void *)params->partial_buf,
            params->x_start, params->y_start,
            params->x_end, params->y_end,
            AMOLED_LCD_H_RES, AMOLED_LCD_V_RES,
            params->rotation);

        lcd_bus_lock_release(&params->tx_color_lock);
        lv_disp_flush_ready(params->disp);

        // if it is the last update in a run of updates
        // then flush the buffer to the display
        if (last_update) {
            // we want to make sure that the buffer has been fully written
            // before flip flopping the sending buffers
            lcd_bus_event_wait(&params->swap_bufs);
            size_t size = AMOLED_LCD_H_RES * AMOLED_LCD_V_RES * lv_color_format_get_size(lv_display_get_color_format(params->disp));
            esp_lcd_panel_io_tx_color(params->io_handle, LCD_CMD_RAMWR, idle_fb, size);
            lcd_bus_event_clear(&params->swap_bufs);
            params->idle_fb = params->active_fb;
            params->active_fb = idle_fb;

            // we need to keep the 2 full buffers in sync so we have to copy
            // the data from the buffer that is transmitting to the buffer that
            // is now idle
            memcpy(params->idle_fb, params->active_fb, size);
        }

        exit = lcd_bus_event_isset(&params->copy_task_exit);
    }

}

__attribute__((always_inline))
static inline void copy_24bpp(uint8_t *from, uint8_t *to)
{
    *to++ = *from++;
    *to++ = *from++;
    *to++ = *from++;
}


void copy_pixels(void *dst, void *src, uint32_t x_start, uint32_t y_start,
        uint32_t x_end, uint32_t y_end, uint32_t dst_width, uint32_t dst_height,
        uint8_t rotate)
{
    if (rotate == RGB_BUS_ROTATION_0) {
        rotate_24bpp(src, dst, _LCD_MIN(x_start, dst_width), _LCD_MIN(y_start, dst_height),
                _LCD_MIN(x_end, dst_width), _LCD_MIN(y_end, dst_height),
                dst_width, dst_height, rotate);
    } else {
        y_end += 1; // removes black lines between blocks
        if (rotate == RGB_BUS_ROTATION_90 || rotate == RGB_BUS_ROTATION_270) {
            x_start = _LCD_MIN(x_start, dst_height);
            x_end = _LCD_MIN(x_end, dst_height);
            y_start = _LCD_MIN(y_start, dst_width);
            y_end = _LCD_MIN(y_end, dst_width);
        } else {
            x_start = _LCD_MIN(x_start, dst_width);
            x_end = _LCD_MIN(x_end, dst_width);
            y_start = _LCD_MIN(y_start, dst_height);
            y_end = _LCD_MIN(y_end, dst_height);
        }

        rotate_24bpp(src, dst, x_start, y_start, x_end, y_end, dst_width, dst_height, rotate);
    }
}


void rotate_24bpp(uint8_t *src, uint8_t *dst, uint32_t x_start, uint32_t y_start,
                uint32_t x_end, uint32_t y_end, uint32_t dst_width, uint32_t dst_height,
                uint8_t rotate)
{
    uint32_t i;
    uint32_t j;

    uint32_t src_bytes_per_line = (x_end - x_start + 1) * 3;
    uint32_t offset = y_start * src_bytes_per_line + x_start * 3;
    uint32_t bytes_per_pixel = 3;

    switch (rotate) {
        case RGB_BUS_ROTATION_0:
            dst += ((y_start * dst_width + x_start) * bytes_per_pixel);
            if(x_start == 0 && x_end == (dst_width - 1)) {
                memcpy(dst, src, dst_width * (y_end - y_start + 1) * bytes_per_pixel);
            } else {
                uint32_t src_bytes_per_line = (x_end - x_start + 1) * bytes_per_pixel;
                uint32_t dst_bytes_per_line = dst_width * bytes_per_pixel;
                for(uint32_t y = y_start; y < y_end; y++) {
                    memcpy(dst, src, src_bytes_per_line);
                    dst += dst_bytes_per_line;
                    src += src_bytes_per_line;
                }
            }
            break;

        case RGB_BUS_ROTATION_90:
            for (uint32_t y = y_start; y < y_end; y++) {
                for (uint32_t x = x_start; x < x_end; x++) {
                    i = y * src_bytes_per_line + x * 3 - offset;
                    j = ((dst_height - 1 - x) * dst_width + y) * 3;
                    copy_24bpp(src + i, dst + j);
                }
            }
            break;

        // MIRROR_X MIRROR_Y
        case RGB_BUS_ROTATION_180:
            _LCD_UNUSED(j);
            _LCD_UNUSED(src_bytes_per_line);
            _LCD_UNUSED(offset);

            for (int y = y_start; y < y_end; y++) {
                i = ((dst_height - 1 - y) * dst_width + (dst_width - 1 - x_start)) * 3;
                for (size_t x = x_start; x < x_end; x++) {
                    copy_24bpp(src, dst + i);
                    src += 3;
                    i -= 3;
                }
                src++;
            }
            break;

        // SWAP_XY   MIRROR_X
        case RGB_BUS_ROTATION_270:
            for (uint32_t y = y_start; y < y_end; y++) {
                for (uint32_t x = x_start; x < x_end; x++) {
                    i = y * src_bytes_per_line + x * 3 - offset;
                    j = (x * dst_width + dst_width - 1 - y) * 3;
                    copy_24bpp(src + i, dst + j);
                }
            }
            break;

        default:
            _LCD_UNUSED(i);
            _LCD_UNUSED(j);
            _LCD_UNUSED(src_bytes_per_line);
            _LCD_UNUSED(offset);
            break;
    }
}
