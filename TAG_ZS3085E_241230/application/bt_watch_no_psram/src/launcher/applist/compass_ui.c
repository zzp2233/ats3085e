/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <widgets/compass.h>
#include "compass_ui.h"
#ifdef CONFIG_SYS_WAKELOCK
#  include <sys_wakelock.h>
#endif
#ifdef CONFIG_SENSOR_MANAGER
#  include <sensor_manager.h>
#endif

#define COMPASS_SELF_TEST 0

typedef struct compass_view_tmp_res {
	lvgl_res_scene_t scene;
	lvgl_res_group_t grp_hole;
} compass_ui_res_t;

typedef struct compass_ui_data {
	lv_obj_t *compass;

	/* lvgl resource */
	lv_img_dsc_t img_main;
	lv_img_dsc_t img_decor;
	lv_img_dsc_t img_char[NUM_COMPASS_CHARS];

#if COMPASS_SELF_TEST
	lv_timer_t *timer;
	uint16_t bearing;
#endif

	const struct compass_ui_presenter *presenter;
} compass_ui_data_t;

static void _compass_unload_resource(compass_ui_data_t *data);

static const uint32_t pic_char_ids[NUM_COMPASS_CHARS] = {
	PIC_0, PIC_1, PIC_2, PIC_3, PIC_4, PIC_5, PIC_6, PIC_7, PIC_8, PIC_9,
	PIC_DEGREE, PIC_EAST, PIC_SOUTH, PIC_WEST, PIC_NORTH,
};

static int _load_pictures(lvgl_res_scene_t *scene, uint32_t group_id,
		const uint32_t *pic_ids, int num_ids, lv_img_dsc_t *images)
{
	lvgl_res_group_t group;
	int res = -ENOENT;

	if (lvgl_res_load_group_from_scene(scene, group_id, &group)) {
		return -ENOENT;
	}

	if (lvgl_res_load_pictures_from_group(&group, pic_ids, images, NULL, num_ids)) {
		goto out_unload_group;
	}

	res = 0;
out_unload_group:
	lvgl_res_unload_group(&group);
	return res;
}

static int _compass_load_resource(compass_ui_data_t * data, compass_ui_res_t * res)
{
	uint32_t pic_id;
	int ret;

	/* scene */
	ret = lvgl_res_load_scene(SCENE_COMPASS_VIEW, &res->scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_COMPASS_VIEW not found");
		return -ENOENT;
	}

	/* background picture */
	pic_id = PIC_MAIN;
	ret = lvgl_res_load_pictures_from_scene(&res->scene, &pic_id, &data->img_main, NULL, 1);
	if (ret < 0) {
		goto fail_exit;
	}

	pic_id = PIC_DECOR;
	ret = lvgl_res_load_pictures_from_scene(&res->scene, &pic_id, &data->img_decor, NULL, 1);
	if (ret < 0) {
		goto fail_exit;
	}

	ret = _load_pictures(&res->scene, RES_CHAR, pic_char_ids, ARRAY_SIZE(pic_char_ids), data->img_char);
	if (ret)
		goto fail_exit;

	ret = lvgl_res_load_group_from_scene(&res->scene, RES_HOLE, &res->grp_hole);
	if (ret)
		goto fail_exit;

	lvgl_res_unload_scene(&res->scene);
	lvgl_res_unload_group(&res->grp_hole);
	return 0;
fail_exit:
	lvgl_res_unload_scene(&res->scene);
	_compass_unload_resource(data);
	return -ENOENT;
}

static void _compass_unload_resource(compass_ui_data_t *data)
{
	lvgl_res_unload_pictures(&data->img_main, 1);
	lvgl_res_unload_pictures(&data->img_decor, 1);
	lvgl_res_unload_pictures(data->img_char, ARRAY_SIZE(data->img_char));
}

#if COMPASS_SELF_TEST
static void _compass_timer_cb(lv_timer_t *timer)
{
	compass_ui_data_t *data = timer->user_data;

	compass_set_bearing(data->compass, data->bearing);

	data->bearing += 5;
	if (data->bearing >= 360)
		data->bearing = 0;
}
#endif /* COMPASS_SELF_TEST */

static int _compass_create(lv_obj_t *scr, const struct compass_ui_presenter *presenter)
{
	compass_ui_res_t tmp_res;

	compass_ui_data_t *data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	if (_compass_load_resource(data, &tmp_res))
		goto fail_exit;

	lv_obj_set_style_bg_color(scr, tmp_res.scene.background, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

	data->compass = compass_create(scr);
	lv_obj_center(data->compass);

	compass_set_decor_image(data->compass, &data->img_decor);
	compass_set_char_image(data->compass, data->img_char, ARRAY_SIZE(data->img_char));
	compass_set_main_image(data->compass, &data->img_main, NULL,
			tmp_res.grp_hole.width > 1 ? tmp_res.grp_hole.width - 1 : 0);

	if (presenter)
		compass_set_bearing(data->compass, presenter->get_bearing());

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_lock(FULL_WAKE_LOCK);
#endif

#if COMPASS_SELF_TEST
	data->timer = lv_timer_create(_compass_timer_cb, 500, data);
#endif

	lv_obj_set_user_data(scr, data);
	return 0;
fail_exit:
	_compass_unload_resource(data);
	app_mem_free(data);
	return -ENOMEM;
}

static int _compass_delete(lv_obj_t *scr)
{
	compass_ui_data_t *data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

#ifdef CONFIG_SENSOR_MANAGER
	sensor_manager_disable(IN_MAG, 0);
#endif

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_unlock(FULL_WAKE_LOCK);
#endif

#if COMPASS_SELF_TEST
	lv_timer_del(data->timer);
#endif

	lv_obj_del(data->compass);
	_compass_unload_resource(data);

	app_mem_free(data);
	return 0;
}

static int _compass_paint(lv_obj_t * scr)
{
	compass_ui_data_t *data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	if (data->presenter) {
		uint16_t bearing = data->presenter->get_bearing();
		compass_set_bearing(data->compass, bearing);

		SYS_LOG_DBG("bearing %u\n", bearing);
	}

	return 0;
}

static int _compass_change_focus(lv_obj_t * scr, bool focused)
{
#ifdef CONFIG_SENSOR_MANAGER
	if (focused) {
		return sensor_manager_enable(IN_MAG, 0);
	} else {
		return sensor_manager_disable(IN_MAG, 0);
	}
#else
	return 0;
#endif
}

int compass_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _compass_create(scr, msg_data);
	case MSG_VIEW_DELETE:
		return _compass_delete(scr);
	case MSG_VIEW_PAINT:
		return _compass_paint(scr);
	case MSG_VIEW_FOCUS:
		return _compass_change_focus(scr, true);
	case MSG_VIEW_DEFOCUS:
		return _compass_change_focus(scr, false);
	default:
		return 0;
	}
}
