/*
 * Copyright (c) 2022 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include "alipay_ui.h"

#define TIMER_PERIOD		(1000)
#define TIMEOUT_PAY			(60*1000)

enum {
	TXT_PAY_TITLE = 0,
	TXT_PAY_SW_QR,
	TXT_PAY_SW_BAR,
	NUM_TXTS,
};

const static uint32_t _txt_ids[] = {
	STR_PAY_TITLE,
	STR_PAY_SWITCH_QR,
	STR_PAY_SWITCH_BAR,
};

typedef struct alipay_pay_view_data_s {
	lv_obj_t* pay_title;
	lv_obj_t* pay_qrcode;
	lv_obj_t* pay_barcode;
	lv_obj_t* pay_btn;
	lv_obj_t* pay_code;

	lv_font_t font;
	lv_style_t sty_lbl;
	lv_style_t sty_lbl_pr;
	lv_timer_t *timer;

	lvgl_res_scene_t res_scene;
	lvgl_res_string_t res_txt[NUM_TXTS];

	/* user data*/
	int status_elaps;
	int first_update;

	const alipay_view_presenter_t *presenter;
} alipay_pay_view_data_t;

static void _pay_btn_evt_handler(lv_event_t * e);

static void _create_ui_pay(lv_obj_t *scr)
{
	alipay_pay_view_data_t *data = lv_obj_get_user_data(scr);
	const alipay_view_presenter_t *presenter = data->presenter;
	bool is_barcode = presenter->is_barcode_on();
	uint32_t buf_len = 128;
	char *str_buf;

	// malloc string buffer
	str_buf = app_mem_malloc(buf_len);
	if (!str_buf) {
		return;
	}

	// get paycode string
	presenter->get_paycode_string(str_buf, &buf_len);

	// set bg color
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);

	// init the style for label
	lv_style_reset(&data->sty_lbl);
	lv_style_set_text_font(&data->sty_lbl, &data->font);
	lv_style_set_text_color(&data->sty_lbl, lv_color_white());

	// init the style for btn
	lv_style_reset(&data->sty_lbl_pr);
	lv_style_set_text_font(&data->sty_lbl_pr, &data->font);
	lv_style_set_text_color(&data->sty_lbl_pr, lv_palette_main(LV_PALETTE_BLUE));

	if (is_barcode) {
		// create barcode
		data->pay_barcode = lv_barcode_create(scr, 240, 100, lv_color_black(), lv_color_white());
		lv_barcode_update(data->pay_barcode, str_buf, buf_len, barcodegen_128C);
		lv_obj_center(data->pay_barcode);
		lv_obj_set_style_border_color(data->pay_barcode, lv_color_white(), 0);
		lv_obj_set_style_border_width(data->pay_barcode, 8, 0);
	} else {
		// create qrcode
		data->pay_qrcode = lv_qrcode_create(scr, 200, lv_color_black(), lv_color_white());
		lv_qrcode_update(data->pay_qrcode, str_buf, buf_len, qrcodegen_Ecc_QUARTILE);
		lv_obj_center(data->pay_qrcode);
		lv_obj_set_style_border_color(data->pay_qrcode, lv_color_white(), 0);
		lv_obj_set_style_border_width(data->pay_qrcode, 8, 0);
	}

	// create title
	data->pay_title = lv_label_create(scr);
	lv_label_set_text(data->pay_title, data->res_txt[TXT_PAY_TITLE].txt);
	lv_obj_add_style(data->pay_title, &data->sty_lbl, LV_PART_MAIN);
	if (is_barcode) {
		lv_obj_align_to(data->pay_title, data->pay_barcode, LV_ALIGN_OUT_TOP_MID, 0, -5);
	} else {
		lv_obj_align_to(data->pay_title, data->pay_qrcode, LV_ALIGN_OUT_TOP_MID, 0, -5);
	}

	// create paycode
	if (is_barcode) {
		data->pay_code = lv_label_create(scr);
		lv_label_set_text(data->pay_code, str_buf);
		lv_obj_add_style(data->pay_code, &data->sty_lbl, LV_PART_MAIN);
		lv_obj_align_to(data->pay_code, data->pay_barcode, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
	}

	// create btn
	data->pay_btn = lv_label_create(scr);
	lv_obj_add_style(data->pay_btn, &data->sty_lbl_pr, LV_PART_MAIN);
	if (is_barcode) {
		lv_label_set_text(data->pay_btn, data->res_txt[TXT_PAY_SW_QR].txt);
		lv_obj_align_to(data->pay_btn, data->pay_code, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
	} else {
		lv_label_set_text(data->pay_btn, data->res_txt[TXT_PAY_SW_BAR].txt);
		lv_obj_align_to(data->pay_btn, data->pay_qrcode, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
	}
	lv_obj_add_flag(data->pay_btn, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(data->pay_btn, _pay_btn_evt_handler, LV_EVENT_SHORT_CLICKED, scr);

	// free string buffer
	app_mem_free(str_buf);
}

static void _delete_ui_pay(lv_obj_t *scr)
{
	alipay_pay_view_data_t *data = lv_obj_get_user_data(scr);

	if (data->pay_title) {
		lv_obj_del(data->pay_title);
		data->pay_title = NULL;
	}
	if (data->pay_qrcode) {
		lv_qrcode_delete(data->pay_qrcode);
		data->pay_qrcode = NULL;
	}
	if (data->pay_barcode) {
		lv_barcode_delete(data->pay_barcode);
		data->pay_barcode = NULL;
	}
	if (data->pay_code) {
		lv_obj_del(data->pay_code);
		data->pay_code = NULL;
	}
	if (data->pay_btn) {
		lv_obj_del(data->pay_btn);
		data->pay_btn = NULL;
	}
}

static void _rebuild_ui_pay(lv_obj_t *scr)
{
	alipay_pay_view_data_t *data = lv_obj_get_user_data(scr);

	// rebuild bind ui
	_delete_ui_pay(scr);
	_create_ui_pay(scr);

	// update status counter
	data->status_elaps = 0;
}

static void _pay_btn_evt_handler(lv_event_t * e)
{
	lv_obj_t *scr = lv_event_get_user_data(e);
	alipay_pay_view_data_t *data = lv_obj_get_user_data(scr);
	const alipay_view_presenter_t *presenter = data->presenter;

	// switch paycode
	presenter->toggle_barcode();

	// rebuild pay view
	_rebuild_ui_pay(scr);
}

static void _status_timer_cb(lv_timer_t *timer)
{
	lv_obj_t *scr = timer->user_data;
	alipay_pay_view_data_t *data = lv_obj_get_user_data(scr);

	// update status counter
	data->status_elaps += TIMER_PERIOD;
	if (data->status_elaps >= TIMEOUT_PAY) {
		// refresh paycode every 60 secs
		_rebuild_ui_pay(scr);
	}
}

static int _alipay_pay_view_create(lv_obj_t * scr, const alipay_view_presenter_t *presenter)
{
	alipay_pay_view_data_t *data;
	int ret;

	data = app_mem_malloc(sizeof(*data));
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;
	lv_obj_set_user_data(scr, data);

	// load scene
	ret = lvgl_res_load_scene(SCENE_ALIPAY_PAY_VIEW, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_ALIPAY_PAY_VIEW not found");
		lv_obj_set_user_data(scr, NULL);
		app_mem_free(data);
		return -ENOENT;
	}

	lvgl_res_load_strings_from_scene(&data->res_scene, _txt_ids, data->res_txt, NUM_TXTS);

	LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_NORMAL);

	lv_style_init(&data->sty_lbl);
	lv_style_init(&data->sty_lbl_pr);

	data->timer = lv_timer_create(_status_timer_cb, TIMER_PERIOD, scr);

	_rebuild_ui_pay(scr);

	return 0;
}

static int _alipay_pay_view_delete(lv_obj_t * scr)
{
	alipay_pay_view_data_t *data = lv_obj_get_user_data(scr);
	const alipay_view_presenter_t *presenter = data->presenter;

	if (data == NULL)
		return 0;

	// stop adv
	presenter->stop_adv();
	// unlock screen
	presenter->unlock_screen();

	if (data->timer) {
		lv_timer_del(data->timer);
		data->timer = NULL;
	}

	_delete_ui_pay(scr);

	lv_style_reset(&data->sty_lbl);
	lv_style_reset(&data->sty_lbl_pr);

	lvgl_res_unload_strings(data->res_txt, NUM_TXTS);
	lvgl_res_unload_scene(&data->res_scene);
	LVGL_FONT_CLOSE(&data->font);

	app_mem_free(data);

	return 0;
}

static int _alipay_pay_view_focus_changed(lv_obj_t * scr, bool focused)
{
	alipay_pay_view_data_t *data = lv_obj_get_user_data(scr);
	const alipay_view_presenter_t *presenter = data->presenter;

	if (focused) {
		// lock screen
		presenter->lock_screen();
		// start adv
		presenter->start_adv();

		if (data) {
			lv_timer_resume(data->timer);
		}
	} else {
		// stop adv
		presenter->stop_adv();
		// unlock screen
		presenter->unlock_screen();

		if (data) {
			lv_timer_pause(data->timer);
		}
	}

	return 0;
}

int alipay_pay_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _alipay_pay_view_create(scr, msg_data);
	case MSG_VIEW_DELETE:
		return _alipay_pay_view_delete(scr);
	case MSG_VIEW_FOCUS:
		return _alipay_pay_view_focus_changed(scr, true);
	case MSG_VIEW_DEFOCUS:
		return _alipay_pay_view_focus_changed(scr, false);
	default:
		return 0;
	}
}
