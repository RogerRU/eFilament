
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"


#ifndef __ROTATION_H__
    #define __ROTATION_H__

    typedef struct _lcd_bus_lock_t {
        SemaphoreHandle_t handle;
        StaticSemaphore_t buffer;
    } lcd_bus_lock_t;


    typedef struct _lcd_bus_event_t {
        EventGroupHandle_t handle;
        StaticEventGroup_t buffer;
    } lcd_bus_event_t;

    // typedef struct rotation_task_t {
    //     lcd_bus_lock_t copy_lock;
    //     lcd_bus_event_t copy_task_exit;
    //     lcd_bus_lock_t tx_color_lock;
    //     lcd_bus_event_t swap_bufs;
    //     lcd_bus_lock_t init_lock;

    //     TaskHandle_t copy_task_handle;

    //     int x_start;
    //     int y_start;
    //     int x_end;
    //     int y_end;

    //     uint16_t width;
    //     uint16_t height;

    //     uint8_t rotation: 2;
    //     uint8_t last_update: 1;
    //     uint8_t update_brightness: 1;
    //     uint8_t percent;

    //     uint8_t *active_fb;
    //     uint8_t *idle_fb;
    //     uint8_t *partial_buf;

    // } rotation_task_t;

        typedef struct rotation_task_t {
        lcd_bus_lock_t copy_lock;
        lcd_bus_event_t copy_task_exit;
        lcd_bus_lock_t tx_color_lock;
        lcd_bus_event_t swap_bufs;
        lcd_bus_lock_t init_lock;

        TaskHandle_t copy_task_handle;

        int x_start;
        int y_start;
        int x_end;
        int y_end;

        uint8_t rotation: 2;
        uint8_t last_update: 1;
        uint8_t update_brightness: 1;
        uint8_t percent;
        uint8_t *active_fb;
        uint8_t *idle_fb;
        uint8_t *partial_buf;

        lv_display_t *disp;

        esp_lcd_panel_io_handle_t io_handle;

        esp_err_t err;

    } rotation_task_t;

    void lcd_bus_event_init(lcd_bus_event_t *event);
    void lcd_bus_event_delete(lcd_bus_event_t *event);
    bool lcd_bus_event_isset(lcd_bus_event_t *event);
    void lcd_bus_event_set(lcd_bus_event_t *event);
    void lcd_bus_event_clear(lcd_bus_event_t *event);
    void lcd_bus_event_clear_from_isr(lcd_bus_event_t *event);
    bool lcd_bus_event_isset_from_isr(lcd_bus_event_t *event);
    void lcd_bus_event_set_from_isr(lcd_bus_event_t *event);
    void lcd_bus_event_wait(lcd_bus_event_t *event);

    void lcd_bus_lock_acquire(lcd_bus_lock_t *lock);
    void lcd_bus_lock_release(lcd_bus_lock_t *lock);
    void lcd_bus_lock_init(lcd_bus_lock_t *lock);
    void lcd_bus_lock_delete(lcd_bus_lock_t *lock);
    void lcd_bus_lock_release_from_isr(lcd_bus_lock_t *lock);

    void lcd_bus_copy_task(void *self_in);

    void init_rotation_task(rotation_task_t *params);

#endif