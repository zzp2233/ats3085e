/*
 * Copyright (c) 2022 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file gps view
 */

#include <assert.h>
#include <sys_wakelock.h>
#include "vibration_ui.h"

#define VIB_PWM_FREQ 100

enum {
	TXT_TITLE = 0,
	TXT_LOW,
	TXT_MID,
	TXT_HIG,
	NUM_TXTS,
};

const static uint32_t _txt_ids[] = {
	STR_VIB_TITLE,
	STR_VIB_LOW,
	STR_VIB_MID,
	STR_VIB_HIG,
};

enum {
	BTN_TOP = 0,
	BTN_LOW,
	BTN_MID,
	BTN_HIG,
	BTN_BUTTOM,
	NUM_BTN,
};

typedef struct btn_info {
	uint8_t btn_id;
	uint8_t level_id;
}t_btn_info;

const static t_btn_info btn_info_list[] = {
	{BTN_LOW, LEVEL_1},
	{BTN_MID, LEVEL_2},
	{BTN_HIG, LEVEL_3},
};

typedef struct vibration_view_data {
	/* lvgl object */
	lv_obj_t *vib_bmp;
	lv_obj_t *vib_title;
	lv_obj_t *btn[NUM_BTN];
	/* ui-editor resource */
	lvgl_res_scene_t res_scene;
	lv_point_t pt_txt[NUM_TXTS];
	lvgl_res_string_t res_txt[NUM_TXTS];

	/* lvgl resource */
	lv_style_t style_txt[NUM_TXTS];
	lv_font_t font;

	const vibration_view_presenter_t *presenter;
} vibration_view_data_t;


static uint8_t btn_get_level(uint8_t btn_id)
{
	int i;
	for(i=0; i<sizeof(btn_info_list); i++) {
		if(btn_info_list[i].btn_id == btn_id)
			return btn_info_list[i].level_id;
	}
	return 0;
}

static void _cvt_txt_array(lv_point_t *pt, lv_style_t *sty, lv_font_t *font, lvgl_res_string_t *txt, uint32_t num)
{
	int i;

	for (i = 0; i < num; i++) {
		pt[i].x = txt[i].x;
		pt[i].y = txt[i].y;

		lv_style_init(&sty[i]);
		lv_style_set_text_font(&sty[i], font);
		lv_style_set_text_color(&sty[i], txt[i].color);
	}
}

static int _load_resource(vibration_view_data_t *data)
{
	int ret;

	/* load scene */
	ret = lvgl_res_load_scene(SCENE_VIB, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_VIB not found");
		return -ENOENT;
	}


	/* open font */
	if (LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_NORMAL) < 0) {
		SYS_LOG_ERR("font not found");
		return -ENOENT;
	}

	/* load string */
	lvgl_res_load_strings_from_scene(&data->res_scene, _txt_ids, data->res_txt, NUM_TXTS);
	_cvt_txt_array(data->pt_txt, data->style_txt, &data->font, data->res_txt, NUM_TXTS);

	SYS_LOG_INF("load resource succeed");

	return 0;
}

static void _unload_resource(vibration_view_data_t *data)
{
	lvgl_res_unload_strings(data->res_txt, NUM_TXTS);
	lvgl_res_unload_scene(&data->res_scene);

	LVGL_FONT_CLOSE(&data->font);
}



static void scroll_event_cb(lv_event_t * e)
{
	vibration_view_data_t *data = lv_event_get_user_data(e);
    lv_obj_t * cont = lv_event_get_target(e);
	
    lv_area_t cont_a;
    lv_obj_get_coords(cont, &cont_a);
	
    lv_coord_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

	//SYS_LOG_INF("scroll_event_cb in, cont_y_center:0x%x", cont_y_center);
    uint32_t i,target_i = 0;
    uint32_t child_cnt = lv_obj_get_child_cnt(cont);

	if(child_cnt <= 0)
	{
		return;
	}

	lv_coord_t diff_y;
	lv_coord_t diff_y_min = LV_COORD_MAX;
	lv_coord_t child_height = 0;

    for(i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        lv_area_t child_a;
        lv_obj_get_coords(child, &child_a);
		child_height = lv_area_get_height(&child_a);
        lv_coord_t child_y_center = child_a.y1 + child_height/ 2;
        diff_y = child_y_center - cont_y_center;
        diff_y = LV_ABS(diff_y);

		//SYS_LOG_INF("[%d] diff_y:%d", i, diff_y);

		if(diff_y < diff_y_min)
		{
			diff_y_min = diff_y;
			target_i = i;
		}
    }

	lv_obj_scroll_to_y(cont, (target_i-BTN_LOW)*child_height, LV_ANIM_ON);

	if (data->presenter)
		data->presenter->set_level(btn_get_level(target_i));
}


