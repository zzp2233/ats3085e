/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <mem_manager.h>
#include <widgets/simple_img.h>
#include <widgets/text_canvas.h>
#include "desktop_ui.h"

#ifdef CONFIG_BT_MANAGER
#  include <bt_manager.h>
#endif

enum {
	BTN_WEATHER = 0,
	BTN_BRIGHTNESS,
	BTN_BLUETOOTH,
	BTN_VIBRATOR,
	BTN_GLASS,
	BTN_SETTING,
	NUM_BTNS,
};

enum {
	IMG_BAT = 0,
	NUM_IMGS,
};

enum {
	TXT_DATE = 0,
	TXT_WEEK,
	TXT_BAT,
	NUM_TXTS,
};

enum {
	BMP_BAT1 = 0,
	BMP_BAT2,
	BMP_BAT3,
	BMP_BAT4,
	BMP_BAT5,
	NUM_BATS,
};

typedef struct {
	lvgl_res_string_t res_txt[NUM_TXTS];
	lv_point_t pt_btn[NUM_BTNS];
	lv_point_t pt_bat[NUM_BATS];
	lvgl_res_scene_t scene;
} ctrl_ui_res_t;

typedef struct {
	lv_obj_t *btn[NUM_BTNS];
	lv_obj_t *img[NUM_IMGS];
	lv_obj_t *lbl[NUM_TXTS];

	lv_img_dsc_t img_dsc_def[NUM_BTNS];
	lv_img_dsc_t img_dsc_sel[NUM_BTNS];
	lv_img_dsc_t img_dsc_bat[NUM_BATS];

	lv_font_t font;

	/* user data */
	uint8_t bat_val;
	uint8_t btn_sta[NUM_BTNS];

	const desktop_ui_presenter_t * presenter;
} ctrl_ui_data_t;

static const uint32_t _btn_def_ids[] = {
	PIC_WEATHER, PIC_BRIGHTNESS, PIC_BLUETOOTH,
	PIC_VIBRATOR, PIC_GLASS, PIC_SETTING,
};

static const uint32_t _btn_sel_ids[] = {
	PIC_WEATHER_H, PIC_BRIGHTNESS_H, PIC_BLUETOOTH_H,
	PIC_VIBRATOR_H, PIC_GLASS_H, PIC_SETTING_H,
};

static const uint32_t _bat_ids[] = {
	PIC_BAT1, PIC_BAT2, PIC_BAT3, PIC_BAT4, PIC_BAT5,
};

static const uint32_t _txt_ids[] = {
	STR_DATE, STR_WEEK, STR_BAT,
};

static const char * const _txt_week[7] = {
	"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

static int _ctrl_load_resource(ctrl_ui_data_t * data, ctrl_ui_res_t * res)
{
	int ret;

	ret = lvgl_res_load_scene(SCENE_SYSTEM_VIEW, &res->scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) return ret;

	/* load resource */
	lvgl_res_load_pictures_from_scene(&res->scene, _btn_def_ids, data->img_dsc_def, res->pt_btn, NUM_BTNS);
	lvgl_res_load_pictures_from_scene(&res->scene, _btn_sel_ids, data->img_dsc_sel, NULL, NUM_BTNS);
	lvgl_res_load_pictures_from_scene(&res->scene, _bat_ids, data->img_dsc_bat, res->pt_bat, NUM_BATS);

	lvgl_res_load_strings_from_scene(&res->scene, _txt_ids, res->res_txt, NUM_TXTS);
	lvgl_res_unload_strings(res->res_txt, NUM_TXTS);

	ret = LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_NORMAL);

	lvgl_res_unload_scene(&res->scene);
	return ret;
}

/*
 * 函数功能：同步各按钮的高亮状态
 * 执行流程：
 *   1. 读取"屏幕常亮"开关状态，刷新亮度按钮
 *   2. 读取蓝牙已连接设备数，刷新蓝牙按钮（连上则高亮）
 *   注：true/false 两个方向都要 set_state，否则状态只亮不灭
 */
