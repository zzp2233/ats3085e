/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief bt call view
 */

#include <os_common_api.h>
#include <input_manager.h>
#ifndef  CONFIG_SIMULATOR
#include <soc_pm.h>
#endif
#include "launcher_app.h"
#include "app_ui.h"


/*power main view*/
enum {
	BMP_POWER_BG = 0,

	NUM_POWER_BG_IMGS,
};
enum {
	BTN_RESTART = 0,
	BTN_POWEROFF,

	NUM_POWER_BTNS,
};
enum {
	BMP_TXT_RESTART = 0,
	BMP_TXT_POWEROFF,

	NUM_POWER_BMP_TXTS,
};

/*confirm restart view*/
enum {
	BTN_RESTART_CONFIRM = 0,

	NUM_RESTART_BTNS,
};
enum {
	BMP_TXT_RESTART_CONFIRM = 0,

	NUM_RESTART_BMP_TXTS,
};
/*confirm poweroff view*/
enum {
	BTN_POWEROFF_CONFIRM = 0,

	NUM_POWEROFF_BTNS,
};
enum {
	BMP_TXT_POWEROFF_CONFIRM = 0,

	NUM_POWEROFF_BMP_TXTS,
};

const static uint32_t power_bg_bmp_ids[] = {
	PIC_POWER_BG,
};
const static uint32_t power_btn_ids[] = {
	PIC_BTN_RESTART,
	PIC_BTN_POWEROFF,
};
const static uint32_t power_bmp_txt_ids[] = {
	PIC_TXT_RESTART,
	PIC_TXT_POWEROFF,
};

const static uint32_t restart_btn_ids[] = {
	PIC_BTN_RESTART_CONFIRM,
};
const static uint32_t restart_bmp_txt_ids[] = {
	PIC_TXT_RESTART_CONFIRM,
};

const static uint32_t poweroff_btn_ids[] = {
	PIC_BTN_POWEROFF_COMFIRM,
};
const static uint32_t poweroff_bmp_txt_ids[] = {
	PIC_TXT_POWEROFF_COMFIRM,
};


typedef struct power_view_data {
	/* lvgl object */
	lv_obj_t *power_bg[NUM_POWER_BG_IMGS];
	lv_obj_t *power_btn[NUM_POWER_BTNS];
	lv_obj_t *power_txt[NUM_POWER_BMP_TXTS];

	lv_obj_t *restart_btn[NUM_RESTART_BTNS];
	lv_obj_t *restart_txt[NUM_RESTART_BMP_TXTS];

	lv_obj_t *poweroff_btn[NUM_POWEROFF_BTNS];
	lv_obj_t *poweroff_txt[NUM_POWEROFF_BMP_TXTS];

	/* ui-editor resource */
	lvgl_res_scene_t res_scene;
	lv_img_dsc_t img_dsc_power_bg[NUM_POWER_BG_IMGS];
	lv_img_dsc_t img_dsc_power_btn[NUM_POWER_BTNS];
	lv_img_dsc_t img_dsc_power_txt[NUM_POWER_BMP_TXTS];

	lv_img_dsc_t img_dsc_restart_btn[NUM_RESTART_BTNS];
	lv_img_dsc_t img_dsc_restart_txt[NUM_RESTART_BMP_TXTS];

	lv_img_dsc_t img_dsc_poweroff_btn[NUM_POWEROFF_BTNS];
	lv_img_dsc_t img_dsc_poweroff_txt[NUM_POWEROFF_BMP_TXTS];

	/* lvgl resource */
	lv_point_t pt_power_bg[NUM_POWER_BG_IMGS];
	lv_point_t pt_power_btn[NUM_POWER_BTNS];
	lv_point_t pt_power_txt[NUM_POWER_BMP_TXTS];

	lv_point_t pt_restart_btn[NUM_RESTART_BTNS];
	lv_point_t pt_restart_txt[NUM_RESTART_BMP_TXTS];

	lv_point_t pt_poweroff_btn[NUM_POWEROFF_BTNS];
	lv_point_t pt_poweroff_txt[NUM_POWEROFF_BMP_TXTS];
} power_view_data_t;

static void _power_enter_restart_view(power_view_data_t *data);
static void _power_enter_poweroff_view(power_view_data_t *data);
static void _power_exit_power_view(power_view_data_t *data);

static void _power_delete_obj_array(lv_obj_t **pobj, uint32_t num)
{
	for (int i = 0; i < num; i++) {
		if (pobj[i]) {
			lv_obj_del(pobj[i]);
			pobj[i] = NULL;
		}
	}
}

