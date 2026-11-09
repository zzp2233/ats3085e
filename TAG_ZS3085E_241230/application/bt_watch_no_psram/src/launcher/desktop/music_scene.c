/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <mem_manager.h>
#include <widgets/watch_clock.h>
#include <widgets/simple_img.h>
#include <widgets/anim_img.h>
#include <widgets/img_number.h>
#include "desktop_ui.h"
#include "../launcher_app.h"

#ifdef CONFIG_BT_TRANSMIT
#  include <bt_transmit/bt_transmit.h>
#endif

#ifdef CONFIG_LCMUSIC_APP
#  include <lcmusic/lcmusic.h>
#endif

#define MAX_SUPPORT_CNT	2
#define MUSIC_INFO_UPDATE_PERIOD	300
#ifdef CONFIG_LCMUSIC_APP
#  define DISPLAY_MORE_VIEW	(1)
#else
#  define DISPLAY_MORE_VIEW	(0)
#endif

enum {
	BTN_PLAY = 0,
	//BTN_STOP,
	BTN_PRE,
	BTN_NEXT,
	BTN_SEQ_PLAY_M,
	//BTN_LIST_LPLAY_M,
	//BTN_RAND_PLAY_M,
	//BTN_SING_LOOP_PLAY_M,
	BTN_VOL,
#if DISPLAY_MORE_VIEW
	BTN_MORE,
#endif
	BTN_BMP_MUSIC_SOURCE,

	NUM_BTNS,
};

enum {
	BMP_BG = 0,
	NUM_IMGS,
};

enum {
	TXT_SONG_NAME = 0,
	TXT_ALBUM,
	NUM_TXTS,
};

enum {
	BTN_VOLUP = 0,
	BTN_VOLDOWN,

	NUM_VOL_BTNS,
};

enum {
	BTN_WATCH= 0,
#if DEF_UI_WIDTH >= 454
	BTN_WATCH_TXT,
	BTN_WATCH_CH,
#endif
	BTN_PHONE,
#if DEF_UI_WIDTH >= 454
	BTN_PHONE_TXT,
	BTN_PHONE_CH,
#endif
	BTN_BT_EARPHONE,
#if DEF_UI_WIDTH >= 454
	BTN_BT_EARPHONE_TXT,
	BTN_BT_EARPHONE_CH,
#endif
	NUM_MORE_BTNS,
};

enum {
	BMP_BT_CONNECTED = 0,
	BMP_BT_DISCONNECT,

	NUM_BT_STATE_IMGS,
};

enum {
	TXT_BT_EARPHONE_LIST_NAME = 0,
	TXT_BT_EARPHONE_NAME1,
	TXT_BT_EARPHONE_NAME1_CNT_STATE,
	TXT_BT_EARPHONE_NAME2,
	TXT_BT_EARPHONE_NAME2_CNT_STATE,

	NUM_BT_EARPHONE_LIST_TXTS,
};

enum {
	BTN_BT_EARPHONE_UPDATE = 0,

	NUM_BT_EARPHONE_LIST_BTNS,
};

typedef struct {
	/* lvgl object */
	lv_obj_t *btn[NUM_BTNS];
	lv_obj_t *bmp[NUM_IMGS];
	lv_obj_t *lbl[NUM_TXTS];
	lv_obj_t *bt_state;
	/*volume adjust*/
	lv_obj_t *vol_btn[NUM_VOL_BTNS];
	lv_obj_t *vol_bmp;
	lv_obj_t *vol_slider;
	/*more set*/
	lv_obj_t *more_btn[NUM_MORE_BTNS];
	lv_obj_t *more_bmp;

	/* ui-editor resource */
	lvgl_res_scene_t res_scene;
	lvgl_res_string_t res_txt[NUM_TXTS];

	/* lvgl resource */
	lv_point_t pt_def[NUM_BTNS];
	lv_point_t pt_sel[NUM_BTNS];
	lv_point_t pt_bmp[NUM_IMGS];
	lv_point_t pt_txt[NUM_TXTS];
	lv_point_t pt_vol_btn[NUM_VOL_BTNS];
	lv_point_t pt_more_btn[NUM_MORE_BTNS];
	lv_point_t pt_bt_state_bmp[NUM_BT_STATE_IMGS];

	lv_img_dsc_t img_dsc_def[NUM_BTNS];
	lv_img_dsc_t img_dsc_sel[NUM_BTNS];
	lv_img_dsc_t img_dsc_bmp[NUM_IMGS];
	lv_img_dsc_t img_dsc_vol_btn[NUM_VOL_BTNS];
	lv_img_dsc_t img_dsc_more_btn[NUM_MORE_BTNS];
	lv_img_dsc_t img_dsc_more_btn_sel[NUM_MORE_BTNS];
	lv_img_dsc_t img_dsc_bt_state_bmp[NUM_BT_STATE_IMGS];

#ifdef CONFIG_BT_TRANSMIT
	lv_obj_t *bt_earphone_name[NUM_BT_EARPHONE_LIST_TXTS];
	lv_obj_t *bt_earphone_update_btn[NUM_BT_EARPHONE_LIST_BTNS];
	lv_obj_t *bt_earphone_list_bg_img;

	lv_img_dsc_t img_dsc_bt_earphone_update_btn[NUM_BT_EARPHONE_LIST_BTNS];
	lv_point_t pt_bt_earphone_list_txt[NUM_BT_EARPHONE_LIST_TXTS];
	lv_point_t pt_bt_earphone_update_btn[NUM_BT_EARPHONE_LIST_BTNS];
	lv_style_t style_txt_bt_earphone_list[NUM_BT_EARPHONE_LIST_TXTS];

	lvgl_res_string_t res_txt_bt_earphone_list[NUM_BT_EARPHONE_LIST_TXTS];
#endif

	lv_style_t style_txt[NUM_TXTS];

	lv_font_t font;
	/* user data */
	uint32_t late_update_song_info : 1;/*0--don't need update,1--need to update*/
	uint32_t filter_update_pstate : 3;/*0--update play state,other--filter update play state one time*/
	uint32_t get_playback_pos : 8;/*0--get music playback pos */

	bool is_switching;

#ifdef CONFIG_BT_TRANSMIT
	uint32_t update_bt_earphone_list : 1;/*0--don't need update,1--need to update*/
	struct bt_device_info_t bt_earphone_info[MAX_SUPPORT_CNT];
#endif

	lv_timer_t *timer;

	music_presenter_id3_info_t *id3_info;
	const music_presenter_t *presenter;
	lv_obj_t *progress_slider;
	bool slider_setting;
	bool ignore_clicked;
} music_ui_data_t;

void lcmusic_display_song_info(char *song_info, bool direct_update);

static void _bmp_event_handler(lv_event_t * e);
static void _btn_event_handler(lv_event_t * e);
static void _music_view_update_process_task_cb(lv_timer_t *timer);

static void _display_vol_view(music_ui_data_t *data);
static void _exit_vol_view(music_ui_data_t *data);

#if DISPLAY_MORE_VIEW
static void _display_more_view(music_ui_data_t *data);
static void _exit_more_view(music_ui_data_t *data);
#endif