/**
 * Translate the object as they scroll
 */
static void vibration_select_area_set(vibration_view_data_t *data)
{	
	/* choosed area */
	lv_obj_t * choose_area = lv_obj_create(data->vib_bmp);
	lv_obj_set_pos(choose_area, 0, 100);
    lv_obj_set_size(choose_area, lv_pct(90), lv_pct(20));
	lv_obj_set_style_radius(choose_area, 30, LV_PART_MAIN);
	lv_obj_set_style_bg_color(choose_area, lv_color_make(0xff, 0xff, 0xff), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(choose_area, LV_OPA_10, LV_PART_MAIN);
    lv_obj_center(choose_area);
	
	/* select area */
    lv_obj_t * cont = lv_obj_create(data->vib_bmp);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(60));
	lv_obj_set_style_bg_color(cont, lv_color_make(0xff, 0xff, 0xff), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(cont, LV_OPA_0, LV_PART_MAIN);
    lv_obj_center(cont);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL_END, data);
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);	    

	/* buttons */
	int i,j;
	for(i = 0;i < NUM_BTN;i++) {
		data->btn[i] = lv_btn_create(cont);
		lv_obj_set_size(data->btn[i], lv_pct(100), lv_pct(33));
		lv_obj_center(data->btn[i]);
	}

    for(i = TXT_LOW, j = BTN_LOW;i <= TXT_HIG;i++, j++) {		
		lv_obj_t * label = lv_label_create(data->btn[j]);
		lv_label_set_text(label, data->res_txt[i].txt);
		lv_obj_add_style(label, &data->style_txt[i], LV_PART_MAIN);
		lv_obj_center(label);
    }	

    if (data->presenter) {
		uint8_t level = data->presenter->get_level();
		lv_obj_scroll_to_view(data->btn[BTN_LOW+level+1], LV_ANIM_OFF);
	}
}


static int _vibration_view_create(lv_obj_t *scr, const vibration_view_presenter_t *presenter)
{
	vibration_view_data_t *data;

	data = app_mem_malloc(sizeof(*data));
	if (!data)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	/* load resource */
	if (_load_resource(data)) {
		app_mem_free(data);
		return -ENOENT;
	}
	
	/* background */
	data->vib_bmp = lv_obj_create(scr);
	lv_obj_set_size(data->vib_bmp, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(data->vib_bmp, lv_color_make(0, 0, 0), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(data->vib_bmp, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_user_data(scr, data);

	/* show title */
	data->vib_title = lv_label_create(data->vib_bmp);
	lv_obj_set_pos(data->vib_title, data->pt_txt[0].x, data->pt_txt[0].y);
	lv_label_set_text(data->vib_title, data->res_txt[TXT_TITLE].txt);
	lv_obj_add_style(data->vib_title, data->style_txt, LV_PART_MAIN);

	/* select area */
	vibration_select_area_set(data);

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_lock(FULL_WAKE_LOCK);
#endif

	return 0;
}

static int _vibration_view_paint(lv_obj_t *scr)
{
	vibration_view_data_t *data = lv_obj_get_user_data(scr);

	const vibration_view_presenter_t *presenter = data->presenter;

	if (data) {
		SYS_LOG_INF("_vibration_view_paint presenter %p",presenter);
	}

	return 0;
}

static int _vibration_view_delete(lv_obj_t *scr)
{
	vibration_view_data_t *data = lv_obj_get_user_data(scr);
	int i;

	if (data == NULL)
		return 0;

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_unlock(FULL_WAKE_LOCK);
#endif

	lv_obj_del(data->vib_title);
	for (i = 0; i < NUM_TXTS; i++) {
		lv_style_reset(&data->style_txt[i]);
	}
	lv_obj_del(data->vib_bmp);
	
	_unload_resource(data);

	app_mem_free(data);
	return 0;
}

static int _vibration_view_focus_changed(lv_obj_t * scr, bool focused)
{
	vibration_view_data_t *data = lv_obj_get_user_data(scr);
	if (focused)  {
		if (data->presenter) {
			data->presenter->set_freq(VIB_PWM_FREQ);
			data->presenter->start();
		}
	} else {
		if (data->presenter)
			data->presenter->stop();
	}

	return 0;
}

int vibration_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _vibration_view_create(scr, msg_data);
	case MSG_VIEW_DELETE:
		return _vibration_view_delete(scr);
	case MSG_VIEW_FOCUS:
		return _vibration_view_focus_changed(scr, 1);
	case MSG_VIEW_DEFOCUS:
		return _vibration_view_focus_changed(scr, 0);
	case MSG_VIEW_PAINT:
		return _vibration_view_paint(scr);
	default:
		return 0;
	}
}
