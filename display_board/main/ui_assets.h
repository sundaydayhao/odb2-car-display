#pragma once
#include "lvgl.h"
typedef enum {
    UI_DRIVE, UI_HEALTH, UI_TIRES, UI_TRIP, UI_POWER, UI_MUSIC, UI_MAP,
    UI_SETTINGS, UI_VOLUME, UI_VOICE, UI_WIFI, UI_BLUETOOTH, UI_SD, UI_UPDATE,
    UI_RADIO, UI_CAR, UI_ALBUM, UI_ASSET_COUNT
} ui_asset_t;
extern const lv_image_dsc_t ui_assets[UI_ASSET_COUNT];
/* A8 artwork can follow either theme without duplicate color bitmaps. */
static inline lv_obj_t *ui_asset_create(lv_obj_t *parent, ui_asset_t asset, int x, int y, uint32_t color)
{
    lv_obj_t *image=lv_image_create(parent);
    lv_image_set_src(image,&ui_assets[asset]);
    lv_obj_set_pos(image,x,y);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_image_recolor(image,lv_color_hex(color),0);
    lv_obj_set_style_image_recolor_opa(image,asset==UI_ALBUM ? LV_OPA_TRANSP : LV_OPA_COVER,0);
    return image;
}