#ifdef CONFIG_BT_TRANSMIT
static void _display_bt_earphone_list_view(music_ui_data_t *data);
static void _exit_bt_earphone_list_view(music_ui_data_t *data);
static void _display_bt_earphone_list_info(music_ui_data_t *data);
static void _bt_earphone_info_update_check(music_ui_data_t *data);
static void _bt_earphone_list_info_update(struct bt_device_info_t *bt_info, uint8_t count);
#endif

static const uint32_t _bmp_ids[] = {
	PIC_BG,
};

static const uint32_t _btn_def_ids[] = {
	PIC_BTPL,
	PIC_BTPRE,
	PIC_BTNEXT,
	//PIC_BTSPM,
	PIC_BTLLPM,
	//PIC_BTRPM,
	//PIC_BTSLPM,
	PIC_BTVOL,
#if DISPLAY_MORE_VIEW
	PIC_BTMORE,
#endif
	PIC_WATCH_P,
};

static const uint32_t _btn_sel_ids[] = {
	PIC_BTSP,
	PIC_BTPRE,
	PIC_BTNEXT,
	PIC_BTSLPM,
	PIC_BTVOL,
#if DISPLAY_MORE_VIEW
	PIC_BTMORE,
#endif
	PIC_PHONE_P,
};

static const uint32_t vol_btn_def_ids[] = {
	PIC_BTN_VOLUP,
	PIC_BTN_VOLDOWN,
};

#if DISPLAY_MORE_VIEW
static const uint32_t more_btn_def_ids[] = {
	PIC_BTN_WATCH,
#if DEF_UI_WIDTH >= 454
	PIC_BTN_WATCH_TXT,
	PIC_BTN_WATCH_UNCH,
#endif
	PIC_BTN_PHONE,
#if DEF_UI_WIDTH >= 454
	PIC_BTN_PHONE_TXT,
	PIC_BTN_PHONE_UNCH,
#endif
	PIC_BTN_BT,
#if DEF_UI_WIDTH >= 454
	PIC_BTN_BT_TXT,
	PIC_BTN_BT_UNCH,
#endif
};
#endif

static const uint32_t _txt_ids[] = {
	STR_SN,	STR_ALBUM,
};

#if DISPLAY_MORE_VIEW
const static uint32_t bt_state_bmp_ids[] = {
	PIC_BT_CONNECTED,
	PIC_BT_DISCONNECT,
};

static const uint32_t more_btn_sel_ids[] = {
	PIC_BTN_WATCH,
#if DEF_UI_WIDTH >= 454
	PIC_BTN_WATCH_TXT,
	PIC_BTN_WATCH_CH,
#endif
	PIC_BTN_PHONE,
#if DEF_UI_WIDTH >= 454
	PIC_BTN_PHONE_TXT,
	PIC_BTN_PHONE_CH,
#endif
	PIC_BTN_BT,
#if DEF_UI_WIDTH >= 454
	PIC_BTN_BT_TXT,
	PIC_BTN_BT_CH,
#endif
};
#endif /* DISPLAY_MORE_VIEW */

#ifdef CONFIG_BT_TRANSMIT
static const uint32_t bt_earphone_update_btn_ids[] = {
	PIC_BTN_BT_EARPHONE_UPDATE,
};

static const uint32_t bt_earphone_list_txt_ids[] = {
	STR_BT_EARPHONE_LIST_NAME,
	STR_BT_EARPHONE_NAME1,
	STR_BT_EARPHONE_NAME1_CNT_STATE,
	STR_BT_EARPHONE_NAME2,
	STR_BT_EARPHONE_NAME2_CNT_STATE,
};
#endif /* CONFIG_BT_TRANSMIT */

static music_ui_data_t *p_music_data;

static OS_MUTEX_DEFINE(music_view_mutex);

static void _delete_obj_array(lv_obj_t **pobj, uint32_t num)
{
	for (int i = 0; i < num; i++) {
		if (pobj[i]) {
			lv_obj_del(pobj[i]);
			pobj[i] = NULL;
		}
	}
}

static void _cvt_txt_array(lv_point_t *pt, lv_style_t *sty, lv_font_t *font,
				lvgl_res_string_t* txt, uint8_t align, uint32_t num)
{
	int i;

	for (i = 0; i < num; i++) {
		pt[i].x = txt[i].x;
		pt[i].y = txt[i].y;

		lv_style_init(&sty[i]);
		lv_style_set_text_font(&sty[i], font);
		lv_style_set_text_color(&sty[i], txt[i].color);
		lv_style_set_text_align(&sty[i], align);
	}
}

__unused
static void _create_img_array(lv_obj_t *par, lv_obj_t **pobj, lv_point_t *pt,
				lv_img_dsc_t *img, uint32_t num)
{
	int i;

	for (i = 0; i < num; i++) {
		pobj[i] = lv_img_create(par);
		lv_img_set_src(pobj[i], &img[i]);
		lv_obj_set_pos(pobj[i], pt[i].x, pt[i].y);
		lv_obj_set_size(pobj[i], img[i].header.w, img[i].header.h);
	}
}

static void _create_btn_array(lv_obj_t *par, lv_obj_t **pobj, lv_point_t *pt,
				lv_img_dsc_t *def, lv_img_dsc_t *sel, uint32_t num,
				lv_event_cb_t event_cb, void *event_data)
{
	int i;

	for (i = 0; i < num; i++) {
		pobj[i] = lv_imgbtn_create(par);
		lv_obj_set_pos(pobj[i], pt[i].x, pt[i].y);
		lv_obj_set_size(pobj[i], def[i].header.w, def[i].header.h);
		lv_obj_set_user_data(pobj[i], (void*)i);
		lv_obj_add_event_cb(pobj[i], event_cb, LV_EVENT_CLICKED, event_data);

		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_RELEASED, NULL, &def[i], NULL);
		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_PRESSED, NULL, &sel[i], NULL);
		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_CHECKED_RELEASED, NULL, &sel[i], NULL);
		lv_imgbtn_set_src(pobj[i], LV_IMGBTN_STATE_CHECKED_PRESSED, NULL, &def[i], NULL);
	}
}

