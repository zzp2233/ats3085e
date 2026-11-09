/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file aod_clock_ui.c
 */

/*********************
 *      INCLUDES
 *********************/
#include <assert.h>
#include <mem_manager.h>
#include <sys_manager.h>
#include <display/display_composer.h>
#include <widgets/watch_clock.h>
#include "desktop_ui.h"
#include "../clock_selector/clock_selector.h"

#ifdef CONFIG_SYS_WAKELOCK
#  include <sys_wakelock.h>
#endif

#ifdef SCENE_AOD_CLOCK_VIEW

/**********************
 *      TYPEDEFS
 **********************/
typedef struct aod_clock_ui_res {
	lvgl_res_scene_t scene;
	lvgl_res_group_t grp_hour;
	lvgl_res_group_t grp_minute;

	lv_point_t pt_colon;
} aod_clock_ui_res_t;

typedef struct aod_clock_ui_data {
	bool is_digital;
	lv_obj_t *obj_clock;

	/* update period */
	uint32_t period;

	/* lvgl resource */
	lv_img_dsc_t img_bg;
	lv_img_dsc_t img_tm_colon; /* : between hour and minute */
	lv_img_dsc_t img_tm_hour[10]; /* 0~9 */
	lv_img_dsc_t img_tm_min[10];  /* 0~9 */

	const clock_dsc_t * clock_dsc;
	const desktop_ui_presenter_t * presenter;
} aod_clock_ui_data_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int _aod_clock_paint(lv_obj_t * scr, bool first);

/**********************
 *  STATIC VARIABLES
 **********************/
static const uint32_t pic_common_ids[] = {
	PIC_0, PIC_1, PIC_2, PIC_3, PIC_4, PIC_5, PIC_6, PIC_7, PIC_8, PIC_9,
};

static bool s_aod_inflated = false;

/**********************
 *   STATIC FUNCTIONS
 **********************/
static int _load_pictures(lvgl_res_scene_t *scene, uint32_t group_id,
		lvgl_res_group_t *group, const uint32_t *pic_ids,
		int num_ids, lv_img_dsc_t *images)
{
	int res;

	if (lvgl_res_load_group_from_scene(scene, group_id, group)) {
		return -ENOENT;
	}

	res = lvgl_res_load_pictures_from_group(group, pic_ids, images, NULL, num_ids);
	lvgl_res_unload_group(group);

	return res;
}

static int _aod_clock_load_resource(aod_clock_ui_data_t *data, aod_clock_ui_res_t *tmp_res)
{
	uint32_t pic_id;
	int ret;

	/* scene */
	ret = lvgl_res_load_scene(SCENE_AOD_CLOCK_VIEW, &tmp_res->scene,
			DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("scene 0x%x not found", SCENE_AOD_CLOCK_VIEW);
		return -ENOENT;
	}

	/* test scene is digital or not */
	ret = lvgl_res_load_group_from_scene(&tmp_res->scene, RES_DIGITAL_ID, &tmp_res->grp_hour);
	if (ret == 0) {
		data->is_digital = true;
		lvgl_res_unload_group(&tmp_res->grp_hour);
	}

	/* background picture */
	pic_id = PIC_BACKGROUND;
	ret = lvgl_res_load_pictures_from_scene(&tmp_res->scene, &pic_id, &data->img_bg, NULL, 1);

	/* hour */
	ret = _load_pictures(&tmp_res->scene, RES_TM_HOUR, &tmp_res->grp_hour,
			pic_common_ids, data->is_digital ? 10 : 1, data->img_tm_hour);
	if (ret < 0) {
		goto out_exit;
	}

	/* minute */
	ret = _load_pictures(&tmp_res->scene, RES_TM_MINUTE, &tmp_res->grp_minute,
			pic_common_ids, data->is_digital ? 10 : 1, data->img_tm_min);
	if (ret < 0) {
		goto out_exit;
	}

	/* time colon */
	pic_id = PIC_TM_COLON;
	lvgl_res_load_pictures_from_scene(&tmp_res->scene, &pic_id,
			&data->img_tm_colon, &tmp_res->pt_colon, 1);

out_exit:
	lvgl_res_unload_scene(&tmp_res->scene);
	return ret;
}

static void _aod_clock_unload_resource(aod_clock_ui_data_t *data)
{
	lvgl_res_unload_pictures(data->img_tm_hour, data->is_digital ? 10 : 1);
	lvgl_res_unload_pictures(data->img_tm_min, data->is_digital ? 10 : 1);
	lvgl_res_unload_pictures(&data->img_bg, 1);
	lvgl_res_unload_pictures(&data->img_tm_colon, 1);
}

