#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: styleBtn_Profile_SET
//

void init_style_style_btn_profile_set_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_align(style, LV_ALIGN_LEFT_MID);
    lv_style_set_bg_opa(style, 0);
    lv_style_set_border_color(style, lv_color_hex(0xff5f641a));
    lv_style_set_border_width(style, 3);
};

lv_style_t *get_style_style_btn_profile_set_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_btn_profile_set_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_btn_profile_set(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_btn_profile_set_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_btn_profile_set(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_btn_profile_set_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: styleBtn_Prof_REC
//

void init_style_style_btn_prof_rec_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_align(style, LV_ALIGN_LEFT_MID);
    lv_style_set_bg_opa(style, 255);
    lv_style_set_border_color(style, lv_color_hex(0xffb73333));
    lv_style_set_border_width(style, 3);
    lv_style_set_bg_color(style, lv_color_hex(0xff7c3431));
};

lv_style_t *get_style_style_btn_prof_rec_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_btn_prof_rec_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_btn_prof_rec(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_btn_prof_rec_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_btn_prof_rec(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_btn_prof_rec_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: styleMsg_box_ok
//

void init_style_style_msg_box_ok_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &lv_font_montserrat_30);
    lv_style_set_text_color(style, lv_color_hex(0xfffafafa));
    lv_style_set_bg_color(style, lv_color_hex(0xff174111));
    lv_style_set_border_color(style, lv_color_hex(0xff56bb42));
    lv_style_set_min_width(style, 380);
    lv_style_set_min_height(style, 100);
    lv_style_set_text_align(style, LV_TEXT_ALIGN_CENTER);
    lv_style_set_align(style, LV_ALIGN_CENTER);
    lv_style_set_pad_top(style, 35);
    lv_style_set_pad_bottom(style, 35);
};

lv_style_t *get_style_style_msg_box_ok_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_msg_box_ok_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_msg_box_ok(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_msg_box_ok_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_msg_box_ok(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_msg_box_ok_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: styleMsg_box_err
//

void init_style_style_msg_box_err_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &lv_font_montserrat_30);
    lv_style_set_text_color(style, lv_color_hex(0xfffafafa));
    lv_style_set_bg_color(style, lv_color_hex(0xffa94040));
    lv_style_set_border_color(style, lv_color_hex(0xffde7e7e));
    lv_style_set_min_width(style, 380);
    lv_style_set_min_height(style, 100);
    lv_style_set_text_align(style, LV_TEXT_ALIGN_CENTER);
    lv_style_set_align(style, LV_ALIGN_CENTER);
    lv_style_set_pad_top(style, 35);
    lv_style_set_pad_bottom(style, 35);
};

lv_style_t *get_style_style_msg_box_err_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_style_msg_box_err_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_style_msg_box_err(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_style_msg_box_err_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_style_msg_box_err(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_style_msg_box_err_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_style_btn_profile_set,
        add_style_style_btn_prof_rec,
        add_style_style_msg_box_ok,
        add_style_style_msg_box_err,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_style_btn_profile_set,
        remove_style_style_btn_prof_rec,
        remove_style_style_msg_box_ok,
        remove_style_style_msg_box_err,
    };
    remove_style_funcs[styleIndex](obj);
}

