/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file app_ui_table.c
 */

/*********************
 *      INCLUDES
 *********************/
#include <os_common_api.h>
#include <app_ui.h>
#include "../applist/applist_ui.h"
#include "../applist/compass_ui.h"
#include "../alipay/alipay_ui.h"
#include "../clock_selector/clock_selector_ui.h"
#include "../desktop/desktop_ui.h"
#include "../gps/gps_ui.h"
#include "../health/health_ui.h"
#include "../vibration/vibration_ui.h"
#ifndef CONFIG_SIMULATOR
#include "../charger/charger.h"
#endif

/**********************
 *   GLOBAL PROTOTYPES
 **********************/
extern int alarm_set_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int card_reader_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int ota_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int power_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int stopwatch_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int welcome_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int goodbye_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int setting_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int view_set_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
extern int anim_set_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

static const app_ui_scene_info_t s_main_scenes[] = {
	{ .id = MUSIC_SCENE,       .proc = music_main_ui_proc,   .presenter = &g_music_ui_presenter,    .fps = DEF_UI_FPS },
	{ .id = CLOCK_SCENE,       .proc = clock_main_ui_proc,   .presenter = &g_desktop_ui_presenter,  .fps = DEF_UI_FPS },
	{ .id = ALIPAY_MAIN_SCENE, .proc = alipay_main_ui_proc,  .presenter = &g_alipay_view_presenter, .fps = DEF_UI_FPS },
	{ .id = HEART_SCENE,       .proc = heart_main_ui_proc,   .presenter = &g_desktop_ui_presenter,  .fps = DEF_UI_FPS },
	{ .id = WXPAY_MAIN_SCENE,  .proc = wxpay_main_ui_proc,   .presenter = &g_wxpay_view_presenter,  .fps = DEF_UI_FPS },
	{ .id = SPORT_SCENE,       .proc = sport_main_ui_proc,   .presenter = &g_desktop_ui_presenter,  .fps = DEF_UI_FPS },
	{ .id = CTRL_SCENE,        .proc = ctrl_main_ui_proc,    .presenter = &g_desktop_ui_presenter,  .fps = DEF_UI_FPS },
	{ .id = MESSAGE_SCENE,     .proc = message_main_ui_proc, .presenter = &g_desktop_ui_presenter,  .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_health_scenes[] = {
	{ .id = BP_SCENE,   .proc = health_bp_ui_proc,   .presenter = &g_bp_ui_presenter,   .fps = DEF_UI_FPS },
	{ .id = SPO2_SCENE, .proc = health_spo2_ui_proc, .presenter = &g_spo2_ui_presenter, .fps = DEF_UI_FPS },
};

#ifdef CONFIG_ALARM_MANAGER
static const app_ui_scene_info_t s_alarmset_scenes[] = {
	{ .id = ALARMSET_SCENE, .proc = alarm_set_ui_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};
#endif

#ifdef SCENE_AOD_CLOCK_VIEW
static const app_ui_scene_info_t s_aodclock_scenes[] = {
	{ .id = AODCLOCK_SCENE, .proc = aod_clock_ui_proc, .presenter = &g_desktop_ui_presenter, .fps = DEF_UI_FPS },
};
#endif

static const app_ui_scene_info_t s_applist_scenes[] = {
	{ .id = APPLIST_SCENE, .proc = applist_ui_proc, .presenter = &g_applist_ui_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_setting_scenes[] = {
	{ .id = SETTING_UI, .proc = setting_ui_proc, .presenter = &g_applist_ui_presenter, .fps = DEF_UI_FPS },
};
static const app_ui_scene_info_t s_viewset_scenes[] = {
	{ .id = VIEW_SET_UI, .proc = view_set_ui_proc, .presenter = &g_applist_ui_presenter, .fps = DEF_UI_FPS },
};
static const app_ui_scene_info_t s_animset_scenes[] = {
	{ .id = ANIM_SET_UI, .proc = anim_set_ui_proc, .presenter = &g_applist_ui_presenter, .fps = DEF_UI_FPS },
};
static const app_ui_scene_info_t s_clocksel_scenes[] = {
	{ .id = CLOCKSEL_SCENE, .proc = clock_selector_ui_proc, .presenter = &g_clocksel_ui_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_compass_scenes[] = {
	{ .id = COMPASS_SCENE, .proc = compass_ui_proc, .presenter = &g_compass_ui_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_power_scenes[] = {
	{ .id = POWER_SCENE, .proc = power_ui_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_stopwatch_scenes[] = {
	{ .id = STOPWATCH_SCENE, .proc = stopwatch_ui_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_alipay_bind_scenes[] = {
	{ .id = ALIPAY_BIND_SCENE, .proc = alipay_bind_ui_proc, .presenter = &g_alipay_view_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_alipay_pay_scenes[] = {
	{ .id = ALIPAY_PAY_SCENE,    .proc = alipay_pay_ui_proc,    .presenter = &g_alipay_view_presenter, .fps = DEF_UI_FPS },
	{ .id = ALIPAY_UNBIND_SCENE, .proc = alipay_unbind_ui_proc, .presenter = &g_alipay_view_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_wxpay_bind_scenes[] = {
	{ .id = WXPAY_BIND_SCENE, .proc = wxpay_bind_ui_proc, .presenter = &g_wxpay_view_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_wxpay_pay_scenes[] = {
	{ .id = WXPAY_PAY_SCENE,    .proc = wxpay_pay_ui_proc,    .presenter = &g_wxpay_view_presenter, .fps = DEF_UI_FPS },
	{ .id = WXPAY_UNBIND_SCENE, .proc = wxpay_unbind_ui_proc, .presenter = &g_wxpay_view_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_gps_scenes[] = {
	{ .id = GPS_SCENE, .proc = gps_ui_proc, .presenter = &gps_view_presenter, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_welcome_scenes[] = {
	{ .id = WELCOME_UI, .proc = welcome_ui_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_goodbye_scenes[] = {
	{ .id = GOODBYE_UI, .proc = goodbye_ui_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};

static const app_ui_scene_info_t s_vib_set_scenes[] = {
	{ .id = VIB_UI, .proc = vibration_ui_proc, .presenter = &vibration_view_presenter, .fps = DEF_UI_FPS },
};

#ifdef CONFIG_OTA_FOREGROUND
static const app_ui_scene_info_t s_ota_scenes[] = {
	{ .id = OTA_UI, .proc = ota_ui_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};
#endif

#ifdef CONFIG_CARD_READER_APP
static const app_ui_scene_info_t s_card_reader_scenes[] = {
	{ .id = CARD_READER_UI, .proc = card_reader_ui_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};
#endif

#ifdef CONFIG_CHARGER_APP
static const app_ui_scene_info_t s_charger_scenes[] = {
	{ .id = CHARGER_UI, .proc = _charger_view_proc, .presenter = NULL, .fps = DEF_UI_FPS },
};
#endif

static const app_ui_info_t s_ui_table[] = {
	[MAIN_UI] = {
		.id = MAIN_UI,
		.vertical = 0,
		.main_scrl_rotated = 1,
		.cross_scrl_overlapped = 1,
		.back_gesture_disabled = 1,
		.n_scenes = { 1, 4, 1, 1 },
		.scenes = s_main_scenes,
	},

	[HEALTH_UI] = {
		.id = HEALTH_UI,
		.vertical = 1,
		.n_scenes = { 0, 1, 0, 0 },
		.scenes = s_health_scenes,
	},

#ifdef CONFIG_ALARM_MANAGER
	[ALARMSET_UI] = {
		.id = ALARMSET_UI,
		.scenes = s_alarmset_scenes,
	},
#endif

#ifdef SCENE_AOD_CLOCK_VIEW
	[AODCLOCK_UI] = {
		.id = AODCLOCK_UI,
		.back_gesture_disabled = 1,
		.scenes = s_aodclock_scenes,
	},
#endif

	[APPLIST_UI] = {
		.id = APPLIST_UI,
		.scenes = s_applist_scenes,
	},

	[SETTING_UI] = {
		.id = SETTING_UI,
		.scenes = s_setting_scenes,
	},

	[VIEW_SET_UI] = {
		.id = SETTING_UI,
		.scenes = s_viewset_scenes,
	},

	[ANIM_SET_UI] = {
		.id = SETTING_UI,
		.scenes = s_animset_scenes,
	},

	[CLOCKSEL_UI] = {
		.id = CLOCKSEL_UI,
		.back_gesture_disabled = 1,
		.scenes = s_clocksel_scenes,
	},

	[COMPASS_UI] = {
		.id = COMPASS_UI,
		.scenes = s_compass_scenes,
	},

	[POWER_UI] = {
		.id = POWER_UI,
		.scenes = s_power_scenes,
	},

	[STOPWATCH_UI] = {
		.id = STOPWATCH_UI,
		.scenes = s_stopwatch_scenes,
	},

	[ALIPAY_BIND_UI] = {
		.id = ALIPAY_BIND_UI,
		.scenes = s_alipay_bind_scenes,
	},

	[ALIPAY_PAY_UI] = {
		.id = ALIPAY_PAY_UI,
		.vertical = 1,
		.n_scenes = { 0, 1, 0, 0 },
		.scenes = s_alipay_pay_scenes,
	},

	[WXPAY_BIND_UI] = {
		.id = WXPAY_BIND_UI,
		.scenes = s_wxpay_bind_scenes,
	},

	[WXPAY_PAY_UI] = {
		.id = WXPAY_PAY_UI,
		.vertical = 1,
		.n_scenes = { 0, 1, 0, 0 },
		.scenes = s_wxpay_pay_scenes,
	},

	[GPS_UI] = {
		.id = GPS_UI,
		.scenes = s_gps_scenes,
	},

	[WELCOME_UI] = {
		.id = WELCOME_UI,
		.scenes = s_welcome_scenes,
	},

	[GOODBYE_UI] = {
		.id = GOODBYE_UI,
		.scenes = s_goodbye_scenes,
	},

	[VIB_UI] = {
		.id = VIB_UI,
		.scenes = s_vib_set_scenes,
	},
#ifdef CONFIG_OTA_FOREGROUND
	[OTA_UI] = {
		.id = OTA_UI,
		.back_gesture_disabled = 1,
		.scenes = s_ota_scenes,
	},
#endif

#ifdef CONFIG_CARD_READER_APP
	[CARD_READER_UI] = {
		.id = CARD_READER_UI,
		.scenes = s_card_reader_scenes,
	},
#endif

#ifdef CONFIG_CHARGER_APP
	[CHARGER_UI] = {
		.id = CHARGER_UI,
		.scenes = s_charger_scenes,
	},
#endif
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const app_ui_info_t * app_ui_get_info(uint16_t id)
{
	if (id >= ARRAY_SIZE(s_ui_table))
		return NULL;

	if (s_ui_table[id].scenes == NULL)
		return NULL;

	return &s_ui_table[id];
}

const app_ui_scene_info_t * app_ui_get_scene_info(const app_ui_info_t * info, uint16_t scene_id)
{
	if (info) {
		int idx = app_ui_get_scene_count(info) - 1;

		for (; idx >= 0; idx--) {
			if (info->scenes[idx].id == scene_id)
				return &info->scenes[idx];
		}
	}

	return NULL;
}

uint16_t app_ui_get_scene_count(const app_ui_info_t *info)
{
	return info ? (info->n_scenes[0] + info->n_scenes[1] +
	               info->n_scenes[2] + info->n_scenes[3] + 1) : 0;
}

uint8_t app_ui_get_scroll_fps(const app_ui_info_t *info, bool vertical)
{
	if (info) {
		uint8_t fps = UINT8_MAX;
		int idx = app_ui_get_scene_count(info) - 1;

		for (; idx >= 0; idx--) {
			uint8_t scene_fps = app_ui_get_scene_fps(&info->scenes[idx]);
			if (fps > scene_fps)
				fps = scene_fps;
		}

		return fps;
	}

	return DEF_UI_FPS;
}

uint8_t app_ui_get_transition_fps(const app_ui_info_t * to, const app_ui_info_t * from, uint8_t anim_type)
{
	uint8_t to_fps = app_ui_get_scroll_fps(to, false);
	uint8_t from_fps = app_ui_get_scroll_fps(from, false);

	return LV_MIN(to_fps, from_fps);
}
