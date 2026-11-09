/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*********************
 *      INCLUDES
 *********************/
#include <os_common_api.h>
#ifndef CONFIG_SIMULATOR
#  include <soc.h>
#endif
#ifdef CONFIG_PROPERTY
#  include <property_manager.h>
#endif
#include "applist_ui.h"
#include "../alipay/alipay_ui.h"
#include "../gps/gps_ui.h"
#include "../clock_selector/clock_selector.h"
#include "launcher_app.h"
#include "../vibration/vibration_ui.h"

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
	uint8_t view_mode;
	uint8_t phone_en : 1;
	uint8_t vibrator_en : 1;
	int16_t scrl_x;
	int16_t scrl_y;
	uint8_t scroll_mode;
} applist_presenter_data_t;

/**********************
 *   STATIC PROTOTYPES
 **********************/
static uint8_t _applist_get_view_mode(void);
static void _applist_set_view_mode(uint8_t mode);
static void _applist_load_scroll_value(int16_t *scrl_x, int16_t *scrl_y);
static void _applist_save_scroll_value(int16_t scrl_x, int16_t scrl_y);
static bool _applist_view_phone_is_on(void);
static void _applist_view_toggle_phone(void);
static bool _applist_view_vibrator_is_on(void);
static void _applist_view_toggle_vibrator(void);
static bool _applist_view_aod_mode_is_on(void);
static void _applist_view_toggle_aod_mode(void);
static void _applist_view_open_stopwatch(void);
static void _applist_view_open_alarm(void);
static void _applist_view_open_compass(void);
static void _applist_view_open_longview(void);
static void _applist_view_open_alipay(void);
static void _applist_view_open_wxpay(void);
static void _applist_view_open_gps(void);
static void _applist_view_open_vib_set(void);
static void _applist_view_open_setting(void);
static void _applist_set_scroll_anim_mode(int mode);
static int _applist_get_scroll_anim_mode(void);
/**********************
 *  GLOBAL VARIABLES
 **********************/
const applist_ui_presenter_t g_applist_ui_presenter = {
	.get_view_mode = _applist_get_view_mode,
	.set_view_mode = _applist_set_view_mode,
	.load_scroll_value = _applist_load_scroll_value,
	.save_scroll_value = _applist_save_scroll_value,
	.phone_is_on = _applist_view_phone_is_on,
	.toggle_phone = _applist_view_toggle_phone,
	.vibrator_is_on = _applist_view_vibrator_is_on,
	.toggle_vibrator = _applist_view_toggle_vibrator,
	.aod_mode_is_on = _applist_view_aod_mode_is_on,
	.toggle_aod_mode = _applist_view_toggle_aod_mode,
	.open_stopwatch = _applist_view_open_stopwatch,
	.open_alarmclock = _applist_view_open_alarm,
	.open_compass = _applist_view_open_compass,
	.open_longview = _applist_view_open_longview,
	.open_alipay = _applist_view_open_alipay,
	.open_wxpay = _applist_view_open_wxpay,
	.open_gps = _applist_view_open_gps,
	.open_vib_set = _applist_view_open_vib_set,
	.open_setting = _applist_view_open_setting,
	.set_scroll_anim_mode = _applist_set_scroll_anim_mode,
	.get_scroll_anim_mode = _applist_get_scroll_anim_mode,
};

/**********************
 *  STATIC VARIABLES
 **********************/
static applist_presenter_data_t s_applist_model = {
	.view_mode = APPLIST_MODE_LIST,
	.scrl_x = INT16_MAX,
	.scrl_y = INT16_MAX,
};

/**********************
 *   STATIC FUNCTIONS
 **********************/
static uint8_t _applist_get_view_mode(void)
{
#ifdef CONFIG_PROPERTY
	s_applist_model.view_mode = property_get_int(CFG_APPLIST_MODE, APPLIST_MODE_LIST);
#endif
	return s_applist_model.view_mode;
}

