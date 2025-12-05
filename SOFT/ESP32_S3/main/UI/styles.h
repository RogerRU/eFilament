#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: styleBtn_Profile_SET
lv_style_t *get_style_style_btn_profile_set_MAIN_DEFAULT();
void add_style_style_btn_profile_set(lv_obj_t *obj);
void remove_style_style_btn_profile_set(lv_obj_t *obj);

// Style: styleBtn_Prof_REC
lv_style_t *get_style_style_btn_prof_rec_MAIN_DEFAULT();
void add_style_style_btn_prof_rec(lv_obj_t *obj);
void remove_style_style_btn_prof_rec(lv_obj_t *obj);

// Style: styleMsg_box_ok
lv_style_t *get_style_style_msg_box_ok_MAIN_DEFAULT();
void add_style_style_msg_box_ok(lv_obj_t *obj);
void remove_style_style_msg_box_ok(lv_obj_t *obj);

// Style: styleMsg_box_err
lv_style_t *get_style_style_msg_box_err_MAIN_DEFAULT();
void add_style_style_msg_box_err(lv_obj_t *obj);
void remove_style_style_msg_box_err(lv_obj_t *obj);



#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/