#ifndef  CONFIG_SIMULATOR
static void _power_reboot(void)
{
	struct app_msg  msg = {0};

	msg.type = MSG_REBOOT;
	msg.cmd = REBOOT_TYPE_NORMAL;
	send_async_msg("main", &msg);
}
#endif

static void _power_btn_evt_handler(lv_event_t * e)
{
	lv_event_code_t event = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_current_target(e);
	power_view_data_t * data = lv_event_get_user_data(e);

	if (event == LV_EVENT_CLICKED) {
		if (data->power_btn[BTN_RESTART] == obj) {
			_power_exit_power_view(data);
			_power_enter_restart_view(data);
		} else if (data->power_btn[BTN_POWEROFF] == obj) {
			_power_exit_power_view(data);
			_power_enter_poweroff_view(data);
		} else if (data->restart_btn[BTN_RESTART_CONFIRM] == obj) {
#ifndef  CONFIG_SIMULATOR
			_power_reboot();
#endif
		} else if (data->poweroff_btn[BTN_POWEROFF_CONFIRM] == obj) {
			sys_event_send_message(MSG_POWER_OFF);
		}

		SYS_LOG_INF("Clicked\n");
	} else if (event == LV_EVENT_VALUE_CHANGED) {
		SYS_LOG_INF("Toggled\n");
	}
}

static void _power_create_btn_array(lv_obj_t *par, lv_obj_t **pobj, lv_point_t *pt,
					lv_img_dsc_t *def, lv_img_dsc_t *sel, uint32_t num, void * user_data)
{
	for (int i = 0; i < num; i++) {
		pobj[i] = lv_imgbtn_create(par);
		lv_obj_set_pos(pobj[i], pt[i].x, pt[i].y);
		lv_obj_set_size(pobj[i], def[i].header.w, def[i].header.h);
		lv_obj_set_user_data(pobj[i], (void*)i);
		lv_obj_add_flag(pobj[i], LV_OBJ_FLAG_CHECKABLE);
		lv_obj_add_event_cb(pobj[i], _power_btn_evt_handler, LV_EVENT_ALL, user_data);

		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_RELEASED, NULL, &def[i], NULL);
		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_PRESSED, NULL, &sel[i], NULL);
		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_CHECKED_RELEASED, NULL, &sel[i], NULL);
		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_CHECKED_PRESSED, NULL, &def[i], NULL);
	}
}

static void _power_create_img_array(lv_obj_t *par, lv_obj_t **pobj, lv_point_t *pt,
										lv_img_dsc_t *img, uint32_t num)
{
	for (int i = 0; i < num; i++) {
		pobj[i] = lv_img_create(par);
		lv_img_set_src(pobj[i], &img[i]);
		lv_obj_set_pos(pobj[i], pt[i].x, pt[i].y);
	}
}

static int _power_load_resource(power_view_data_t *data)
{
	int ret;

	/* load scene */
	ret = lvgl_res_load_scene(SCENE_POWER_VIEW, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_HEART_VIEW not found");
	}

	return ret;
}

static void _power_unload_resource(power_view_data_t *data)
{
	if (data->power_btn[BTN_RESTART]) {
		lvgl_res_unload_pictures(data->img_dsc_power_btn, NUM_POWER_BTNS);
		lvgl_res_unload_pictures(data->img_dsc_power_txt, NUM_POWER_BMP_TXTS);
	} else if (data->restart_btn[BTN_RESTART_CONFIRM]) {
		lvgl_res_unload_pictures(data->img_dsc_restart_btn, NUM_RESTART_BTNS);
		lvgl_res_unload_pictures(data->img_dsc_restart_txt, NUM_RESTART_BMP_TXTS);
	} else if (data->poweroff_btn[BTN_POWEROFF_CONFIRM]) {
		lvgl_res_unload_pictures(data->img_dsc_poweroff_btn, NUM_POWEROFF_BTNS);
		lvgl_res_unload_pictures(data->img_dsc_poweroff_txt, NUM_POWEROFF_BMP_TXTS);
	}

	lvgl_res_unload_pictures(data->img_dsc_power_bg, NUM_POWER_BG_IMGS);
	lvgl_res_unload_scene(&data->res_scene);

}

static void _power_exit_power_view(power_view_data_t *data)
{
	lvgl_res_unload_pictures(data->img_dsc_power_btn, NUM_POWER_BTNS);
	lvgl_res_unload_pictures(data->img_dsc_power_txt, NUM_POWER_BMP_TXTS);
	_power_delete_obj_array(data->power_btn, NUM_POWER_BTNS);
	_power_delete_obj_array(data->power_txt, NUM_POWER_BMP_TXTS);
}