static void _create_label_array(lv_obj_t *par, lv_obj_t **pobj, lv_point_t *pt,
				lv_style_t *sty, lvgl_res_string_t *res_txt, uint32_t num)
{
	int i;

	for (i = 0; i < num; i++) {
		pobj[i] = lv_label_create(par);
		lv_label_set_text(pobj[i], "");
		lv_label_set_long_mode(pobj[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
		lv_obj_set_style_anim_speed(pobj[i], 20, LV_PART_MAIN);

		lv_obj_add_style(pobj[i], &sty[i], LV_PART_MAIN);
		lv_obj_set_pos(pobj[i], pt[i].x, pt[i].y);
		//lv_obj_set_size(pobj[i], res_txt[i].width, res_txt[i].height);
		lv_obj_set_width(pobj[i], res_txt[i].width);

		SYS_LOG_INF("x:%d y=%d width =%d height=%d\n",
			pt[i].x, pt[i].y, res_txt[i].width, res_txt[i].height);
	}
}

static void btn_state_toggle(lv_obj_t * btn)
{
	if (lv_obj_has_state(btn, LV_STATE_CHECKED)) {
		lv_obj_clear_state(btn, LV_STATE_CHECKED);
	} else {
		lv_obj_add_state(btn, LV_STATE_CHECKED);
	}
}

static void _set_view_play_state(music_ui_data_t *data, bool playing)
{
	uint8_t btn_state = lv_obj_get_state(data->btn[BTN_PLAY]);

	if (!(btn_state & LV_STATE_PRESSED) &&
		((playing && ((btn_state & LV_STATE_CHECKED) == 0))
		|| (!playing && ((btn_state & LV_STATE_CHECKED) == LV_STATE_CHECKED)))) {
		btn_state_toggle(data->btn[BTN_PLAY]);
		lv_obj_invalidate(data->btn[BTN_PLAY]);
	}
}

static void _display_song_name_album(music_ui_data_t *data)
{
	uint8_t player = data->presenter->get_play_type();
	bool update = false;

	if (player == BTMUSIC_PLAYER) {
		update = data->presenter->is_remote_id3_updated();
	} else {
		update = data->late_update_song_info;
	}

	if (update) {
		update = data->presenter->get_id3_info(data->id3_info);
		if (update) {
			data->late_update_song_info = 0;

			lv_label_set_text_static(data->lbl[TXT_SONG_NAME], data->id3_info->name);
			lv_label_set_text_static(data->lbl[TXT_ALBUM], data->id3_info->album);

			SYS_LOG_INF("song_name:%s album:%s\n", data->id3_info->name, data->id3_info->album);
		}
	}
}

#ifdef CONFIG_LCMUSIC_APP
static void _music_play_view(music_ui_data_t *data, uint8_t cur_player, uint8_t prev_player)
{
	uint8_t btn_state;

	btn_state_toggle(data->btn[BTN_BMP_MUSIC_SOURCE]);
	lv_obj_invalidate(data->btn[BTN_BMP_MUSIC_SOURCE]);

	lv_label_set_text(data->lbl[TXT_SONG_NAME], "");
	lv_label_set_text(data->lbl[TXT_ALBUM], "");

	if (cur_player == BTMUSIC_PLAYER) {
		lv_obj_add_flag(data->btn[BTN_SEQ_PLAY_M], LV_OBJ_FLAG_HIDDEN);
		data->presenter->check_remote_state();
	} else {
		lv_obj_clear_flag(data->btn[BTN_SEQ_PLAY_M], LV_OBJ_FLAG_HIDDEN);
		lcmusic_display_song_info(NULL, true);
	}

	btn_state = lv_obj_get_state(data->btn[BTN_PLAY]);
	if (btn_state == LV_STATE_PRESSED || btn_state == LV_STATE_CHECKED) {
		btn_state_toggle(data->btn[BTN_PLAY]);
		lv_obj_invalidate(data->btn[BTN_PLAY]);
	}
}
#endif

#ifdef CONFIG_LCMUSIC_APP
void lcmusic_display_song_info(char *song_info, bool direct_update)
{
	music_ui_data_t *data = p_music_data;
	if (data == NULL)
		return;

	data->late_update_song_info = 1;

	/*direct update song info must in ui_service thread*/
	if (direct_update)
		_display_song_name_album(data);

	data->is_switching = false;
}
#endif /* CONFIG_LCMUSIC_APP */

static void _display_play_progress(music_ui_data_t *data)
{
	static int32_t ref_value = 0;
	uint32_t total_time = 0;
	uint32_t cur_time = 0;
	int32_t set_value = 0;
	int32_t cur_value;

	cur_value = lv_slider_get_value(data->progress_slider);
	if(cur_value != ref_value) {
		ref_value = cur_value;
		return;
	}

	if(data->slider_setting || data->presenter->is_seeking_time()) {
		return;
	}

	data->presenter->get_play_time(&cur_time, &total_time);

	if (total_time) {
		set_value = 100 * cur_time / total_time;
		lv_slider_set_value(data->progress_slider, set_value, LV_ANIM_ON);
		SYS_LOG_INF("_display_play_progress, total_time=%d cur_time=%d value=%d\n", total_time, cur_time, set_value);
	} else {
		lv_slider_set_value(data->progress_slider, 0, LV_ANIM_OFF);
	}
	ref_value = set_value;
}

static void _music_view_update_process_task_cb(lv_timer_t *timer)
{
	music_ui_data_t *data = timer->user_data;
	uint8_t player = data->presenter->get_play_type();

	_display_play_progress(data);

	if (data->filter_update_pstate == 0) {
		bool playing = data->presenter->is_playing();

		_set_view_play_state(data, playing);

		if (player == BTMUSIC_PLAYER) {
			data->get_playback_pos++;
			if (playing && (data->get_playback_pos * MUSIC_INFO_UPDATE_PERIOD >= 1000)) {
				data->presenter->check_remote_playpos();
				data->get_playback_pos = 0;
			}
		}
	} else {
		data->filter_update_pstate--;
	}

	if (player == BTMUSIC_PLAYER && data->vol_slider) {
		if (data->presenter->is_remote_vol_updated())
			lv_slider_set_value(data->vol_slider, data->presenter->get_volume(), LV_ANIM_OFF);
	}

	_display_song_name_album(data);

	os_mutex_lock(&music_view_mutex, OS_FOREVER);

#ifdef CONFIG_BT_TRANSMIT
	if (data->update_bt_earphone_list) {
		data->update_bt_earphone_list = 0;
		_display_bt_earphone_list_info(data);
	} else if (data->bt_earphone_list_bg_img && data->bt_earphone_info[0].name) {
		_bt_earphone_info_update_check(data);
	}
#endif

	os_mutex_unlock(&music_view_mutex);
}

static void _bmp_event_handler(lv_event_t * e)
{
	lv_obj_t *obj = lv_event_get_current_target(e);
	music_ui_data_t *data = lv_event_get_user_data(e);
	uint8_t gesture = lv_indev_get_gesture_dir(lv_event_get_param(e));

	if (data->vol_bmp == obj) {
		if (gesture & LV_DIR_HOR) {
			_exit_vol_view(data);
			lv_indev_wait_release(lv_indev_get_act());
		}
	}

#ifdef CONFIG_BT_TRANSMIT
	if (data->bt_earphone_list_bg_img == obj) {
		_exit_bt_earphone_list_view(data);
		lv_indev_wait_release(lv_indev_get_act());
		_display_more_view(data);
	}
#endif
}

static void _btn_event_handler(lv_event_t * e)
{
	lv_obj_t *obj = lv_event_get_current_target(e);
	music_ui_data_t *data = lv_event_get_user_data(e);
	uint8_t player = data->presenter->get_play_type();

	if (data->ignore_clicked)
		return;

	SYS_LOG_INF("%p Clicked\n", obj);

	uint8_t btn_state = 0;

	if (data->btn[BTN_PLAY] == obj) {
		btn_state = lv_obj_get_state(data->btn[BTN_PLAY]);
		data->filter_update_pstate = 600 / MUSIC_INFO_UPDATE_PERIOD;
		if (btn_state == LV_STATE_PRESSED || ((btn_state & (LV_STATE_CHECKED | LV_STATE_PRESSED)) == LV_STATE_CHECKED)) {
			data->presenter->play();
		} else {
			data->presenter->pause(false);
		}

		/* use the feedback play state by phone */
		if (player == BTMUSIC_PLAYER) {
			btn_state_toggle(data->btn[BTN_PLAY]);
			lv_obj_invalidate(data->btn[BTN_PLAY]);
		}
	}

	if (data->btn[BTN_PRE] == obj) {
		if (player == LCMUSIC_PLAYER) {
			if (!data->is_switching) {
				data->presenter->play_prev();
				data->is_switching = true;
			}
		} else {
			data->presenter->play_prev();
		}

		btn_state = lv_obj_get_state(data->btn[BTN_PLAY]);
		if ((btn_state & (LV_STATE_PRESSED | LV_STATE_CHECKED)) == 0 || btn_state == (LV_STATE_PRESSED | LV_STATE_CHECKED)) {
			btn_state_toggle(data->btn[BTN_PLAY]);
			lv_obj_invalidate(data->btn[BTN_PLAY]);
		}
	}

	if (data->btn[BTN_NEXT] == obj) {
		if (player == LCMUSIC_PLAYER) {
			if (!data->is_switching) {
				data->presenter->play_next();
				data->is_switching = true;
			}
		} else {
			data->presenter->play_next();
		}

		btn_state = lv_obj_get_state(data->btn[BTN_PLAY]);
		if ((btn_state & (LV_STATE_PRESSED | LV_STATE_CHECKED)) == 0 || btn_state == (LV_STATE_PRESSED | LV_STATE_CHECKED)) {
			btn_state_toggle(data->btn[BTN_PLAY]);
			lv_obj_invalidate(data->btn[BTN_PLAY]);
		}
	}

	if (data->btn[BTN_SEQ_PLAY_M] == obj) {
		btn_state = lv_obj_get_state(data->btn[BTN_SEQ_PLAY_M]);
		data->presenter->set_play_mode((btn_state & LV_STATE_CHECKED) ? 1 : 0);
	}

	if (data->btn[BTN_VOL] == obj) {
		_display_vol_view(data);
	}

#if DISPLAY_MORE_VIEW
	if (data->btn[BTN_MORE] == obj) {
		_display_more_view(data);
	}
#endif
}


static void _vol_view_btn_event_cb(lv_event_t * e)
{
	music_ui_data_t *data = lv_event_get_user_data(e);
	lv_obj_t *btn = lv_event_get_target(e);
	int vol = 0;

	if (btn == data->vol_btn[BTN_VOLUP]) {
		vol = lv_slider_get_max_value(data->vol_slider);
	} else if (btn == data->vol_btn[BTN_VOLDOWN]) {
		vol = lv_slider_get_min_value(data->vol_slider);
	} else {
		return;
	}

	if (data->presenter)
		data->presenter->set_volume(vol);

	lv_slider_set_value(data->vol_slider, vol, LV_ANIM_OFF);
}

static void _vol_view_slider_event_cb(lv_event_t * e)
{
	music_ui_data_t *data = lv_event_get_user_data(e);
	int vol = lv_slider_get_value(data->vol_slider);

	if (data->presenter)
		data->presenter->set_volume(vol);
}

static void _display_vol_view(music_ui_data_t *data)
{
	/* load resource */
	lvgl_res_load_pictures_from_scene(&data->res_scene, vol_btn_def_ids,
			data->img_dsc_vol_btn, data->pt_vol_btn, NUM_VOL_BTNS);

	data->vol_bmp = lv_obj_create(data->bmp[BMP_BG]);
	lv_obj_set_size(data->vol_bmp, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(data->vol_bmp, lv_color_make(0x3b, 0x3b, 0x3b), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(data->vol_bmp, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_add_event_cb(data->vol_bmp, _bmp_event_handler, LV_EVENT_GESTURE, data);
	lv_obj_clear_flag(data->vol_bmp, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

	/* create button */
	_create_btn_array(data->vol_bmp, data->vol_btn, data->pt_vol_btn,
			data->img_dsc_vol_btn, data->img_dsc_vol_btn, NUM_VOL_BTNS,
			_vol_view_btn_event_cb, data);

	/* create bar to display volume bar */
	data->vol_slider = lv_slider_create(data->vol_bmp);
	lv_obj_set_size(data->vol_slider, 6, LV_PCT(62));
	lv_obj_set_ext_click_area(data->vol_slider, LV_DPX(32));
	lv_obj_center(data->vol_slider);
	lv_obj_set_style_bg_color(data->vol_slider, lv_color_make(0xC0, 0xC0, 0xC0), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(data->vol_slider, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_radius(data->vol_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(data->vol_slider, lv_color_make(0, 0, 255), LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(data->vol_slider, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(data->vol_slider, lv_color_make(0, 0, 255), LV_PART_KNOB);
	lv_obj_set_style_bg_opa(data->vol_slider, LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_radius(data->vol_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
	lv_obj_set_style_pad_all(data->vol_slider, 6, LV_PART_KNOB);
	lv_obj_add_event_cb(data->vol_slider, _vol_view_slider_event_cb, LV_EVENT_VALUE_CHANGED, data);

	if (data->presenter) {
		uint8_t min_vol = 0, max_vol = 15;
		data->presenter->get_volume_range(&min_vol, &max_vol);
		lv_slider_set_range(data->vol_slider, min_vol, max_vol);
		lv_slider_set_value(data->vol_slider, data->presenter->get_volume(), LV_ANIM_OFF);
	} else {
		lv_slider_set_range(data->vol_slider, 0, 15);
		lv_slider_set_value(data->vol_slider, 0, LV_ANIM_OFF);
	}

	app_ui_set_scene_scrollable(false);
}

static void _exit_vol_view(music_ui_data_t *data)
{
	if (data->vol_bmp == NULL)
		return;

	_delete_obj_array(data->vol_btn, NUM_VOL_BTNS);
	lv_obj_del(data->vol_bmp);
	data->vol_bmp = NULL;
	data->vol_slider = NULL;

	lvgl_res_unload_pictures(data->img_dsc_vol_btn, NUM_VOL_BTNS);

	lv_indev_wait_release(lv_indev_get_act());
	app_ui_set_scene_scrollable(true);
}

#if DISPLAY_MORE_VIEW
static void _create_bt_connect_state_img(music_ui_data_t *data)
{
	if (data->bt_state) {
		lv_obj_del(data->bt_state);
		data->bt_state = NULL;
	}

	data->bt_state = lv_img_create(data->more_bmp);
#ifdef CONFIG_BT_TRANSMIT
	if (bt_manager_trs_get_connected_dev_num()) {
		lv_img_set_src(data->bt_state, &data->img_dsc_bt_state_bmp[BMP_BT_CONNECTED]);
		lv_obj_set_pos(data->bt_state, data->pt_bt_state_bmp[BMP_BT_CONNECTED].x, data->pt_bt_state_bmp[BMP_BT_CONNECTED].y);
	}
	else
#endif
	{
		lv_img_set_src(data->bt_state, &data->img_dsc_bt_state_bmp[BMP_BT_DISCONNECT]);
		lv_obj_set_pos(data->bt_state, data->pt_bt_state_bmp[BMP_BT_DISCONNECT].x, data->pt_bt_state_bmp[BMP_BT_DISCONNECT].y);
	}
}

static void _more_view_btn_event_cb(lv_event_t * e)
{
	launcher_app_t *app = launcher_app_get();
	music_ui_data_t *data = lv_event_get_user_data(e);
	lv_obj_t *btn = lv_event_get_target(e);

	if (data->more_btn[BTN_WATCH] == btn
#if DEF_UI_WIDTH >= 454
		|| data->more_btn[BTN_WATCH_TXT] == btn
		|| data->more_btn[BTN_WATCH_CH] == btn
#endif
		) {
		/* delect more view */
		_exit_more_view(data);
		if (app->cur_player == BTMUSIC_PLAYER) {
			SYS_LOG_INF("switch to lcmusic player\n");
			data->presenter->pause(true);
			app->cur_player = LCMUSIC_PLAYER;
		#ifdef CONFIG_LCMUSIC_APP
			_music_play_view(data, app->cur_player, BTMUSIC_PLAYER);
		#endif
			data->presenter->set_play_type(LCMUSIC_PLAYER);
		}
	}

	if (data->more_btn[BTN_PHONE] == btn
#if DEF_UI_WIDTH >= 454
		|| data->more_btn[BTN_PHONE_TXT] == btn
		|| data->more_btn[BTN_PHONE_CH] == btn
#endif
		) {
		/* delect more view */
		_exit_more_view(data);
		if (app->cur_player == LCMUSIC_PLAYER) {
			SYS_LOG_INF("switch to btmusic player\n");
			data->presenter->pause(false);
			app->cur_player = BTMUSIC_PLAYER;
		#ifdef CONFIG_LCMUSIC_APP
			_music_play_view(data, app->cur_player, LCMUSIC_PLAYER);
		#endif
			data->presenter->set_play_type(BTMUSIC_PLAYER);
		}
	}

#ifdef CONFIG_BT_TRANSMIT
	if (data->presenter->get_play_type() == LCMUSIC_PLAYER &&
		(data->more_btn[BTN_BT_EARPHONE] == btn
#if DEF_UI_WIDTH >= 454
		|| data->more_btn[BTN_BT_EARPHONE_TXT] == btn
		|| data->more_btn[BTN_BT_EARPHONE_CH] == btn
#endif
		)) {
		/* delect more view */
		_exit_more_view(data);
		bt_transmit_inquiry_start_notify(_bt_earphone_list_info_update);
		_display_bt_earphone_list_view(data);
	}
#endif /* CONFIG_BT_TRANSMIT */
}

static void _display_more_view(music_ui_data_t *data)
{
	/* load resource */
	lvgl_res_load_pictures_from_scene(&data->res_scene, more_btn_def_ids,
			data->img_dsc_more_btn, data->pt_more_btn, NUM_MORE_BTNS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, more_btn_sel_ids,
			data->img_dsc_more_btn_sel, data->pt_more_btn, NUM_MORE_BTNS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, bt_state_bmp_ids,
			data->img_dsc_bt_state_bmp, data->pt_bt_state_bmp, NUM_BT_STATE_IMGS);

	/* create image */
	data->more_bmp = lv_obj_create(data->bmp[BMP_BG]);
	lv_obj_set_size(data->more_bmp, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(data->more_bmp, lv_color_make(0x3b, 0x3b, 0x3b), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(data->more_bmp, LV_OPA_COVER, LV_PART_MAIN);

	/* create button */
	_create_btn_array(data->more_bmp, data->more_btn, data->pt_more_btn,
			data->img_dsc_more_btn, data->img_dsc_more_btn_sel, NUM_MORE_BTNS,
			_more_view_btn_event_cb, data);

#if DEF_UI_WIDTH >= 454
	lv_obj_set_ext_click_area(data->more_btn[BTN_WATCH_CH],
			lv_obj_get_style_width(data->more_btn[BTN_WATCH_CH], LV_PART_MAIN));
	lv_obj_set_ext_click_area(data->more_btn[BTN_PHONE_CH],
			lv_obj_get_style_width(data->more_btn[BTN_PHONE_CH], LV_PART_MAIN));
	lv_obj_set_ext_click_area(data->more_btn[BTN_BT_EARPHONE_CH],
			lv_obj_get_style_width(data->more_btn[BTN_BT_EARPHONE_CH], LV_PART_MAIN));

	lv_obj_set_ext_click_area(data->more_btn[BTN_WATCH_TXT],
			lv_obj_get_style_height(data->more_btn[BTN_WATCH_TXT], LV_PART_MAIN));
	lv_obj_set_ext_click_area(data->more_btn[BTN_PHONE_TXT],
			lv_obj_get_style_height(data->more_btn[BTN_PHONE_TXT], LV_PART_MAIN));
	lv_obj_set_ext_click_area(data->more_btn[BTN_BT_EARPHONE_TXT],
			lv_obj_get_style_height(data->more_btn[BTN_BT_EARPHONE_TXT], LV_PART_MAIN));

	lv_obj_add_flag(data->more_btn[BTN_WATCH_CH], LV_OBJ_FLAG_CHECKABLE);
	lv_obj_add_flag(data->more_btn[BTN_PHONE_CH], LV_OBJ_FLAG_CHECKABLE);
	if (data->presenter->get_play_type() == BTMUSIC_PLAYER) {
		btn_state_toggle(data->more_btn[BTN_PHONE_CH]);
		lv_obj_invalidate(data->more_btn[BTN_PHONE_CH]);
	} else {
		btn_state_toggle(data->more_btn[BTN_WATCH_CH]);
		lv_obj_invalidate(data->more_btn[BTN_WATCH_CH]);
	}
#endif

	/* create bt connect state image */
	_create_bt_connect_state_img(data);

	app_ui_set_scene_scrollable(false);
}

static void _exit_more_view(music_ui_data_t *data)
{
	if (data->more_bmp == NULL)
		return;

	_delete_obj_array(data->more_btn, NUM_MORE_BTNS);

	lv_obj_del(data->more_bmp);
	data->more_bmp = NULL;
	data->bt_state = NULL;

	lvgl_res_unload_pictures(data->img_dsc_more_btn, NUM_MORE_BTNS);
	lvgl_res_unload_pictures(data->img_dsc_more_btn_sel, NUM_MORE_BTNS);
	lvgl_res_unload_pictures(data->img_dsc_bt_state_bmp, NUM_BT_STATE_IMGS);

	app_ui_set_scene_scrollable(true);
}
#endif /* DISPLAY_MORE_VIEW */

#ifdef CONFIG_BT_TRANSMIT
static void _bt_earphone_lbl_event_cb(lv_event_t * e)
{
	lv_event_code_t event = lv_event_get_code(e);
	music_ui_data_t *data = lv_event_get_user_data(e);
	lv_obj_t *obj = lv_event_get_target(e);

	if (event == LV_EVENT_SHORT_CLICKED) {
		lv_obj_set_user_data(obj, (void *)1);
	} else if (event == LV_EVENT_CLICKED) {
		bool clear_pairing = true;

		if (lv_obj_get_user_data(obj) != NULL) { /* long click */
			lv_obj_set_user_data(obj, NULL);
			clear_pairing = false;
		}

		lv_obj_t *name_objs[] = {
			data->bt_earphone_name[TXT_BT_EARPHONE_NAME1],
			data->bt_earphone_name[TXT_BT_EARPHONE_NAME2],
		};

		os_mutex_lock(&music_view_mutex, OS_FOREVER);

		for (int i = 1; i >= 0; i--) {
			if (obj != name_objs[i] || !data->bt_earphone_info[i].name)
				continue;

			if (data->bt_earphone_info[i].connected) {
				SYS_LOG_INF("disconnect %s", data->bt_earphone_info[i].name);
				bt_manager_disconnect_device(BTSRV_DISCONNECT_TRS_MODE); /* Or disconnect device by address */
				//bt_manager_br_disconnect(&data->bt_earphone_info[i].addr);
			} else if (clear_pairing == false) {
				SYS_LOG_INF("start connect %s", data->bt_earphone_info[i].name);
				bt_manager_trs_start_connect(data->bt_earphone_info[i].addr.val);
			}

			if (clear_pairing) {
				SYS_LOG_INF("clear pairing %s", data->bt_earphone_info[i].name);
				bt_manager_clear_trs_dev_info(&data->bt_earphone_info[i].addr);
			}
		}

		os_mutex_unlock(&music_view_mutex);
	}
}

static void _bt_earphone_btn_event_cb(lv_event_t * e)
{
	music_ui_data_t *data = lv_event_get_user_data(e);
	lv_obj_t *btn = lv_event_get_target(e);

	if (data->bt_earphone_update_btn[BTN_BT_EARPHONE_UPDATE] == btn) {
		bt_transmit_inquiry_start_notify(_bt_earphone_list_info_update);
	}
}

static void _display_bt_earphone_list_view(music_ui_data_t *data)
{
	/* load resource */
	lvgl_res_load_pictures_from_scene(&data->res_scene, bt_earphone_update_btn_ids,
			data->img_dsc_bt_earphone_update_btn, data->pt_bt_earphone_update_btn, NUM_BT_EARPHONE_LIST_BTNS);
	lvgl_res_load_strings_from_scene(&data->res_scene, bt_earphone_list_txt_ids,
			data->res_txt_bt_earphone_list, NUM_BT_EARPHONE_LIST_TXTS);
	/* convert resource */
	_cvt_txt_array(data->pt_bt_earphone_list_txt, data->style_txt_bt_earphone_list,
			&data->font, data->res_txt_bt_earphone_list, LV_TEXT_ALIGN_CENTER, NUM_BT_EARPHONE_LIST_TXTS);

	/* create bg image */
	data->bt_earphone_list_bg_img = lv_img_create(data->bmp[BMP_BG]);
	lv_obj_set_size(data->bt_earphone_list_bg_img, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(data->bt_earphone_list_bg_img, lv_color_make(0x3b, 0x3b, 0x3b), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(data->bt_earphone_list_bg_img, LV_OPA_COVER, LV_PART_MAIN);

	lv_obj_add_event_cb(data->bt_earphone_list_bg_img, _bmp_event_handler, LV_EVENT_GESTURE, data);
	/*set current background clickable to cut out click event send to front bg*/
	lv_obj_add_flag(data->bt_earphone_list_bg_img, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(data->bt_earphone_list_bg_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

	/* create button */
	_create_btn_array(data->bt_earphone_list_bg_img, data->bt_earphone_update_btn,
			data->pt_bt_earphone_update_btn, data->img_dsc_bt_earphone_update_btn,
			data->img_dsc_bt_earphone_update_btn, NUM_BT_EARPHONE_LIST_BTNS,
			_bt_earphone_btn_event_cb, data);

	/*create text*/
	_create_label_array(data->bt_earphone_list_bg_img, data->bt_earphone_name, data->pt_bt_earphone_list_txt,
			data->style_txt_bt_earphone_list, data->res_txt_bt_earphone_list, NUM_BT_EARPHONE_LIST_TXTS);

	lv_obj_add_event_cb(data->bt_earphone_name[TXT_BT_EARPHONE_NAME1], _bt_earphone_lbl_event_cb, LV_EVENT_ALL, data);
	lv_obj_add_event_cb(data->bt_earphone_name[TXT_BT_EARPHONE_NAME2], _bt_earphone_lbl_event_cb, LV_EVENT_ALL, data);
	lv_obj_add_flag(data->bt_earphone_name[TXT_BT_EARPHONE_NAME1], LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(data->bt_earphone_name[TXT_BT_EARPHONE_NAME2], LV_OBJ_FLAG_CLICKABLE);
	lv_label_set_text(data->bt_earphone_name[TXT_BT_EARPHONE_LIST_NAME], "蓝牙耳机");

	data->update_bt_earphone_list = 1;

	app_ui_set_scene_scrollable(false);
}

static void _exit_bt_earphone_list_view(music_ui_data_t *data)
{
	if (data->bt_earphone_list_bg_img == NULL)
		return;

	_delete_obj_array(data->bt_earphone_update_btn, NUM_BT_EARPHONE_LIST_BTNS);
	_delete_obj_array(data->bt_earphone_name, NUM_BT_EARPHONE_LIST_TXTS);

	lv_obj_del(data->bt_earphone_list_bg_img);
	data->bt_earphone_list_bg_img = NULL;

	lvgl_style_array_reset(data->style_txt_bt_earphone_list, NUM_BT_EARPHONE_LIST_TXTS);

	lvgl_res_unload_pictures(data->img_dsc_bt_earphone_update_btn, NUM_BT_EARPHONE_LIST_BTNS);
	lvgl_res_unload_strings(data->res_txt_bt_earphone_list, NUM_BT_EARPHONE_LIST_TXTS);

	lv_indev_wait_release(lv_indev_get_act());
	app_ui_set_scene_scrollable(true);

	os_mutex_lock(&music_view_mutex, OS_FOREVER);
	data->update_bt_earphone_list = 0;
	os_mutex_unlock(&music_view_mutex);
}

static void _bt_earphone_list_info_update(struct bt_device_info_t *bt_info, uint8_t count)
{
	music_ui_data_t *data = p_music_data;
	struct bt_device_info_t *info = data->bt_earphone_info;
	int i = 0, j = 0;
	int connected_idx = -1;

	if (!data)
		return;

	os_mutex_lock(&music_view_mutex, OS_FOREVER);

	data->update_bt_earphone_list = 1;

	/* clear disconnected device info */
	for (i = 0; i < MAX_SUPPORT_CNT; i++) {
		if (info[i].name == NULL)
			continue;

		if (!info[i].connected) {
			app_mem_free(info[i].name);
			memset(&info[i], 0, sizeof(info[i]));
		} else if (connected_idx < 0) {
			connected_idx = i;
		}
	}

	/* move connected device to the first line*/
	if (connected_idx >= 0 && connected_idx < MAX_SUPPORT_CNT) {
		memcpy(&info[0], &info[connected_idx], sizeof(info[0]));
		memset(&info[connected_idx], 0, sizeof(info[0]));
	}

	if (count == 0) {
		os_mutex_unlock(&music_view_mutex);
		return;
	}

	for (i = 0, j = 0; i < MAX_SUPPORT_CNT; i++) {
		if (info[i].name)
			continue;

		/* avoid redisplay connected device*/
		if (info[0].connected) {
			if (info[0].addr.val[0] == bt_info[j].addr.val[0] &&
				info[0].addr.val[1] == bt_info[j].addr.val[1] &&
				info[0].addr.val[2] == bt_info[j].addr.val[2] &&
				info[0].addr.val[3] == bt_info[j].addr.val[3] &&
				info[0].addr.val[4] == bt_info[j].addr.val[4] &&
				info[0].addr.val[5] == bt_info[j].addr.val[5]) {

				if (++j >= count) break;
				else continue;
			}
		}

		info[i].name = app_mem_malloc(strlen(bt_info[j].name) + 1);
		if (!info[i].name) {
			SYS_LOG_ERR("malloc failed\n");
			os_mutex_unlock(&music_view_mutex);
			return;
		}

		strcpy(info[i].name, bt_info[j].name);
		memcpy(&info[i].addr, &bt_info[j].addr, sizeof(bd_address_t));
		info[i].connected = bt_info[j].connected;
		info[i].paired = bt_info[j].paired;

		if (++j >= count) break;
	}

	os_mutex_unlock(&music_view_mutex);
}

static void _display_bt_earphone_list_info(music_ui_data_t *data)
{
	if (!data || !data->bt_earphone_name[TXT_BT_EARPHONE_NAME1] ||
		!data->bt_earphone_name[TXT_BT_EARPHONE_NAME2] ||
		!data->bt_earphone_name[TXT_BT_EARPHONE_NAME1_CNT_STATE] ||
		!data->bt_earphone_name[TXT_BT_EARPHONE_NAME2_CNT_STATE])
		return;

	struct bt_device_info_t *info = data->bt_earphone_info;

	lv_obj_t *name_objs[] = {
		data->bt_earphone_name[TXT_BT_EARPHONE_NAME1],
		data->bt_earphone_name[TXT_BT_EARPHONE_NAME2],
	};

	lv_obj_t *state_objs[] = {
		data->bt_earphone_name[TXT_BT_EARPHONE_NAME1_CNT_STATE],
		data->bt_earphone_name[TXT_BT_EARPHONE_NAME2_CNT_STATE],
	};

	for (int i = 0; i < 2; i++) {
		if (info[i].name == NULL) {
			lv_label_set_text(name_objs[i], "");
			lv_label_set_text(state_objs[i], "");
			continue;
		}

		lv_label_set_text(name_objs[i], info[i].name);

		if (info[i].connected) {
			lv_label_set_text(state_objs[i], "已连接");
		} else if (info[i].paired) {
			lv_label_set_text(state_objs[i], "已配对");
		} else {
			lv_label_set_text(state_objs[i], "");
		}

		SYS_LOG_INF("[%d] name %s, connected %d, paired %d\n",
			i, info[i].name, info[i].connected, info[i].paired);
	}
}

static void _bt_earphone_info_update_check(music_ui_data_t *data)
{
	struct bt_device_info_t *info = data->bt_earphone_info;
	bool need_update = false;

	for (int i = 0; i < MAX_SUPPORT_CNT; i++) {
		bool connected, paired;

		if (info[i].name == NULL)
			continue;

		connected = bt_transmit_check_dev_connected(info[i].addr.val) ? 1 : 0;
		paired = bt_manager_get_addr_linkkey(&info[i].addr, NULL) ? 0 : 1;

		if (connected != info[i].connected || paired != info[i].paired) {
			SYS_LOG_INF("name %s, connected %d, paired %d\n", info[i].name,
				info[i].connected, info[i].paired);
			info[i].connected = connected;
			info[i].paired = paired;
			need_update = true;
		}
	}

	if (need_update)
		_display_bt_earphone_list_info(data);
}

#endif /* CONFIG_BT_TRANSMIT */

static int _load_resource(music_ui_data_t *data)
{
	int ret;

	/* load scene */
	ret = lvgl_res_load_scene(SCENE_LCMUSIC_VIEW, &data->res_scene, DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("SCENE_LCMUSIC_VIEW not found");
		return -ENOENT;
	}

	/* open font */
	if (LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_NORMAL) < 0) {
		SYS_LOG_ERR("font not found");
		return -ENOENT;
	}

#ifdef CONFIG_BITMAP_FONT_SUPPORT_EMOJI
	LVGL_FONT_SET_EMOJI(&data->font, DEF_FONT28_EMOJI);
	LVGL_FONT_SET_DEFAULT_CODE(&data->font, 0x53E3, 0x1ffff);
#endif

	/* load resource */
	lvgl_res_load_pictures_from_scene(&data->res_scene, _bmp_ids, data->img_dsc_bmp, data->pt_bmp, NUM_IMGS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, _btn_def_ids, data->img_dsc_def, data->pt_def, NUM_BTNS);
	lvgl_res_load_pictures_from_scene(&data->res_scene, _btn_sel_ids, data->img_dsc_sel, data->pt_sel, NUM_BTNS);
	lvgl_res_load_strings_from_scene(&data->res_scene, _txt_ids, data->res_txt, NUM_TXTS);

	/* convert resource */
	_cvt_txt_array(data->pt_txt, data->style_txt, &data->font, data->res_txt, LV_TEXT_ALIGN_CENTER, NUM_TXTS);

	return 0;
}

static void _unload_resource(music_ui_data_t *data)
{
	LVGL_FONT_CLOSE(&data->font);

	lvgl_res_unload_pictures(data->img_dsc_def, NUM_BTNS);
	lvgl_res_unload_pictures(data->img_dsc_sel, NUM_BTNS);
	lvgl_res_unload_pictures(data->img_dsc_bmp, NUM_IMGS);

	lvgl_res_unload_strings(data->res_txt, NUM_TXTS);
	lvgl_res_unload_scene(&data->res_scene);
}

static void music_progress_slider_event_cb(lv_event_t * e)
{
	//SYS_LOG_INF("music_progress_slider_event_cb  e->code:%d", e->code);
	music_ui_data_t *data = lv_event_get_user_data(e);
	
	if (data->presenter->get_play_type() != LCMUSIC_PLAYER)
		return;

	if(e->code == LV_EVENT_PRESSED) {
		data->slider_setting = true;
	}
	else if(e->code == LV_EVENT_RELEASED) {
		//get progress value
		int value = lv_slider_get_value(data->progress_slider);
		data->presenter->seek(value);
		data->slider_setting = false;
	}
}

static int _music_create(lv_obj_t * scr, const music_presenter_t * presenter)
{
	launcher_app_t *app = launcher_app_get();

	music_ui_data_t *data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;

	data->id3_info = lv_mem_alloc(sizeof(*data->id3_info));
	if (data->id3_info == NULL) {
		app_mem_free(data);
		return -ENOMEM;
	}

	if (_load_resource(data)) {
		lv_mem_free(data->id3_info);
		app_mem_free(data);
		return -ENOENT;
	}

	lv_obj_set_user_data(scr, data);

	p_music_data = data;
	/* create image */
	//_create_img_array(scr, data->bmp, data->pt_bmp, data->img_dsc_bmp, NUM_IMGS);
	data->bmp[BMP_BG] = lv_obj_create(scr);
	lv_obj_set_pos(data->bmp[BMP_BG], data->pt_bmp[BMP_BG].x, data->pt_bmp[BMP_BG].y);
	lv_obj_set_size(data->bmp[BMP_BG], data->img_dsc_bmp[BMP_BG].header.w, data->img_dsc_bmp[BMP_BG].header.h);
	lv_obj_set_style_bg_color(data->bmp[BMP_BG], lv_color_black(), 0);
	lv_obj_set_style_bg_opa(data->bmp[BMP_BG], LV_OPA_COVER, 0);
	lv_obj_center(data->bmp[BMP_BG]);

	/* create play progress slider*/
	data->progress_slider = lv_slider_create(data->bmp[BMP_BG]);
	lv_obj_set_pos(data->progress_slider, 58, 305);
	lv_obj_set_size(data->progress_slider, 350, 10);
	lv_obj_set_ext_click_area(data->progress_slider, LV_DPX(32));
	lv_obj_set_style_bg_color(data->progress_slider, lv_color_make(0xC0, 0xC0, 0xC0), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(data->progress_slider, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_radius(data->progress_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(data->progress_slider, lv_color_make(255, 0, 0), LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(data->progress_slider, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(data->progress_slider, lv_color_make(0, 0, 255), LV_PART_KNOB);
	lv_obj_set_style_bg_opa(data->progress_slider, LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_radius(data->progress_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
	lv_obj_set_style_pad_all(data->progress_slider, 10, LV_PART_KNOB);
	lv_obj_add_event_cb(data->progress_slider, music_progress_slider_event_cb, LV_EVENT_ALL, data);

	/* create button */
	_create_btn_array(data->bmp[BMP_BG], data->btn, data->pt_def, data->img_dsc_def, data->img_dsc_sel,
			NUM_BTNS, _btn_event_handler, data);

	/* FIXME: donot update the play/pause icon when pressed */
	lv_imgbtn_set_src(data->btn[BTN_PLAY], LV_IMGBTN_STATE_RELEASED, NULL, &data->img_dsc_def[BTN_PLAY], NULL);
	lv_imgbtn_set_src(data->btn[BTN_PLAY], LV_IMGBTN_STATE_PRESSED, NULL, &data->img_dsc_def[BTN_PLAY], NULL);
	lv_imgbtn_set_src(data->btn[BTN_PLAY], LV_IMGBTN_STATE_CHECKED_RELEASED, NULL, &data->img_dsc_sel[BTN_PLAY], NULL);
	lv_imgbtn_set_src(data->btn[BTN_PLAY], LV_IMGBTN_STATE_CHECKED_PRESSED, NULL, &data->img_dsc_sel[BTN_PLAY], NULL);

	lv_obj_add_flag(data->btn[BTN_PLAY], LV_OBJ_FLAG_CHECKABLE);
	lv_obj_add_flag(data->btn[BTN_SEQ_PLAY_M], LV_OBJ_FLAG_CHECKABLE);
	lv_obj_add_flag(data->btn[BTN_BMP_MUSIC_SOURCE], LV_OBJ_FLAG_CHECKABLE);
	lv_obj_clear_flag(data->btn[BTN_BMP_MUSIC_SOURCE], LV_OBJ_FLAG_CLICKABLE);

	/*set button ext click area to improve the recognition rate*/
	lv_obj_set_ext_click_area(data->btn[BTN_PRE], lv_obj_get_style_width(data->btn[BTN_PRE], LV_PART_MAIN));
	lv_obj_set_ext_click_area(data->btn[BTN_NEXT], lv_obj_get_style_width(data->btn[BTN_NEXT], LV_PART_MAIN));
	lv_obj_set_ext_click_area(data->btn[BTN_SEQ_PLAY_M], lv_obj_get_style_width(data->btn[BTN_SEQ_PLAY_M], LV_PART_MAIN));
#if DISPLAY_MORE_VIEW
	lv_obj_set_ext_click_area(data->btn[BTN_MORE], lv_obj_get_style_width(data->btn[BTN_MORE], LV_PART_MAIN));
#endif
	lv_obj_set_ext_click_area(data->btn[BTN_VOL], lv_obj_get_style_width(data->btn[BTN_VOL], LV_PART_MAIN) / 2);

	/* create label */
	_create_label_array(data->bmp[BMP_BG], data->lbl, data->pt_txt, data->style_txt, data->res_txt, NUM_TXTS);

	music_ui_presenter_init();

	/* init text */
#ifdef CONFIG_LCMUSIC_APP
	if (data->presenter->get_play_type() == LCMUSIC_PLAYER)
		lcmusic_display_song_info(NULL, true);

	if (presenter->get_play_mode()) {
		btn_state_toggle(data->btn[BTN_SEQ_PLAY_M]);
		lv_obj_invalidate(data->btn[BTN_SEQ_PLAY_M]);
	}
#endif

	if (data->presenter->get_play_type() == BTMUSIC_PLAYER) {
		app->cur_player = BTMUSIC_PLAYER;
	#ifdef CONFIG_LCMUSIC_APP
		_music_play_view(data, BTMUSIC_PLAYER, NUM_PLAYERS);
	#endif
	}

	data->timer = lv_timer_create(_music_view_update_process_task_cb, MUSIC_INFO_UPDATE_PERIOD, data);
	if (data->timer)
		lv_timer_ready(data->timer);

	return 0;
}

static int _music_view_delete(lv_obj_t * scr)
{
	music_ui_data_t *data = lv_obj_get_user_data(scr);

	if (data) {
		music_ui_presenter_deinit();

		_exit_vol_view(data);

#if DISPLAY_MORE_VIEW
		_exit_more_view(data);
#endif

#ifdef CONFIG_BT_TRANSMIT
		_exit_bt_earphone_list_view(data);

		/* free bt transmit device info */
		os_mutex_lock(&music_view_mutex, OS_FOREVER);
		for (int i = 0; i < MAX_SUPPORT_CNT; i++) {
			if (data->bt_earphone_info[i].name)
				app_mem_free(data->bt_earphone_info[i].name);
		}
		os_mutex_unlock(&music_view_mutex);
#endif

		if (data->timer)
			lv_timer_del(data->timer);

		_delete_obj_array(data->btn, NUM_BTNS);
		_delete_obj_array(data->lbl, NUM_TXTS);
		_delete_obj_array(data->bmp, NUM_IMGS);
		lvgl_style_array_reset(data->style_txt, NUM_TXTS);

		_unload_resource(data);

		os_mutex_lock(&music_view_mutex, OS_FOREVER);

		lv_mem_free(data->id3_info);
		app_mem_free(data);

		p_music_data = NULL;
		os_mutex_unlock(&music_view_mutex);
	}

	return 0;
}

static int _music_view_focus_changed(lv_obj_t * scr, bool focused)
{
	music_ui_data_t *data = lv_obj_get_user_data(scr);

	if (data->timer) {
		if (focused) {
			lv_timer_resume(data->timer);
		} else {
			lv_timer_pause(data->timer);
		}
	}

	data->ignore_clicked = !focused;
	return 0;
}

int music_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _music_create(scr, msg_data);
	case MSG_VIEW_FOCUS:
		return _music_view_focus_changed(scr, true);
	case MSG_VIEW_DEFOCUS:
		return _music_view_focus_changed(scr, false);
	case MSG_VIEW_DELETE:
		return _music_view_delete(scr);
	default:
		return 0;
	}
}
