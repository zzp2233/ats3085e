/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*********************
 *      INCLUDES
 *********************/
#include <assert.h>
#include <widgets/simple_img.h>
#include "clock_selector_ui.h"

/*********************
 *      DEFINES
 *********************/
#define SCENE_CLOCK_SELECTOR_ID SCENE_CLOCK_SEL_VIEW

/**********************
 *      TYPEDEFS
 **********************/
typedef struct clock_selector_ui_data {
	lv_obj_t * tileview;

	lv_img_dsc_t * img_dsc;

	int8_t sel_src; /* src index of the selected clock index */
	int8_t sel_idx; /* selected clock index */

	lv_font_t font;
	lv_style_t sty_txt;
	lv_style_t sty_bg;

	lvgl_res_picregion_t picreg;
	lvgl_res_string_t strtxt[1];
	lvgl_res_scene_t scene;

	const clock_selector_ui_presenter_t *presenter;
} clock_selector_ui_data_t;

/**********************
 *   STATIC PROTOTYPES
 **********************/
static void _clock_selector_unload_resource(clock_selector_ui_data_t *data);

/**********************
 *   STATIC FUNCTIONS
 **********************/
static int _clock_selector_load_resource(clock_selector_ui_data_t *data)
{
	uint32_t res_id = STR_NAME;
	int ret;

	/* scene */
	ret = lvgl_res_load_scene(SCENE_CLOCK_SELECTOR_ID, &data->scene,
			DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret) {
		SYS_LOG_ERR("scene selector view not found");
		return -ENOENT;
	}

	ret = lvgl_res_load_picregion_from_scene(&data->scene, RES_THUMBNAIL, &data->picreg);
	if (ret || data->picreg.frames < 1) {
		SYS_LOG_ERR("cannot find picreg 0x%x\n", RES_THUMBNAIL);
		goto fail_exit;
	}

	data->img_dsc = app_mem_malloc(sizeof(lv_img_dsc_t) * data->picreg.frames);
	if (data->img_dsc == NULL) {
		lvgl_res_unload_picregion(&data->picreg);
		goto fail_exit;
	}

	ret = lvgl_res_load_pictures_from_picregion(&data->picreg, 0, data->picreg.frames - 1, data->img_dsc);
	lvgl_res_unload_picregion(&data->picreg);

	if (ret) {
		SYS_LOG_ERR("cannot load picreg 0x%x pictures\n", RES_THUMBNAIL);
		goto fail_exit;
	}

	lvgl_res_load_strings_from_scene(&data->scene, &res_id, data->strtxt, ARRAY_SIZE(data->strtxt));
	if (data->strtxt[0].txt) {
		ret = LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_NORMAL);
		if (ret) {
			goto fail_exit;
		}
	}

	return 0;
fail_exit:
	_clock_selector_unload_resource(data);
	return -ENOENT;
}

static void _clock_selector_unload_resource(clock_selector_ui_data_t *data)
{
	if (data->strtxt[0].txt) {
		LVGL_FONT_CLOSE(&data->font);

		lvgl_res_unload_strings(data->strtxt, ARRAY_SIZE(data->strtxt));
	}

	lvgl_res_unload_pictures(data->img_dsc, data->picreg.frames);
	lvgl_res_unload_scene(&data->scene);
}

static void _clock_selector_click_event_handler(lv_event_t * e)
{
	clock_selector_ui_data_t *data = lv_event_get_user_data(e);
	lv_obj_t *img = lv_event_get_target(e);

	data->sel_idx = (int)lv_obj_get_user_data(img);
	if (data->presenter)
		data->presenter->set_clock_id(data->sel_idx);
}

static int _clock_selector_create(lv_obj_t * scr, const clock_selector_ui_presenter_t * presenter)
{
	clock_selector_ui_data_t *data;
	int i;

	data = app_mem_malloc(sizeof(*data));
	if (data == NULL) {
		return -ENOMEM;
	}

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;
	data->sel_idx = presenter ? presenter->get_clock_id() : 0;

	if (_clock_selector_load_resource(data)) {
		app_mem_free(data);
		return -ENOENT;
	}

	lv_style_init(&data->sty_bg);
	lv_style_set_bg_color(&data->sty_bg, data->scene.background);
	lv_style_set_bg_opa(&data->sty_bg, LV_OPA_COVER);
	lv_obj_add_style(scr, &data->sty_bg, LV_PART_MAIN);

	if (data->strtxt[0].txt) {
		lv_style_init(&data->sty_txt);
		lv_style_set_text_font(&data->sty_txt, &data->font);
		lv_style_set_text_color(&data->sty_txt, data->strtxt[0].color);
		lv_style_set_text_align(&data->sty_txt, LV_TEXT_ALIGN_CENTER);
		lv_style_set_bg_opa(&data->sty_txt, LV_OPA_TRANSP);
		lv_style_set_align(&data->sty_txt, LV_ALIGN_TOP_MID);
	}

	data->tileview = lv_tileview_create(scr);
	lv_obj_set_size(data->tileview, data->scene.width, data->scene.height);
	lv_obj_center(data->tileview);
	lv_obj_add_style(data->tileview, &data->sty_bg, LV_PART_MAIN);
	lv_obj_set_user_data(data->tileview, data);

	for (i = 0; i < data->picreg.frames; i++) {
		lv_obj_t * tile = lv_tileview_add_tile(data->tileview, i, 0, LV_DIR_HOR);
		lv_obj_set_user_data(tile, (void *)i);

		lv_obj_t * img = lv_img_create(tile);
		lv_img_set_src(img, &data->img_dsc[i]);
		//lv_obj_add_style(img, &data->sty_bg, LV_PART_MAIN);
		lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_pos(img, data->picreg.x, data->picreg.y);
		lv_obj_set_size(img, data->picreg.width, data->picreg.height);
		lv_obj_set_user_data(img, (void *)i);
		lv_obj_add_event_cb(img, _clock_selector_click_event_handler, LV_EVENT_SHORT_CLICKED, data);

		if (data->strtxt[0].txt) {
			lv_obj_t * txt = lv_label_create(tile);
			lv_label_set_text_static(txt, presenter->get_clock_name(i));
			lv_obj_add_style(txt, &data->sty_txt, LV_PART_MAIN);
		}
	}

	lv_obj_update_layout(data->tileview);
	lv_obj_set_tile_id(data->tileview, data->sel_idx, 0, false);

	lv_obj_set_user_data(scr, data);
	return 0;
}

static int _clock_selector_delete(lv_obj_t * scr)
{
	clock_selector_ui_data_t *data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	lv_obj_clean(scr);
	lv_style_reset(&data->sty_bg);
	lv_style_reset(&data->sty_txt);

	_clock_selector_unload_resource(data);

	app_mem_free(data->img_dsc);
	app_mem_free(data);
	return 0;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int clock_selector_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _clock_selector_create(scr, msg_data);
	case MSG_VIEW_DELETE:
		return _clock_selector_delete(scr);
	case MSG_VIEW_PAINT:
	default:
		return 0;
	}
}