static void _applist_set_view_mode(uint8_t mode)
{
	if (mode < NUM_APPLIST_MODES && mode != s_applist_model.view_mode) {
		s_applist_model.view_mode = mode;
		s_applist_model.scrl_x = INT16_MAX;
		s_applist_model.scrl_y = INT16_MAX;
#ifdef CONFIG_PROPERTY
		property_set_int(CFG_APPLIST_MODE, mode);
		property_flush(CFG_APPLIST_MODE);
#endif
	}
}

static void _applist_set_scroll_anim_mode(int mode)
{
	if(mode != s_applist_model.scroll_mode) {
	#ifdef CONFIG_PROPERTY
		property_set_int(CFG_SCROLL_MODE, mode);
		property_flush(CFG_SCROLL_MODE);
	#endif
	}
	s_applist_model.scroll_mode = mode;
}

static int _applist_get_scroll_anim_mode(void)
{
	return s_applist_model.scroll_mode;
}

static void _applist_load_scroll_value(int16_t *scrl_x, int16_t *scrl_y)
{
	if (scrl_x && s_applist_model.scrl_x != INT16_MAX)
		*scrl_x = s_applist_model.scrl_x;

	if (scrl_y && s_applist_model.scrl_y != INT16_MAX)
		*scrl_y = s_applist_model.scrl_y;
}

static void _applist_save_scroll_value(int16_t scrl_x, int16_t scrl_y)
{
	s_applist_model.scrl_x = scrl_x;
	s_applist_model.scrl_y = scrl_y;
}

static bool _applist_view_phone_is_on(void)
{
	return s_applist_model.phone_en;
}

static void _applist_view_toggle_phone(void)
{
	s_applist_model.phone_en = !s_applist_model.phone_en;
}

static bool _applist_view_vibrator_is_on(void)
{
	return s_applist_model.vibrator_en;
}

static void _applist_view_toggle_vibrator(void)
{
	s_applist_model.vibrator_en = !s_applist_model.vibrator_en;
}

static bool _applist_view_aod_mode_is_on(void)
{
#ifndef CONFIG_SIMULATOR
	return soc_get_aod_mode();
#else
	return 0;
#endif
}

static void _applist_view_toggle_aod_mode(void)
{
#ifndef CONFIG_SIMULATOR
	/* AOD mode supported */
	if (clocksel_get_aod_clock_dsc() != NULL) {
		soc_set_aod_mode(!soc_get_aod_mode());
#ifdef CONFIG_PROPERTY
		property_set_int(CFG_AOD_MODE, soc_get_aod_mode());
		property_flush(CFG_AOD_MODE);
#endif
	}
#endif /* CONFIG_SIMULATOR */
}

static void _applist_view_open_stopwatch(void)
{
	app_ui_stack_forward(STOPWATCH_UI, LV_SCR_LOAD_ANIM_ZOOM_IN);
}

static void _applist_view_open_alarm(void)
{
#ifdef CONFIG_ALARM_MANAGER
	app_ui_stack_forward(ALARMSET_UI, LV_SCR_LOAD_ANIM_ZOOM_IN);
#endif
}

static void _applist_view_open_compass(void)
{
	/* FIXME: COMPASS_UI requires rotation which consumes too much memory */
	//app_ui_stack_forward(COMPASS_UI, DEF_UI_LOAD_ANIM_TYPE);
}

static void _applist_view_open_longview(void)
{
}

static void _applist_view_open_alipay(void)
{
	alipay_ui_enter(LV_SCR_LOAD_ANIM_ZOOM_IN);
}

static void _applist_view_open_wxpay(void)
{
	wxpay_ui_enter(LV_SCR_LOAD_ANIM_ZOOM_IN);
}

static void _applist_view_open_gps(void)
{
	gps_ui_enter(LV_SCR_LOAD_ANIM_ZOOM_IN);
}

static void _applist_view_open_vib_set(void)
{
	vibration_ui_enter(LV_SCR_LOAD_ANIM_ZOOM_IN);
}

static void _applist_view_open_setting(void)
{
	app_ui_stack_forward(SETTING_UI, LV_SCR_LOAD_ANIM_ZOOM_IN);
}

