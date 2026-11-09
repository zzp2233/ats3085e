/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "desktop_ui.h"
#ifdef CONFIG_SENSOR_MANAGER
#include <sensor_manager.h>
#endif

typedef struct {
	lvgl_res_scene_t scene;
	lvgl_res_string_t text;
	lv_point_t pt_bg;
} heart_ui_res_t;

typedef struct {
	/* lvgl object */
	lv_obj_t *img;
	lv_obj_t *lbl;

	/* lvgl resource */
	lv_img_dsc_t img_bg;
	lv_font_t font;

	const desktop_ui_presenter_t * presenter;
} heart_ui_data_t;

static int _heart_paint(lv_obj_t * scr);

static int _heart_load_resource(heart_ui_data_t * data, heart_ui_res_t * res)
{
	int ret = lvgl_res_load_scene(SCENE_HEART_VIEW, &res->scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) return ret;

	uint32_t res_id = PIC_BG;
	lvgl_res_load_pictures_from_scene(&res->scene, &res_id, &data->img_bg, &res->pt_bg, 1);

	res_id = STR_HRATE;
	lvgl_res_load_strings_from_scene(&res->scene, &res_id, &res->text, 1);
	lvgl_res_unload_strings(&res->text, 1);

	ret = LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_NORMAL);

	lvgl_res_unload_scene(&res->scene);
	return ret;
}

static int _heart_create(lv_obj_t * scr, const desktop_ui_presenter_t * presenter)
{
	heart_ui_res_t tmp_res;

	heart_ui_data_t * data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	if (_heart_load_resource(data, &tmp_res)) {
		app_mem_free(data);
		return -ENOENT;
	}

	data->img = lv_img_create(scr);
	lv_img_set_src(data->img, &data->img_bg);
	//lv_obj_set_pos(data->img, tmp_res.pt_bg.x, tmp_res.pt_bg.y);
	lv_obj_center(data->img);

	data->lbl = lv_label_create(data->img);
	lv_label_set_text_fmt(data->lbl, "%02d", 80); /* default value */
	lv_obj_set_pos(data->lbl, tmp_res.text.x, tmp_res.text.y);
	lv_obj_set_style_text_font(data->lbl, &data->font, LV_PART_MAIN);
	lv_obj_set_style_text_color(data->lbl, tmp_res.text.color, LV_PART_MAIN);

	lv_obj_set_user_data(scr, data);
	_heart_paint(scr); /* initialize value */
	return 0;
}

static int _heart_paint(lv_obj_t * scr)
{
	heart_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return -EINVAL;

	const desktop_ui_presenter_t * presenter = data->presenter;
	if (presenter == NULL)
		return -EINVAL;

	lv_label_set_text_fmt(data->lbl, "%02d", presenter->get_heart_rate());
	return 0;
}

static int _heart_delete(lv_obj_t * scr)
{
	heart_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	lv_obj_clean(scr);

	lvgl_res_unload_pictures(&data->img_bg, 1);
	LVGL_FONT_CLOSE(&data->font);

	app_mem_free(data);
	return 0;
}

static int _heart_focus(lv_obj_t * scr, bool focused)
{
#ifdef CONFIG_SENSOR_MANAGER	
	if (focused) {
		sensor_manager_enable(IN_HEARTRATE, 0);
	} else {
		sensor_manager_disable(IN_HEARTRATE, 0);
	}
#endif

	return 0;
}

int heart_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _heart_create(scr, msg_data);
	case MSG_VIEW_PAINT:
		return _heart_paint(scr);
	case MSG_VIEW_FOCUS:
		return _heart_focus(scr, true);
	case MSG_VIEW_DEFOCUS:
		return _heart_focus(scr, false);
	case MSG_VIEW_DELETE:
#ifdef CONFIG_SENSOR_MANAGER
		sensor_manager_disable(IN_HEARTRATE, 0);
#endif
		return _heart_delete(scr);
	default:
		return 0;
	}
}