static void _update_btn_status(ctrl_ui_data_t *data)
{
	data->btn_sta[BTN_BRIGHTNESS] = data->presenter->get_sleep_never();
	lv_imgbtn_set_state(data->btn[BTN_BRIGHTNESS],
		data->btn_sta[BTN_BRIGHTNESS] ? LV_IMGBTN_STATE_CHECKED_RELEASED
		                             : LV_IMGBTN_STATE_RELEASED);

#ifdef CONFIG_BT_MANAGER
#if CONFIG_BT_BR_ACTS
	data->btn_sta[BTN_BLUETOOTH] = (bt_manager_get_connected_dev_num() > 0);
#else
	data->btn_sta[BTN_BLUETOOTH] = 0;
#endif
	lv_imgbtn_set_state(data->btn[BTN_BLUETOOTH],
		data->btn_sta[BTN_BLUETOOTH] ? LV_IMGBTN_STATE_CHECKED_RELEASED
		                            : LV_IMGBTN_STATE_RELEASED);
#endif
}

static void _btn_evt_handler(lv_event_t * e)
{
	lv_obj_t *scr = lv_event_get_user_data(e);
	ctrl_ui_data_t *data = lv_obj_get_user_data(scr);
	lv_obj_t *btn = lv_event_get_target(e);
	int idx = (int)lv_obj_get_user_data(btn);

	SYS_LOG_INF("Btn[%d] Toggled\n", idx);

	if (idx == BTN_BRIGHTNESS) {
		// screen always on
		data->btn_sta[idx] = !data->btn_sta[idx];
		data->presenter->set_sleep_never(data->btn_sta[idx]);
	}
}

/**
 * @brief 控制中心"设置"按钮点击回调
 *
 * 处理流程：
 *   1) 打日志，方便串口确认事件确实到达
 *   2) 向前压栈打开 SETTING_UI（设置页）
 *
 * 注意：该按钮在 _ctrl_create 里被清掉了 LV_OBJ_FLAG_CHECKABLE，
 *       不会产生 LV_EVENT_VALUE_CHANGED，所以必须单独绑 CLICKED。
 */
static void _btn_setting_evt_handler(lv_event_t * e)
{
	SYS_LOG_INF("ctrl setting clicked\n");
	app_ui_stack_forward(SETTING_UI, LV_SCR_LOAD_ANIM_ZOOM_IN);
}

static int _ctrl_paint(lv_obj_t * scr)
{
	ctrl_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return -EINVAL;

	const desktop_ui_presenter_t * presenter = data->presenter;
	if (presenter == NULL)
		return -EINVAL;

	struct rtc_time time;
	char buf[16];

	/* update battery */
	data->bat_val = presenter->get_battery_percent();
	sprintf(buf, "%d%%", data->bat_val);
	text_canvas_set_text(data->lbl[TXT_BAT], buf);
	lv_img_set_src(data->img[IMG_BAT], &data->img_dsc_bat[data->bat_val / (100/4)]);

	/* update date */
	presenter->get_time(&time);
	sprintf(buf, "%04d-%02d-%02d", time.tm_year+1900, time.tm_mon+1, time.tm_mday);
	text_canvas_set_text(data->lbl[TXT_DATE], buf);
	text_canvas_set_text(data->lbl[TXT_WEEK], _txt_week[time.tm_wday]);

	/*
	 * 每次面板重绘/获得焦点时同步按钮状态
	 * 原因：场景是常驻缓存的，_ctrl_create 只跑一次，
	 *       若创建时未连蓝牙，之后连上也永远不会高亮
	 */
	_update_btn_status(data);

	return 0;
}

