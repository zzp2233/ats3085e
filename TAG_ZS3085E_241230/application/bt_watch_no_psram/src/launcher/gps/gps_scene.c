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
#include "gps_ui.h"

enum {
	TXT_TITLE = 0,
	TXT_LATITUDE,
	TXT_TATITUDE,
	TXT_ALTITUDE,
	TXT_UTC,
	NUM_TXTS,
};

const static uint32_t _txt_ids[] = {
	STR_GPS_TITLE,
	STR_LATITUDE,
	STR_TATITUDE,
	STR_ALTITUDE,
	STR_UTC,
};

typedef struct gps_view_data {
	/* lvgl object */
	lv_obj_t *lbl[NUM_TXTS];

	/* ui-editor resource */
	lvgl_res_scene_t res_scene;
	lv_point_t pt_txt[NUM_TXTS];
	lvgl_res_string_t res_txt[NUM_TXTS];

	/* lvgl resource */
	lv_style_t style_txt[NUM_TXTS];
	lv_font_t font;
	char gps_buf[50];

	const gps_view_presenter_t *presenter;
} gps_view_data_t;

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

static int _load_resource(gps_view_data_t *data)
{
	int ret;

	/* load scene */
	ret = lvgl_res_load_scene(SCENE_GPS_VIEW, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_GPS_VIEW not found");
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

static void _unload_resource(gps_view_data_t *data)
{
	lvgl_res_unload_strings(data->res_txt, NUM_TXTS);
	lvgl_res_unload_scene(&data->res_scene);

	LVGL_FONT_CLOSE(&data->font);
}

static int _gps_view_create(lv_obj_t *scr, const gps_view_presenter_t *presenter)
{
	gps_view_data_t *data;
	int i;

	data = app_mem_malloc(sizeof(*data));
	if (!data)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	if (_load_resource(data)) {
		app_mem_free(data);
		return -ENOENT;
	}

	// create label
	for (i = 0; i < NUM_TXTS; i++) {
		data->lbl[i] = lv_label_create(scr);
		lv_obj_set_pos(data->lbl[i], data->pt_txt[i].x, data->pt_txt[i].y);
		lv_obj_add_style(data->lbl[i], &data->style_txt[i], LV_PART_MAIN);

		lv_label_set_text(data->lbl[i], data->res_txt[i].txt);
	}

	lv_obj_set_user_data(scr, data);

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_lock(FULL_WAKE_LOCK);
#endif

	return 0;
}

static int _gps_view_paint(lv_obj_t *scr)
{
	gps_view_data_t *data = lv_obj_get_user_data(scr);

#ifdef CONFIG_GPS_MANAGER
	const gps_view_presenter_t *presenter = data->presenter;
	gps_res_t* gps_data = presenter->get_gps_data();
#endif

	if (data) {
		SYS_LOG_INF("_gps_view_paint");
#ifdef CONFIG_GPS_MANAGER
		snprintf(data->gps_buf, sizeof(data->gps_buf), "纬度: %f",
			minmea_tofloat(&gps_data->gga_data.latitude));
		lv_label_set_text(data->lbl[TXT_LATITUDE], data->gps_buf);

		snprintf(data->gps_buf, sizeof(data->gps_buf), "经度: %f",
			minmea_tofloat(&gps_data->gga_data.longitude));
		lv_label_set_text(data->lbl[TXT_TATITUDE], data->gps_buf);

		snprintf(data->gps_buf, sizeof(data->gps_buf), "海拔高度: %f",
			minmea_tofloat(&gps_data->gga_data.altitude));
		lv_label_set_text(data->lbl[TXT_ALTITUDE], data->gps_buf);

		snprintf(data->gps_buf, sizeof(data->gps_buf), "GPS时间: %02d:%02d:%02d",
			gps_data->gga_data.time.hours,gps_data->gga_data.time.minutes,
			gps_data->gga_data.time.seconds);
		lv_label_set_text(data->lbl[TXT_UTC], data->gps_buf);
#endif
	}

	return 0;
}

static int _gps_view_delete(lv_obj_t *scr)
{
	gps_view_data_t *data = lv_obj_get_user_data(scr);
	int i;

	if (data == NULL)
		return 0;

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_unlock(FULL_WAKE_LOCK);
#endif

	for (i = 0; i < NUM_TXTS; i++) {
		lv_obj_del(data->lbl[i]);
		lv_style_reset(&data->style_txt[i]);
	}

	_unload_resource(data);

	app_mem_free(data);
	return 0;
}

static int _gps_view_focus_changed(lv_obj_t * scr, bool focused)
{
#ifdef CONFIG_GPS_MANAGER
	gps_view_data_t *data = lv_obj_get_user_data(scr);

	if (focused)  {
		gps_manager_enable(0,0);
	} else {
		gps_manager_disable(0, 0);
	}
#endif

	return 0;
}

int gps_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _gps_view_create(scr, msg_data);
	case MSG_VIEW_DELETE:
		return _gps_view_delete(scr);
	case MSG_VIEW_FOCUS:
		return _gps_view_focus_changed(scr, 1);
	case MSG_VIEW_DEFOCUS:
		return _gps_view_focus_changed(scr, 0);
	case MSG_VIEW_PAINT:
		return _gps_view_paint(scr);
	default:
		return 0;
	}
}