static int _aod_clock_create(lv_obj_t * scr, const desktop_ui_presenter_t * presenter)
{
	aod_clock_ui_res_t res;

	aod_clock_ui_data_t * data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->clock_dsc = clocksel_get_aod_clock_dsc();
	data->presenter = presenter;

	if (_aod_clock_load_resource(data, &res)) {
		_aod_clock_unload_resource(data);
		app_mem_free(data);
		return -ENOENT;
	}

	data->obj_clock = watch_clock_create(scr);
	watch_clock_set_type(data->obj_clock,
			data->is_digital ? DIGITAL_CLOCK : ANALOG_CLOCK, false);
	watch_clock_set_24hour(data->obj_clock, data->is_digital);

	lv_obj_set_size(data->obj_clock, res.scene.width, res.scene.height);
	lv_obj_center(data->obj_clock);

	if (data->img_bg.data) {
		lv_obj_set_style_bg_img_src(data->obj_clock, &data->img_bg, LV_PART_MAIN);
		lv_obj_set_style_bg_img_opa(data->obj_clock, LV_OPA_COVER, LV_PART_MAIN);
	} else {
		lv_obj_set_style_bg_color(data->obj_clock, res.scene.background, LV_PART_MAIN);
		lv_obj_set_style_bg_opa(data->obj_clock, LV_OPA_COVER, LV_PART_MAIN);
	}

	if (data->is_digital) {
		lv_area_t area;

		lv_area_set(&area, res.grp_hour.x, res.grp_hour.y,
				res.grp_hour.x + res.grp_hour.width - 1,
				res.grp_hour.y + res.grp_hour.height - 1);
		watch_clock_set_digit_images(data->obj_clock, CLOCK_HOUR, 10, data->img_tm_hour, &area);

		lv_area_set(&area, res.grp_minute.x, res.grp_minute.y,
				res.grp_minute.x + res.grp_minute.width - 1,
				res.grp_minute.y + res.grp_minute.height - 1);
		watch_clock_set_digit_images(data->obj_clock, CLOCK_MIN, 10, data->img_tm_min, &area);

		if (data->img_tm_colon.data) {
			watch_clock_set_separator_image(data->obj_clock,
					&data->img_tm_colon, res.pt_colon.x, res.pt_colon.y);
		}
	} else {
		lv_coord_t pivot_x, pivot_y;

		pivot_x = (data->img_bg.header.w / 2) - res.grp_hour.x;
		pivot_y = (data->img_bg.header.h / 2) - res.grp_hour.y;
		watch_clock_set_pointer_images(data->obj_clock, CLOCK_HOUR, 1, data->img_tm_hour, pivot_x, pivot_y);

		pivot_x = (data->img_bg.header.w / 2) - res.grp_minute.x;
		pivot_y = (data->img_bg.header.h / 2) - res.grp_minute.y;
		watch_clock_set_pointer_images(data->obj_clock, CLOCK_MIN, 1, data->img_tm_min, pivot_x, pivot_y);
	}

	lv_obj_set_user_data(scr, data);

	/* initialize values */
	_aod_clock_paint(scr, true);

	SYS_LOG_INF("AOD clock inflated");
	return 0;
}

static int _aod_clock_paint(lv_obj_t * scr, bool first)
{
	aod_clock_ui_data_t *data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	const desktop_ui_presenter_t * presenter = data->presenter;
	if (presenter == NULL)
		return -EINVAL;

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_lock(PARTIAL_WAKE_LOCK);
#endif

	struct rtc_time time;
	presenter->get_time(&time);

	/* adjust rtc period if time diff greater than 1s */
	if (first || time.tm_sec > 1) {
		data->period = data->clock_dsc->period - time.tm_sec * 1000;
		presenter->set_rtc(true, data->period);
	} else if (data->period != data->clock_dsc->period) {
		data->period = data->clock_dsc->period;
		presenter->set_rtc(true, data->period);
	}

	SYS_LOG_INF("time %02u:%02u:%02u\n", time.tm_hour, time.tm_min, time.tm_sec);

	lv_disp_enable_invalidation(NULL, true);

	watch_clock_set_time(data->obj_clock, time.tm_hour, time.tm_min, 0);

	lv_disp_enable_invalidation(NULL, false);

	/* refresh immediately */
	lv_refr_now(NULL);

	/* wait refresh finish */
	if (app_ui_wait_for_refresh() < 0) {
		SYS_LOG_ERR("flush timeout");
	}

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_unlock(PARTIAL_WAKE_LOCK);
#endif

	if (first == false) {
		system_request_fast_standby();
	}

	return 0;
}

static int _aod_clock_focus(lv_obj_t * scr, bool focused)
{
	aod_clock_ui_data_t *data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	if (data->presenter)
		data->presenter->set_rtc(focused, data->period);

	if (focused) {
		lv_obj_invalidate(scr);
		_aod_clock_paint(scr, false);
		s_aod_inflated = true;
	}

	lv_disp_enable_invalidation(NULL, !focused);
	return 0;
}

static int _aod_clock_delete(lv_obj_t * scr)
{
	aod_clock_ui_data_t *data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	lv_obj_clean(scr);
	_aod_clock_unload_resource(data);
	app_mem_free(data);

	s_aod_inflated = false;
	return 0;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int aod_clock_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _aod_clock_create(scr, msg_data);
	case MSG_VIEW_PAINT:
		return _aod_clock_paint(scr, false);
	case MSG_VIEW_FOCUS:
		return _aod_clock_focus(scr, true);
	case MSG_VIEW_DEFOCUS:
		return _aod_clock_focus(scr, false);
	case MSG_VIEW_DELETE:
		return _aod_clock_delete(scr);
	default:
		return 0;
	}
}

int aod_clock_ui_enter(void)
{
	app_ui_stack_forward(AODCLOCK_UI, APP_UI_LOAD_ANIM_NONE);

	while (s_aod_inflated == false) {
		os_sleep(2);
	}

	return 0;
}

int aod_clock_ui_exit(void)
{
	return app_ui_stack_back_home(APP_UI_LOAD_ANIM_NONE);
}

#else

int aod_clock_ui_enter(void)
{
	return -ENOSYS;
}

int aod_clock_ui_exit(void)
{
	return 0;
}

#endif /* SCENE_AOD_CLOCK_VIEW */
