/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <widgets/img_number.h>
#include "health_ui.h"

typedef struct {
	lvgl_res_scene_t scene;
	lvgl_res_group_t grp_spo2;
} spo2_ui_res_t;

typedef struct {
	lv_obj_t *obj_bg;
	lv_obj_t *obj_spo2;

	/* lvgl resource */
	lv_img_dsc_t img_bg;
	lv_img_dsc_t img_numbers[10];

	const spo2_ui_presenter_t *presenter;
} spo2_ui_data_t;

static void _spo2_unload_resource(spo2_ui_data_t * data);
static int _spo2_paint(lv_obj_t * scr);

static const uint32_t pic_spo2_ids[] = {
	PIC_0, PIC_1, PIC_2, PIC_3, PIC_4, PIC_5, PIC_6, PIC_7, PIC_8, PIC_9,
};

static int _spo2_load_resource(spo2_ui_data_t * data, spo2_ui_res_t * tmp_res)
{
	uint32_t pic_id;
	int ret;

	/* scene */
	ret = lvgl_res_load_scene(SCENE_SPO2_VIEW, &tmp_res->scene,
			DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_SPO2_VIEW not found");
		return -ENOENT;
	}

	/* background picture */
	pic_id = PIC_BACKGROUND;
	ret = lvgl_res_load_pictures_from_scene(&tmp_res->scene, &pic_id, &data->img_bg, NULL, 1);
	if (ret < 0) {
		goto out_exit;
	}

	/* spo2 value */
	ret = lvgl_res_load_group_from_scene(&tmp_res->scene, RES_SPO2, &tmp_res->grp_spo2);
	if (ret < 0) {
		goto out_exit;
	}

	ret = lvgl_res_load_pictures_from_group(&tmp_res->grp_spo2, pic_spo2_ids,
			data->img_numbers, NULL, ARRAY_SIZE(pic_spo2_ids));

	lvgl_res_unload_group(&tmp_res->grp_spo2);
out_exit:
	lvgl_res_unload_scene(&tmp_res->scene);
	if (ret < 0) {
		_spo2_unload_resource(data);
	}

	return ret;
}

static void _spo2_unload_resource(spo2_ui_data_t * data)
{
	lvgl_res_unload_pictures(&data->img_bg, 1);
	lvgl_res_unload_pictures(data->img_numbers, ARRAY_SIZE(data->img_numbers));
}

static int _spo2_create(lv_obj_t * scr, const spo2_ui_presenter_t *presenter)
{
	spo2_ui_res_t tmp_res;

	spo2_ui_data_t * data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	if (_spo2_load_resource(data, &tmp_res)) {
		app_mem_free(data);
		return -ENOENT;
	}

	data->obj_bg = lv_img_create(scr);
	lv_img_set_src(data->obj_bg, &data->img_bg);
	lv_obj_center(data->obj_bg);

	data->obj_spo2 = img_number_create(data->obj_bg);
	lv_obj_set_pos(data->obj_spo2, tmp_res.grp_spo2.x, tmp_res.grp_spo2.y);
	lv_obj_set_size(data->obj_spo2, tmp_res.grp_spo2.width, tmp_res.grp_spo2.height);
	img_number_set_src(data->obj_spo2, data->img_numbers, 10);
	img_number_set_align(data->obj_spo2, LV_ALIGN_RIGHT_MID);

	lv_obj_set_user_data(scr, data); /* set user data */
	_spo2_paint(scr); /* first paint */
	return 0;
}

static int _spo2_paint(lv_obj_t * scr)
{
	spo2_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return -ESRCH;

	uint8_t spo2 = data->presenter ? data->presenter->get_spo2() : 99;
	img_number_set_value(data->obj_spo2, spo2, 0);

	return 0;
}

static int _spo2_delete(lv_obj_t * scr)
{
	spo2_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	_spo2_unload_resource(data);
	app_mem_free(data);
	return 0;
}

int health_spo2_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _spo2_create(scr, msg_data);
	case MSG_VIEW_PAINT:
		return _spo2_paint(scr);
	case MSG_VIEW_DELETE:
		return _spo2_delete(scr);
	default:
		return 0;
	}
}
