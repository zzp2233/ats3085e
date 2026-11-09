/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief card reader app view
*/
#include <os_common_api.h>
#include "charger.h"
#include "hotplug_manager.h"
#include <input_manager.h>
#include <ui_manager.h>
#include <app_ui.h>

enum {
	BMP_CHARG_BG,
	BMP_CHARG_ICON,
	NUM_CHARGER_IMGS,
};

static const uint32_t charger_bg_bmp_ids[] = {
	PIC_CHARGER_BG,
	PIC_CHARG_ICON,
};

typedef struct charger_view_data {
	/* resource */
	lvgl_res_scene_t res_scene;
	lv_img_dsc_t img_dsc_charger_bg[NUM_CHARGER_IMGS];
	lv_point_t pt_charger_bg[NUM_CHARGER_IMGS];
	/* lvgl object */
	lv_obj_t *cont;
	lv_obj_t *img[NUM_CHARGER_IMGS];
	const charger_view_presenter_t *presenter;
} charger_view_data_t;

void charger_view_init(void)
{
	app_ui_stack_forward(CHARGER_UI, LV_SCR_LOAD_ANIM_NONE);
	SYS_LOG_INF("charger_view_init ok\n");
}

void charger_view_deinit(void)
{
	app_ui_stack_back_home(LV_SCR_LOAD_ANIM_NONE);
	SYS_LOG_INF("ok\n");
}

static int _charger_load_resource(charger_view_data_t *data)
{
	int ret;
	/* scene */
	ret = lvgl_res_load_scene(SCENE_CHARGER_VIEW, &data->res_scene,
			DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_CHARGER_VIEW not found");
		return -ENOENT;
	}

	/* background picture */
	ret = lvgl_res_load_pictures_from_scene(&data->res_scene, charger_bg_bmp_ids, data->img_dsc_charger_bg, data->pt_charger_bg, NUM_CHARGER_IMGS);
	if (ret < 0) {
		SYS_LOG_ERR("charger scene load pictures failed");
		return ret;
	}

	return 0;
}

static void _charger_unload_resource(charger_view_data_t *data)
{
	lvgl_res_unload_pictures(data->img_dsc_charger_bg, NUM_CHARGER_IMGS);
	lvgl_res_unload_scene(&data->res_scene);
}

static void _charger_create_img_array(lv_obj_t *par, lv_obj_t **pobj, lv_point_t *pt,
										lv_img_dsc_t *img, uint32_t num)
{
	for (int i = 0; i < num; i++) {
		pobj[i] = lv_img_create(par);
		lv_img_set_src(pobj[i], &img[i]);
		lv_obj_set_pos(pobj[i], pt[i].x, pt[i].y);
	}
}

int _charger_view_create(void * scr, const charger_view_presenter_t *presenter)
{
	SYS_LOG_INF("_charger_view_create");

	charger_view_data_t * data;
	data = app_mem_malloc(sizeof(*data));
	if (!data)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	if (_charger_load_resource(data)) {
		SYS_LOG_ERR("res load failed");
		app_mem_free(data);
		return -ENOMEM;
	}

	/* background */
	data->cont = lv_obj_create(scr);
    lv_obj_set_size(data->cont, LV_PCT(100), LV_PCT(100));
	//lv_obj_set_style_bg_color(data->cont, lv_color_make(0x3b, 0x3b, 0x3b), LV_PART_MAIN);
	//lv_obj_set_style_bg_opa(data->cont, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_user_data(scr, data);

	/* charger img */
	_charger_create_img_array(data->cont, data->img, data->pt_charger_bg, data->img_dsc_charger_bg, NUM_CHARGER_IMGS);

	return 0;
}

int _charger_view_delete(void * scr)
{
	SYS_LOG_INF("_charger_view_delete");
	charger_view_data_t *data = lv_obj_get_user_data(scr);
	lv_obj_del(data->cont);
	_charger_unload_resource(data);
	app_mem_free(data);
	SYS_LOG_INF("lpower msgbox deleted\n");
	return 0;
}

int _charger_view_focus_changed(void * scr, bool focus)
{
	SYS_LOG_INF("_charger_view_focus_changed, focus:%d", focus);
	return 0;
}

int _charger_view_paint(void * scr)
{
	SYS_LOG_INF("_charger_view_paint");
	return 0;
}

int _charger_view_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _charger_view_create(scr, msg_data);
	case MSG_VIEW_DELETE:
		return _charger_view_delete(scr);
	case MSG_VIEW_FOCUS:
		return _charger_view_focus_changed(scr, 1);
	case MSG_VIEW_DEFOCUS:
		return _charger_view_focus_changed(scr, 0);
	case MSG_VIEW_PAINT:
		return _charger_view_paint(scr);
	default:
		return 0;
    return 0;
	}
}


