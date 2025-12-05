#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *test_page;
    lv_obj_t *profile_sel;
    lv_obj_t *ssaver;
    lv_obj_t *calibrate;
    lv_obj_t *txt_wifi;
    lv_obj_t *cont_profile;
    lv_obj_t *txt_filament_type;
    lv_obj_t *txt_filament_vendor_pack;
    lv_obj_t *arc_main;
    lv_obj_t *cont_perc;
    lv_obj_t *txt_prc_dec;
    lv_obj_t *txt_prc_fl;
    lv_obj_t *txt_cap_percent;
    lv_obj_t *cont_info;
    lv_obj_t *txt_cap_wgt;
    lv_obj_t *txt_info_wgt;
    lv_obj_t *txt_cap_lgt;
    lv_obj_t *txt_info_lgt;
    lv_obj_t *txt_cap_total;
    lv_obj_t *txt_info_total;
    lv_obj_t *cnt_test;
    lv_obj_t *txt_coord2;
    lv_obj_t *cont_time;
    lv_obj_t *txt_time_h;
    lv_obj_t *txt_time_sep;
    lv_obj_t *txt_time_m;
    lv_obj_t *tst_arc;
    lv_obj_t *txt_coord;
    lv_obj_t *panel_touch;
    lv_obj_t *roll_profile;
    lv_obj_t *cont_set_rec;
    lv_obj_t *btn_prof_set;
    lv_obj_t *txt_btn_set;
    lv_obj_t *btn_prof_rec;
    lv_obj_t *txt_btn_rec;
    lv_obj_t *chk_fil_type;
    lv_obj_t *chk_fil_vendor;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *arc_rec;
    lv_obj_t *txt_ss_h;
    lv_obj_t *txt_ss_sep;
    lv_obj_t *txt_ss_m;
    lv_obj_t *txt_ss_date;
    lv_obj_t *cont_ss_prc;
    lv_obj_t *txt_ss_prc_fl;
    lv_obj_t *txt_ss_prc_dec;
    lv_obj_t *txt_ss_prc_label;
    lv_obj_t *cont_ss;
    lv_obj_t *arc_calibrate;
    lv_obj_t *txt_calib_header;
    lv_obj_t *txt_calib_target;
    lv_obj_t *txt_calib_prc;
    lv_obj_t *area_txt_calib_info;
    lv_obj_t *txt_calib_ref_wgt;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_TEST_PAGE = 2,
    SCREEN_ID_PROFILE_SEL = 3,
    SCREEN_ID_SSAVER = 4,
    SCREEN_ID_CALIBRATE = 5,
};

void create_screen_main();
void tick_screen_main();

void create_screen_test_page();
void tick_screen_test_page();

void create_screen_profile_sel();
void tick_screen_profile_sel();

void create_screen_ssaver();
void tick_screen_ssaver();

void create_screen_calibrate();
void tick_screen_calibrate();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/