static void _power_enter_power_view(lv_obj_t *scr, power_view_data_t *data)
{
	/* load resource */
	lvgl_res_load_pictures_from_scene(&data->res_scene, power_bg_bmp_ids,
			data->img_dsc_power_bg, data->pt_power_bg, NUM_POWER_BG_IMGS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, power_btn_ids,
			data->img_dsc_power_btn, data->pt_power_btn, NUM_POWER_BTNS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, power_bmp_txt_ids,
			data->img_dsc_power_txt, data->pt_power_txt, NUM_POWER_BMP_TXTS);

	/* create power bg image */
	_power_create_img_array(scr, data->power_bg, data->pt_power_bg, data->img_dsc_power_bg, NUM_POWER_BG_IMGS);
	lv_obj_center(data->power_bg[BMP_POWER_BG]);

	/* create power button */
	_power_create_btn_array(data->power_bg[BMP_POWER_BG], data->power_btn, data->pt_power_btn,
			data->img_dsc_power_btn, data->img_dsc_power_btn, NUM_POWER_BTNS, data);
	/* create power txt image */
	_power_create_img_array(data->power_bg[BMP_POWER_BG], data->power_txt, data->pt_power_txt,
			data->img_dsc_power_txt, NUM_POWER_BMP_TXTS);
}

static void _power_enter_restart_view(power_view_data_t *data)
{
	lvgl_res_load_pictures_from_scene(&data->res_scene, restart_btn_ids,
			data->img_dsc_restart_btn, data->pt_restart_btn, NUM_RESTART_BTNS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, restart_bmp_txt_ids,
			data->img_dsc_restart_txt, data->pt_restart_txt, NUM_RESTART_BMP_TXTS);
	/* create restart button */
	_power_create_btn_array(data->power_bg[BMP_POWER_BG], data->restart_btn, data->pt_restart_btn,
			data->img_dsc_restart_btn, data->img_dsc_restart_btn, NUM_RESTART_BTNS, data);
	/* create restart txt image */
	_power_create_img_array(data->power_bg[BMP_POWER_BG], data->restart_txt, data->pt_restart_txt,
			data->img_dsc_restart_txt, NUM_RESTART_BMP_TXTS);
}

static void _power_enter_poweroff_view(power_view_data_t *data)
{
	lvgl_res_load_pictures_from_scene(&data->res_scene, poweroff_btn_ids,
			data->img_dsc_poweroff_btn, data->pt_poweroff_btn, NUM_POWEROFF_BTNS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, poweroff_bmp_txt_ids,
			data->img_dsc_poweroff_txt, data->pt_poweroff_txt, NUM_POWEROFF_BMP_TXTS);
	/* create restart button */
	_power_create_btn_array(data->power_bg[BMP_POWER_BG], data->poweroff_btn, data->pt_poweroff_btn,
			data->img_dsc_poweroff_btn, data->img_dsc_poweroff_btn, NUM_POWEROFF_BTNS, data);
	/* create restart txt image */
	_power_create_img_array(data->power_bg[BMP_POWER_BG], data->poweroff_txt, data->pt_poweroff_txt,
			data->img_dsc_poweroff_txt, NUM_POWEROFF_BMP_TXTS);
}

static int _power_view_create(lv_obj_t * scr)
{
	power_view_data_t *data = app_mem_malloc(sizeof(*data));
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, sizeof(*data));

	if (_power_load_resource(data)) {
		app_mem_free(data);
		return -ENOENT;
	}

	lv_obj_set_user_data(scr, data);

	_power_enter_power_view(scr,data);

	return 0;
}

static int _power_view_delete(lv_obj_t * scr)
{
	power_view_data_t *data = lv_obj_get_user_data(scr);

	if (data) {
		if (data->power_btn[BTN_RESTART]) {
			_power_delete_obj_array(data->power_btn, NUM_POWER_BTNS);
			_power_delete_obj_array(data->power_txt, NUM_POWER_BMP_TXTS);
		} else if (data->restart_btn[BTN_RESTART_CONFIRM]) {
			_power_delete_obj_array(data->restart_btn, NUM_RESTART_BTNS);
			_power_delete_obj_array(data->restart_txt, NUM_RESTART_BMP_TXTS);
		} else if (data->poweroff_btn[BTN_POWEROFF_CONFIRM]) {
			_power_delete_obj_array(data->poweroff_btn, NUM_POWEROFF_BTNS);
			_power_delete_obj_array(data->poweroff_txt, NUM_POWEROFF_BMP_TXTS);
		}

		_power_delete_obj_array(data->power_bg, NUM_POWER_BG_IMGS);
		_power_unload_resource(data);

		app_mem_free(data);
		SYS_LOG_INF("ok\n");
	}

	return 0;
}

int power_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _power_view_create(scr);
	case MSG_VIEW_DELETE:
		return _power_view_delete(scr);
	default:
		return 0;
	}
}