static int _ctrl_create(lv_obj_t * scr, const desktop_ui_presenter_t * presenter)
{
	ctrl_ui_res_t tmp_res;

	ctrl_ui_data_t * data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	if (_ctrl_load_resource(data, &tmp_res)) {
		app_mem_free(data);
		return -ENOENT;
	}

	lv_obj_set_style_bg_color(scr, tmp_res.scene.background, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

	/* button */
	for (int i = 0; i < NUM_BTNS; i++) {
		data->btn[i] = lv_imgbtn_create(scr);
		lv_obj_set_pos(data->btn[i], tmp_res.pt_btn[i].x, tmp_res.pt_btn[i].y);
		lv_obj_set_size(data->btn[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_user_data(data->btn[i], (void*)i);
		lv_obj_add_flag(data->btn[i], LV_OBJ_FLAG_CHECKABLE);
		lv_obj_add_event_cb(data->btn[i], _btn_evt_handler, LV_EVENT_VALUE_CHANGED, scr);

		lv_imgbtn_set_src(data->btn[i], LV_IMGBTN_STATE_RELEASED, NULL, &data->img_dsc_def[i], NULL);
		lv_imgbtn_set_src(data->btn[i], LV_IMGBTN_STATE_PRESSED, NULL, &data->img_dsc_sel[i], NULL);
		lv_imgbtn_set_src(data->btn[i], LV_IMGBTN_STATE_CHECKED_RELEASED, NULL, &data->img_dsc_sel[i], NULL);
		lv_imgbtn_set_src(data->btn[i], LV_IMGBTN_STATE_CHECKED_PRESSED, NULL, &data->img_dsc_def[i], NULL);
	}

	lv_obj_clear_flag(data->btn[BTN_BLUETOOTH], LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(data->btn[BTN_SETTING], LV_OBJ_FLAG_CHECKABLE);

	/* BTN_SETTING 不可 CHECKABLE，不会产生 VALUE_CHANGED，改为单独绑 CLICKED 打开设置页 */
	lv_obj_add_event_cb(data->btn[BTN_SETTING], _btn_setting_evt_handler, LV_EVENT_CLICKED, scr);

	/* battery */
	data->img[IMG_BAT] = lv_img_create(scr);
	lv_img_set_src(data->img[IMG_BAT], &data->img_dsc_bat[0]);
	lv_obj_set_pos(data->img[IMG_BAT], tmp_res.pt_bat[0].x, tmp_res.pt_bat[0].y);

	/* date & time */
	for (int i = 0; i < NUM_TXTS; i++) {
		data->lbl[i] = text_canvas_create(scr);
		lv_obj_set_pos(data->lbl[i], tmp_res.res_txt[i].x, tmp_res.res_txt[i].y);
		lv_obj_set_style_text_font(data->lbl[i], &data->font, LV_PART_MAIN);
		lv_obj_set_style_text_color(data->lbl[i], tmp_res.res_txt[i].color, LV_PART_MAIN);
		lv_obj_set_style_text_align(data->lbl[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
		text_canvas_set_text_static(data->lbl[i], "");
	}

	lv_obj_set_user_data(scr, data);

	/* status update */
	_update_btn_status(data);

	_ctrl_paint(scr);

	return 0;
}

static int _ctrl_delete(lv_obj_t * scr)
{
	ctrl_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	lv_obj_clean(scr);

	lvgl_res_unload_pictures(data->img_dsc_def, NUM_BTNS);
	lvgl_res_unload_pictures(data->img_dsc_sel, NUM_BTNS);
	lvgl_res_unload_pictures(data->img_dsc_bat, NUM_BATS);

	LVGL_FONT_CLOSE(&data->font);

	app_mem_free(data);

	return 0;
}

int ctrl_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _ctrl_create(scr, msg_data);
	case MSG_VIEW_PAINT:
		return _ctrl_paint(scr);
	case MSG_VIEW_FOCUS:
		return _ctrl_paint(scr);
	case MSG_VIEW_DELETE:
		return _ctrl_delete(scr);
	default:
		return 0;
	}
}
