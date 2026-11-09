/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief card reader app view
*/

#include "card_reader.h"
#include "hotplug_manager.h"
#include <input_manager.h>
#include <app_ui.h>

enum {
	BMP_USB_BG = 0,

	NUM_CARD_READER_IMGS,
};

typedef struct card_reader_view_data {
	/* lvgl object */
	lv_obj_t *bmp[NUM_CARD_READER_IMGS];

	/* ui-editor resource */
	lvgl_res_scene_t res_scene;
	lv_img_dsc_t img_bmp[NUM_CARD_READER_IMGS];


	/* lvgl resource */
	lv_point_t pt_bmp[NUM_CARD_READER_IMGS];
	/* user data */
} card_reader_view_data_t;

static const uint32_t bmp_ids[] = {
	PIC_BG_USB,
};

static void _bmp_event_handler(lv_event_t * e)
{
	struct app_msg	msg = {
		.type = MSG_HOTPLUG_EVENT,
		.cmd = HOTPLUG_USB_DEVICE,
		.value = HOTPLUG_OUT,
	};

	send_async_msg("main", &msg);
}

static void _create_img_array(lv_obj_t *par, lv_obj_t **pobj, lv_point_t *pt,
										lv_img_dsc_t *img, uint32_t num)
{
	int i;

	for (i = 0; i < num; i++) {
		pobj[i] = lv_img_create(par);
		lv_img_set_src(pobj[i], &img[i]);
		lv_obj_set_pos(pobj[i], pt[i].x, pt[i].y);
	}
}

void card_reader_show_storage_working(void)
{
#ifdef CONFIG_SEG_LED_MANAGER
	seg_led_display_set_flash(FLASH_ITEM(SLED_SD), 400, FLASH_FOREVER, NULL);
#endif
#ifdef CONFIG_LED_MANAGER
	led_manager_set_blink(0, 200, 100, 500, LED_START_STATE_ON, NULL);
	led_manager_set_blink(1, 200, 100, 500, LED_START_STATE_OFF, NULL);
#endif
}

void card_reader_show_card_plugin(void)
{
#ifdef CONFIG_SEG_LED_MANAGER
	seg_led_display_icon(SLED_SD, true);
#endif
}

static int _load_resource(card_reader_view_data_t *data)
{
	int32_t ret;

	/* load scene */
	ret = lvgl_res_load_scene(SCENE_CARD_READER_VIEW, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		LOG_ERR("SCENE_HEART_VIEW not found");
		return -ENOENT;
	}

	/* load resource */
	lvgl_res_load_pictures_from_scene(&data->res_scene, bmp_ids, data->img_bmp, data->pt_bmp, NUM_CARD_READER_IMGS);

	return 0;
}

static void _unload_resource(card_reader_view_data_t *data)
{
	lvgl_res_unload_pictures(data->img_bmp, NUM_CARD_READER_IMGS);
	lvgl_res_unload_scene(&data->res_scene);
}

void card_reader_view_init(void)
{
	app_ui_stack_forward(CARD_READER_UI, APP_UI_LOAD_ANIM_NONE);
}

void card_reader_view_deinit(void)
{
	app_ui_stack_back(APP_UI_LOAD_ANIM_NONE);
}

static int _card_reader_view_create(lv_obj_t * scr)
{
	card_reader_view_data_t *data;

	data = app_mem_malloc(sizeof(*data));
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, sizeof(*data));

	if (_load_resource(data)) {
		app_mem_free(data);
		return -ENOENT;
	}

	lv_obj_set_user_data(scr, data);

	/* create image */
	_create_img_array(scr, data->bmp, data->pt_bmp, data->img_bmp, NUM_CARD_READER_IMGS);
	lv_obj_center(data->bmp[BMP_USB_BG]);
	lv_obj_add_flag(data->bmp[BMP_USB_BG], LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(data->bmp[BMP_USB_BG], _bmp_event_handler, LV_EVENT_CLICKED, data);

	return 0;
}

static int _card_reader_view_delete(lv_obj_t * scr)
{
	card_reader_view_data_t *data = lv_obj_get_user_data(scr);

	if (data) {
		_unload_resource(data);
		app_mem_free(data);
	}

	return 0;
}

int card_reader_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _card_reader_view_create(scr);
	case MSG_VIEW_DELETE:
		return _card_reader_view_delete(scr);
	default:
		return 0;
	}
}
