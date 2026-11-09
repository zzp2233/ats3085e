/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <widgets/img_number.h>
#include <widgets/anim_img.h>
#include "health_ui.h"

typedef struct {
	lvgl_res_group_t grp_bp_low;
	lvgl_res_group_t grp_bp_high;
} bp_ui_res_t;

typedef struct {
	lv_obj_t *obj_bg;
	lv_obj_t *obj_anim;
	lv_obj_t *obj_bp_low;
	lv_obj_t *obj_bp_high;

	/* lvgl resource */
	lv_img_dsc_t img_bg;
	lv_img_dsc_t img_numbers[10];

	lvgl_res_picregion_t prg_anim;
	lvgl_res_scene_t scene;

	const bp_ui_presenter_t *presenter;
} bp_ui_data_t;

static void _bp_unload_resource(bp_ui_data_t * data);
static int _bp_paint(lv_obj_t * scr);

static const uint32_t pic_bp_ids[] = {
	PIC_0, PIC_1, PIC_2, PIC_3, PIC_4, PIC_5, PIC_6, PIC_7, PIC_8, PIC_9,
};

static int _bp_load_resource(bp_ui_data_t * data, bp_ui_res_t * tmp_res)
{
	uint32_t pic_id;
	int ret;

	/* scene */
	ret = lvgl_res_load_scene(SCENE_BP_VIEW, &data->scene,
				DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_BP_VIEW not found");
		return -ENOENT;
	}

	/* background picture */
	pic_id = PIC_BACKGROUND;
	ret = lvgl_res_load_pictures_from_scene(&data->scene, &pic_id, &data->img_bg, NULL, 1);
	if (ret < 0) {
		goto out_exit;
	}

	/* animation */
	ret = lvgl_res_load_picregion_from_scene(&data->scene, RES_ANIMATION, &data->prg_anim);
	if (ret < 0) {
		goto out_exit;
	}

	/* bp low */
	ret = lvgl_res_load_group_from_scene(&data->scene, RES_BP_LOW, &tmp_res->grp_bp_low);
	if (ret < 0) {
		goto out_exit;
	}

	lvgl_res_unload_group(&tmp_res->grp_bp_low);

	/* bp high */
	ret = lvgl_res_load_group_from_scene(&data->scene, RES_BP_HIGH, &tmp_res->grp_bp_high);
	if (ret < 0) {
		goto out_exit;
	}

	ret = lvgl_res_load_pictures_from_group(&tmp_res->grp_bp_high, pic_bp_ids,
			data->img_numbers, NULL, ARRAY_SIZE(pic_bp_ids));

	lvgl_res_unload_group(&tmp_res->grp_bp_high);

out_exit:
	if (ret < 0) {
		_bp_unload_resource(data);
	}

	return ret;
}

static void _bp_unload_resource(bp_ui_data_t * data)
{
	lvgl_res_unload_pictures(&data->img_bg, 1);
	lvgl_res_unload_pictures(data->img_numbers, ARRAY_SIZE(data->img_numbers));
	lvgl_res_unload_picregion(&data->prg_anim);
	lvgl_res_unload_scene(&data->scene);
}

static int _bp_create(lv_obj_t * scr, const bp_ui_presenter_t *presenter)
{
	bp_ui_res_t tmp_res;

	bp_ui_data_t * data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	if (_bp_load_resource(data, &tmp_res)) {
		app_mem_free(data);
		return -ENOENT;
	}

	data->obj_bg = lv_img_create(scr);
	lv_img_set_src(data->obj_bg, &data->img_bg);
	lv_obj_center(data->obj_bg);

	data->obj_anim = anim_img_create(data->obj_bg);
	lv_obj_set_pos(data->obj_anim, data->prg_anim.x, data->prg_anim.y);
	lv_obj_set_size(data->obj_anim, data->prg_anim.width, data->prg_anim.height);
	anim_img_set_src_picregion(data->obj_anim, SCENE_BP_VIEW, &data->prg_anim);
	anim_img_set_duration(data->obj_anim, 16, 128 * data->prg_anim.frames);
	anim_img_set_repeat(data->obj_anim, 0, LV_ANIM_REPEAT_INFINITE);

	data->obj_bp_low = img_number_create(data->obj_bg);
	lv_obj_set_pos(data->obj_bp_low, tmp_res.grp_bp_low.x, tmp_res.grp_bp_low.y);
	lv_obj_set_size(data->obj_bp_low, tmp_res.grp_bp_low.width, tmp_res.grp_bp_low.height);
	img_number_set_src(data->obj_bp_low, data->img_numbers, 10);
	img_number_set_align(data->obj_bp_low, LV_ALIGN_LEFT_MID);

	data->obj_bp_high = img_number_create(data->obj_bg);
	lv_obj_set_pos(data->obj_bp_high, tmp_res.grp_bp_high.x, tmp_res.grp_bp_high.y);
	lv_obj_set_size(data->obj_bp_high, tmp_res.grp_bp_high.width, tmp_res.grp_bp_high.height);
	img_number_set_src(data->obj_bp_high, data->img_numbers, 10);
	img_number_set_align(data->obj_bp_high, LV_ALIGN_LEFT_MID);

	lv_obj_set_user_data(scr, data); /* set user data */
	_bp_paint(scr); /* first paint */
	return 0;
}

static int _bp_paint(lv_obj_t * scr)
{
	bp_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return -ESRCH;

	uint8_t bp_low = 70;
	uint8_t bp_high = 120;

	if (data->presenter)
		data->presenter->get_bp_range(&bp_low, &bp_high);

	img_number_set_value(data->obj_bp_low, bp_low, 0);
	img_number_set_value(data->obj_bp_high, bp_high, 0);
	return 0;
}

static int _bp_focus(lv_obj_t * scr, bool focused)
{
	bp_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return -ESRCH;

	if (data->obj_anim) {
		if (focused) {
			anim_img_start(data->obj_anim, true);
		} else {
			anim_img_stop(data->obj_anim);
		}
	}

	return 0;
}

static int _bp_delete(lv_obj_t * scr)
{
	bp_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	lv_obj_clean(scr);
	_bp_unload_resource(data);
	app_mem_free(data);
	return 0;
}

int health_bp_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _bp_create(scr, msg_data);
	case MSG_VIEW_PAINT:
		return _bp_paint(scr);
	case MSG_VIEW_FOCUS:
		return _bp_focus(scr, true);
	case MSG_VIEW_DEFOCUS:
		return _bp_focus(scr, false);
	case MSG_VIEW_DELETE:
		return _bp_delete(scr);
	default:
		return 0;
	}
}
