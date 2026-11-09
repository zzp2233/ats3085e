/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "desktop_ui.h"

#define ENG_STR_FILE CONFIG_APP_UI_DISK"/bt_watch.enG"

enum {
	BMP_BG = 0,
	BMP_BTN,
	BMP_BTN_H,
	NUM_BMPS,
};

enum {
	TXT_TEST = 0,
	NUM_TXTS,
};

typedef struct {
	lv_point_t pt_bmp[NUM_BMPS];
} sport_ui_res_t;

typedef struct {
	/* lvgl object */
	lv_obj_t *btn;
	lv_obj_t *img;
	lv_obj_t *lbl;

	/* lvgl resource */
	lv_img_dsc_t img_dsc[NUM_BMPS];
	lv_font_t font;

	lvgl_res_scene_t res_scene;
} sport_ui_data_t;

static const uint32_t _bmp_ids[] = {
	PIC_BG, PIC_BTN, PIC_BTN_H
};

static const uint32_t _txt_ids[] = {
	STR_TEXT,
};

static const char *str_file = DEF_STR_FILE;

static int _sport_load_resource(sport_ui_data_t * data, sport_ui_res_t * res)
{
	int ret = lvgl_res_load_scene(SCENE_SPORT_VIEW, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, str_file);
	if (ret < 0) return ret;

	lvgl_res_load_pictures_from_scene(&data->res_scene, _bmp_ids, data->img_dsc, res->pt_bmp, NUM_BMPS);

	ret = LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_NORMAL);
	if (ret < 0) {
		lvgl_res_unload_pictures(data->img_dsc, NUM_BMPS);
		lvgl_res_unload_scene(&data->res_scene);
	}

	return ret;
}

static void _sport_reload_text(sport_ui_data_t * data)
{
	lvgl_res_string_t res_text[NUM_TXTS];

	lvgl_res_unload_scene(&data->res_scene);
	lvgl_res_load_scene(SCENE_SPORT_VIEW, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, str_file);

	lvgl_res_load_strings_from_scene(&data->res_scene, _txt_ids, res_text, NUM_TXTS);

	SYS_LOG_INF("update text: %s", res_text[TXT_TEST].txt);

	lv_obj_set_pos(data->lbl, res_text[TXT_TEST].x, res_text[TXT_TEST].y);
	lv_obj_set_style_text_color(data->lbl, res_text[TXT_TEST].color, LV_PART_MAIN);
	lv_label_set_text(data->lbl, res_text[TXT_TEST].txt);

	lvgl_res_unload_strings(res_text, NUM_TXTS);
}

static void _sport_change_lang(sport_ui_data_t * data)
{
	SYS_LOG_INF("Toggle language\n");

	if (strcmp(str_file, DEF_STR_FILE)) {
		str_file = DEF_STR_FILE;
	} else {
		str_file = ENG_STR_FILE;
	}

	_sport_reload_text(data);
}

static void _btn_evt_handler(lv_event_t * e)
{
	sport_ui_data_t *data = lv_event_get_user_data(e);

	_sport_change_lang(data);
}

static int _sport_create(lv_obj_t * scr)
{
	sport_ui_res_t tmp_res;

	sport_ui_data_t * data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));

	if (_sport_load_resource(data, &tmp_res)) {
		app_mem_free(data);
		return -ENOENT;
	}

	data->img = lv_img_create(scr);
	lv_img_set_src(data->img, &data->img_dsc[BMP_BG]);
	//lv_obj_set_pos(data->img, tmp_res.pt_bmp[BMP_BG].x, tmp_res.pt_bmp[BMP_BG].y);
	lv_obj_center(data->img);

	data->btn = lv_imgbtn_create(data->img);
	lv_imgbtn_set_src(data->btn, LV_IMGBTN_STATE_RELEASED, NULL, &data->img_dsc[BMP_BTN], NULL);
	lv_imgbtn_set_src(data->btn, LV_IMGBTN_STATE_PRESSED, NULL, &data->img_dsc[BMP_BTN_H], NULL);
	lv_imgbtn_set_src(data->btn, LV_IMGBTN_STATE_CHECKED_RELEASED, NULL, &data->img_dsc[BMP_BTN_H], NULL);
	lv_imgbtn_set_src(data->btn, LV_IMGBTN_STATE_CHECKED_PRESSED, NULL, &data->img_dsc[BMP_BTN], NULL);

	lv_obj_set_pos(data->btn, tmp_res.pt_bmp[BMP_BTN].x, tmp_res.pt_bmp[BMP_BTN].y);
	lv_obj_set_size(data->btn, data->img_dsc[BMP_BTN].header.w, data->img_dsc[BMP_BTN].header.h);
	lv_obj_add_flag(data->btn, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_add_event_cb(data->btn, _btn_evt_handler, LV_EVENT_VALUE_CHANGED, data);

	data->lbl = lv_label_create(data->img);
	lv_obj_set_style_text_font(data->lbl, &data->font, LV_PART_MAIN);
	_sport_reload_text(data);

	lv_obj_set_user_data(scr, data);
	return 0;
}

static int _sport_delete(lv_obj_t * scr)
{
	sport_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	lv_obj_clean(scr);

	lvgl_res_unload_pictures(data->img_dsc, NUM_BMPS);
	lvgl_res_unload_scene(&data->res_scene);
	LVGL_FONT_CLOSE(&data->font);

	app_mem_free(data);
	return 0;
}

static int _sport_proc_key(lv_obj_t * scr, ui_key_msg_data_t * key_data)
{
	sport_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	if (key_data->event == (KEY_TBD | KEY_TYPE_SHORT_UP)) {
		key_data->done = true; /* key done, and stop next processing */
		_sport_change_lang(data);
	}

	return 0;
}

int sport_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _sport_create(scr);
	case MSG_VIEW_DELETE:
		return _sport_delete(scr);
	case MSG_VIEW_KEY:
		return _sport_proc_key(scr, msg_data);
	default:
		return 0;
	}
}
