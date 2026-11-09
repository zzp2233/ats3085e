/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file launcher_app.c
 */
#include <assert.h>
#include <app_manager.h>
#include <srv_manager.h>
#include <mem_manager.h>
#include <msg_manager.h>
#include <power_manager.h>
#include <thread_timer.h>
#include <msgbox_cache.h>
#include "app_defines.h"
#include "system_app.h"
#include "launcher_app.h"
#ifdef CONFIG_ACTS_DVFS_DYNAMIC_LEVEL
#include "dvfs.h"
#endif
#ifdef CONFIG_UI_MANAGER
#  include "clock_selector/clock_selector.h"
#  include "desktop/desktop_ui.h"
#  include "app_ui.h"
#endif

#ifndef CONFIG_SIMULATOR
#  include <soc.h>
#  include <drivers/power_supply.h>
#endif

#ifdef CONFIG_PROPERTY
#  include <property_manager.h>
#endif
#ifdef CONFIG_BT_MANAGER
#  include <bt_manager.h>
#endif
#ifdef CONFIG_ALARM_MANAGER
#  include <alarm_manager.h>
#endif
#ifdef CONFIG_ALARM_APP
#  include <alarm/alarm.h>
#endif
#ifdef CONFIG_SYS_WAKELOCK
#  include <sys_wakelock.h>
#endif
#ifdef CONFIG_BT_PLAYER
#  include <bt_player/bt_player.h>
#endif

#ifdef CONFIG_LCMUSIC_APP
extern void lcmusic_init(const char *dir);
extern void lcmusic_exit(void);
extern void lcmusic_event_proc(struct app_msg *msg);
extern void lcmusic_input_event_proc(struct app_msg *msg);
#endif /* CONFIG_LCMUSIC_APP */

#ifdef CONFIG_BT_CALL_APP
extern void btcall_input_event_proc(struct app_msg *msg);
extern void btcall_bt_event_proc(struct app_msg *msg);
extern void btcall_tts_event_proc(struct app_msg *msg);
extern bool btcall_key_event_proc(uint32_t event);
extern void btcall_view_resume(void);
#endif/* CONFIG_BT_CALL_APP */

extern void system_create_low_power_view(void);
extern void system_delete_low_power_view(void);

#ifdef CONFIG_BT_TRANSMIT
extern void bt_transmit_bt_event_proc(struct app_msg *msg);
#endif /* CONFIG_BT_TRANSMIT */

static void _launcher_key_event_handle(uint16_t view_id, uint32_t event);

launcher_app_t g_launcher_app;

#ifdef CONFIG_UI_MANAGER
#ifdef CONFIG_RTC_ACTS
static void _launcher_app_update_clock_view(os_work *work);

static OS_WORK_DEFINE(clock_update_work, _launcher_app_update_clock_view);

static void _launcher_app_update_clock_view(os_work *work)
{
	app_ui_paint(CLOCK_SCENE, NULL);
}

static void _rtc_alarm_period_handler(const void *cb_data)
{
	os_work_submit(&clock_update_work);
}

void launcher_set_rtc_period(bool en, uint16_t period_ms)
{
	launcher_app_t *app = launcher_app_get();

	if (en) {
		app->rtc_config.tm_msec = period_ms % 1000;
		app->rtc_config.tm_sec = period_ms / 1000;
	}

	rtc_set_period_alarm(app->rtc_dev, (void *)&app->rtc_config, en);
}

#elif defined(CONFIG_THREAD_TIMER)
static void _rtc_timer_period_handler(struct thread_timer *ttimer, void *arg)
{
	launcher_app_t *app = launcher_app_get();

	if (++app->rtc_time.tm_sec >= 60) {
		app->rtc_time.tm_sec = 0;
		app->rtc_time.tm_min++;
	}

	if (app->rtc_time.tm_min >= 60) {
		app->rtc_time.tm_hour++;
	}

	if (app->rtc_time.tm_hour >= 24) {
		app->rtc_time.tm_hour = 0;
	}

	app_ui_paint(CLOCK_SCENE, NULL);
}

void launcher_set_rtc_period(bool en, uint16_t period_ms) { }
#endif
#endif /* CONFIG_UI_MANAGER */

#ifdef CONFIG_SENSOR_MANAGER
static int _sensor_res_init(void)
{
	launcher_app_t *app = launcher_app_get();
	sensor_res_t *res = &app->sensor_res;
	int ret;

	ret = sensor_manager_get_result(res);
	if (ret == 0) {
		app->heart_rate = (uint32_t)res->heart_rate;
		app->step_count = (uint32_t)res->pedometer.total_steps;
		SYS_LOG_INF("heart_rate: %d", app->heart_rate);
		SYS_LOG_INF("step_count: %d", app->step_count);
	}

	return 0;
}

static int _sensor_res_callback(int evt_id, sensor_res_t *res)
{
#ifdef CONFIG_UI_MANAGER
	launcher_app_t *app = launcher_app_get();

	if (app->heart_rate != (uint32_t)res->heart_rate) {
		app->heart_rate = (uint32_t)res->heart_rate;
		SYS_LOG_INF("heart_rate: %d", app->heart_rate);
		app_ui_paint(HEART_SCENE, NULL);
	}

	if (app->step_count != (uint32_t)res->pedometer.total_steps) {
		app->step_count = (uint32_t)res->pedometer.total_steps;
		SYS_LOG_INF("step_count: %d", app->step_count);
		app_ui_paint(CLOCK_SCENE, NULL);
	}

	if (app->bearing != (uint32_t)res->orientation) {
		app->bearing = (uint32_t)res->orientation;
		SYS_LOG_INF("bearing: %d", app->bearing);
		app_ui_paint(COMPASS_SCENE, NULL);
	}
#endif

	return 0;
}
#endif /* CONFIG_SENSOR_MANAGER */

static void _launcher_app_load_config(void)
{
#ifdef CONFIG_PROPERTY
#ifdef CONFIG_SYS_WAKELOCK
	if (property_get_int(CFG_SLEEP_NEVER, 0))
		sys_wake_lock(FULL_WAKE_LOCK);
#endif
#endif /* CONFIG_PROPERTY */
}

static int _launcher_app_init(void)
{
	launcher_app_t *app = launcher_app_get();

	memset(app, 0, sizeof(*app));

	_launcher_app_load_config();

#ifdef CONFIG_SENSOR_MANAGER
	_sensor_res_init();
	sensor_manager_add_callback(_sensor_res_callback);
#endif

#ifdef CONFIG_UI_MANAGER
#ifdef CONFIG_RTC_ACTS
	app->rtc_dev = device_get_binding(CONFIG_RTC_0_NAME);
	assert(app->rtc_dev != NULL);
	app->rtc_config.cb_fn = _rtc_alarm_period_handler;
	app->rtc_config.cb_data = NULL;
#elif defined(CONFIG_THREAD_TIMER)
	thread_timer_init(&app->rtc_timer, _rtc_timer_period_handler, app);
#endif /* CONFIG_RTC_ACTS*/

	clocksel_init_clocks();

	ui_manager_set_keyevent_callback(_launcher_key_event_handle);

	app_ui_stack_clean(false);
	app_ui_stack_forward(MAIN_UI, APP_UI_LOAD_ANIM_NONE);
#endif /* CONFIG_UI_MANAGER */

#ifdef CONFIG_ALARM_MANAGER
#ifdef CONFIG_ALARM_APP
	system_registry_alarm_callback(alarm_event_cb);
#endif
	alarm_manager_init();
	find_and_set_alarm();
#endif

#ifdef CONFIG_LCMUSIC_APP
	lcmusic_init(CONFIG_APP_FAT_DISK"/");
#endif

#ifdef CONFIG_UI_MANAGER
	app->cur_player = music_ui_presenter_get_default_player();
#else
	app->cur_player = BTMUSIC_PLAYER;
#endif

	return 0;
}

static void _music_player_exit(void)
{
#if defined(CONFIG_BT_PLAYER) || defined(CONFIG_BT_CALL_APP)
	struct app_msg msg = { 0 };
#endif

#ifdef CONFIG_LCMUSIC_APP
	lcmusic_exit();
#endif

	/*stop btmusic player*/
#ifdef CONFIG_BT_PLAYER
	msg.cmd = BT_A2DP_STREAM_SUSPEND_EVENT;
	bt_player_bt_event_proc(&msg);
#endif

#ifdef CONFIG_ALARM_APP
	alarm_exit(false);
#endif

#ifdef CONFIG_BT_CALL_APP
	msg.cmd = BT_HFP_CALL_STATE_EXIT;
	btcall_bt_event_proc(&msg);
#endif
}

static void _launcher_app_exit(void)
{
#if !defined(CONFIG_RTC_ACTS) && defined(CONFIG_THREAD_TIMER)
	launcher_app_t *app = launcher_app_get();
	thread_timer_stop(&app->rtc_timer);
#endif

#ifdef CONFIG_SENSOR_MANAGER
	sensor_manager_remove_callback(_sensor_res_callback);
#endif
	_music_player_exit();

	app_ui_stack_clean(false);

	app_manager_thread_exit(APP_ID_LAUNCHER);
}

static void _launcher_app_early_suspend(void)
{
	launcher_app_t *app = launcher_app_get();

	app->suspended = 1;

#ifdef CONFIG_UI_MANAGER
#ifdef CONFIG_BT_CALL_APP
	if (app->cur_player == BTCALL_PLAYER) {
		msgbox_cache_close(BTCALL_MSGBOX_ID, true);
	}
#endif
#endif

#ifndef CONFIG_SIMULATOR
	if (soc_get_aod_mode()) {
		SYS_LOG_INF("AOD enter\n");

		if (!aod_clock_ui_enter())
			app->in_aod_view = 1;
	}
#endif /* CONFIG_SIMULATOR */

	SYS_LOG_INF("launcher early-suspend\n");
}

static void _launcher_app_late_resume(void)
{
	launcher_app_t *app = launcher_app_get();

#ifndef CONFIG_SIMULATOR
	if (app->in_aod_view) {
		aod_clock_ui_exit();
		app->in_aod_view = 0;

		SYS_LOG_INF("AOD exit\n");
	}
#endif /* CONFIG_SIMULATOR */

	app->suspended = 0;

#ifdef CONFIG_UI_MANAGER
#ifdef CONFIG_BT_CALL_APP
	if (app->cur_player == BTCALL_PLAYER) {
		btcall_view_resume();
	}
#endif
#endif

#ifdef CONFIG_ALARM_APP
	if (app->alarm_en) {
		alarm_start_msg_send();
		app->alarm_en = 0;
	}
#endif

	SYS_LOG_INF("launcher late-resume\n");
}

static void _launcher_app_suspend(void)
{
	launcher_app_t *app = launcher_app_get();

#ifdef CONFIG_SYS_WAKELOCK
	if (sys_wakelocks_check(PARTIAL_WAKE_LOCK)
		|| sys_wakelocks_check(FULL_WAKE_LOCK)) {
		SYS_LOG_INF("wake locked\n");
		return;
	}
#endif

	app->suspended = 2;

	SYS_LOG_INF("launcher suspend\n");
}

static void _launcher_app_resume(void)
{
	launcher_app_t *app = launcher_app_get();

	app->suspended = 1;

	SYS_LOG_INF("launcher resume\n");
}

#ifdef CONFIG_BT_MANAGER

#ifdef CONFIG_BT_CALL_APP
static void btcall_start_exit_event(struct app_msg *msg)
{
	launcher_app_t *app = launcher_app_get();

	if (msg->cmd == BT_HFP_CALL_STATE_START) {
		if (app->cur_player == BTCALL_PLAYER) {
			SYS_LOG_INF("btcall is cur player\n");
		} else if (app->cur_player == ALARM_PLAYER) {
			SYS_LOG_INF("switch player alarm -> btcall\n");
		#ifdef CONFIG_ALARM_APP
			alarm_snooze(false);
		#endif
			app->cur_player = BTCALL_PLAYER;
			btcall_bt_event_proc(msg);
		} else if (app->cur_player == LCMUSIC_PLAYER || app->cur_player == BTMUSIC_PLAYER){
			SYS_LOG_INF("switch player %d -> btcall\n", app->cur_player);
			_music_player_exit();
			app->prev_player = app->cur_player;
			app->cur_player = BTCALL_PLAYER;
			btcall_bt_event_proc(msg);
		}
	} else if (msg->cmd == BT_HFP_CALL_STATE_EXIT) {
		btcall_bt_event_proc(msg);
	} else {
		btcall_bt_event_proc(msg);
	}
}
#endif

static void _launcher_bt_event_handle(struct app_msg *msg)
{
#if defined(CONFIG_BT_CALL_APP) || defined(CONFIG_BT_TRANSMIT)\
    || defined(CONFIG_BT_TRANSMIT)
	launcher_app_t *app = launcher_app_get();
#endif

	switch (msg->cmd) {
#ifdef CONFIG_BT_CALL_APP
	case BT_HFP_CALL_STATE_START:
	case BT_HFP_CALL_STATE_EXIT:
		btcall_start_exit_event(msg);
		return;
#endif

#ifdef CONFIG_BT_PLAYER
	case BT_A2DP_STREAM_SUSPEND_EVENT:
		bt_player_bt_event_proc(msg);
		return;
#endif

#ifdef CONFIG_BT_TRANSMIT
	case BT_TRS_A2DP_STREAM_CLOSE_EVENT:
		bt_transmit_bt_event_proc(msg);
		return;
#endif

	default:
		break;
	}

#ifdef CONFIG_BT_CALL_APP
	if (app->cur_player == BTCALL_PLAYER) {
		btcall_start_exit_event(msg);
		return;
	}
#endif

#ifdef CONFIG_BT_PLAYER
	if (app->cur_player == BTMUSIC_PLAYER) {
		bt_player_bt_event_proc(msg);
		return;
	}
#endif

#ifdef CONFIG_BT_TRANSMIT
	if (app->cur_player == LCMUSIC_PLAYER) {
		bt_transmit_bt_event_proc(msg);
		return;
	}
#endif
}

static void _launcher_bt_mgr_event_handle(struct app_msg *msg)
{
#ifdef CONFIG_ALARM_MANAGER
	int ret;
#endif

	switch (msg->cmd) {
#ifdef CONFIG_ALARM_MANAGER
	case BT_MAP_SET_TIME_EVENT:
		ret = alarm_manager_set_time((struct rtc_time *)msg->ptr);
		if (ret) {
			SYS_LOG_ERR("set time error ret=%d\n", ret);
		} else {
			find_and_set_alarm();
		}
#ifdef CONFIG_UI_MANAGER
		//ui_view_paint2(MAIN_VIEW, 0, NULL);
#endif
		break;
#endif

	default:
		SYS_LOG_ERR("error: 0x%x!\n", msg->cmd);
		break;
	}
}
#endif /* CONFIG_BT_MANAGER */

#ifdef CONFIG_ALARM_APP
static void _launcher_alarm_event_handle(struct app_msg *msg)
{
	launcher_app_t *app = launcher_app_get();

	if (app->cur_player == BTCALL_PLAYER) {
		SYS_LOG_INF("btcall going");
		alarm_snooze(false);
		return;
	}

	if (msg->cmd == MSG_ALARM_PROC_START) {
		if (app->cur_player != ALARM_PLAYER) {
			_music_player_exit();
			app->prev_player = app->cur_player;
			app->cur_player = ALARM_PLAYER;
			SYS_LOG_INF("switch player %d to alarm\n", app->prev_player);
		}

		if (app->suspended != 2) {
			alarm_event_proc(msg);
		} else {
		#ifdef CONFIG_SYS_WAKELOCK
			sys_wake_lock(FULL_WAKE_LOCK);
			sys_wake_unlock(FULL_WAKE_LOCK);
		#endif
			app->alarm_en = 1;
		}
	} else if (app->cur_player == ALARM_PLAYER) {
		alarm_event_proc(msg);
	}
}
#endif /* CONFIG_ALARM_APP */

static void _launcher_key_event_handle(uint16_t view_id, uint32_t event)
{
	launcher_app_t *app = launcher_app_get();

	SYS_LOG_INF("view %u, key 0x%x", view_id, event);

	if (app->suspended) /* bypass key event until launcher late-resume */
		return;

	os_strace_u32(SYS_TRACE_ID_KEY_READ, event);

#ifdef CONFIG_BT_CALL_APP
	/* if (app->cur_player == BTCALL_PLAYER) */ {
		if (btcall_key_event_proc(event) == true)
			return;
	}
#endif

#ifdef CONFIG_ALARM_APP
	if (app->cur_player == ALARM_PLAYER) {
		if (alarm_key_event_proc(event) == true)
			return;
	}
#endif /* CONFIG_ALARM_APP */

#ifdef CONFIG_UI_MANAGER
	if (msgbox_cache_num_popup_get() > 0) {
		return;
	}

	uint16_t cur_scene = APP_SCENE_INVALID;
	uint16_t cur_ui = app_ui_stack_current(&cur_scene);

	if (event == (KEY_POWER | KEY_TYPE_LONG_DOWN)) {
		if (cur_ui != POWER_UI) {
			app_ui_stack_forward(POWER_UI, APP_UI_LOAD_ANIM_NONE);
		}
		return;
	}

	if (event == (KEY_POWER | KEY_TYPE_SHORT_UP)) {
		if (cur_ui == MAIN_UI) {
			if (cur_scene == CLOCK_SCENE) {
				app_ui_stack_forward(APPLIST_UI, DEF_UI_LOAD_ANIM_TYPE);
			} else {
				app_ui_focus_scene(CLOCK_SCENE);
			}
			return;
		}

		if (cur_ui == APPLIST_UI) {
			app_ui_stack_forward(HEALTH_UI, DEF_UI_LOAD_ANIM_TYPE);
			return;
		}

		/* go back to MAIN_UI */
		app_ui_stack_back_home((cur_ui == CLOCKSEL_UI || cur_ui == POWER_UI) ?
				APP_UI_LOAD_ANIM_NONE : DEF_UI_LOAD_ANIM_TYPE);
		return;
	}

	if (KEY_VALUE(event) == KEY_GESTURE_RIGHT) {
		uint8_t anim_type = DEF_UI_LOAD_ANIM_TYPE;
		if (cur_ui == POWER_UI)
			anim_type = APP_UI_LOAD_ANIM_NONE;
		else if (cur_ui != HEALTH_UI && app_ui_stack_prev(NULL) == APPLIST_UI)
			anim_type = APP_UI_LOAD_ANIM_ZOOM_OUT;

		app_ui_stack_back_gestured(anim_type);
	}

#ifdef CONFIG_INPUT_DEV_ACTS_KNOB
	if(event == KEY_KONB_CLOCKWISE) {
		SYS_LOG_INF("KEY_KONB_CLOCKWISE");
	} else if(event == KEY_KONB_ANTICLOCKWISE) {
		SYS_LOG_INF("KEY_KONB_ANTICLOCKWISE");
	}
#endif
#endif /* CONFIG_UI_MANAGER */
}

static void _launcher_input_event_handle(struct app_msg *msg)
{
#if defined(CONFIG_BT_CALL_APP) || defined(CONFIG_ALARM_APP)
	launcher_app_t *app = launcher_app_get();
#endif

#ifdef CONFIG_BT_CALL_APP
	if (app->cur_player == BTCALL_PLAYER ||
		msg->cmd == MSG_BT_SIRI_START ||
		msg->cmd == MSG_BT_SIRI_STOP) {
		btcall_input_event_proc(msg);
		return;
	}
#endif

#ifdef CONFIG_ALARM_APP
	if (app->cur_player == ALARM_PLAYER) {
		alarm_input_event_proc(msg);
		return;
	}
#endif

#ifdef CONFIG_BT_PLAYER
	if (msg->reserve) { /*(app->cur_player == BTMUSIC_PLAYER)*/
		btmusic_view_input_event_proc(msg);
		return;
	}
#endif

#ifdef CONFIG_LCMUSIC_APP
	if (msg->reserve == 0) {
		lcmusic_input_event_proc(msg);
		return;
	}
#endif
}
static void _launcher_tts_event_handle(struct app_msg *msg)
{
#ifdef CONFIG_BT_CALL_APP
	launcher_app_t *app = launcher_app_get();

	if (app->cur_player == BTCALL_PLAYER) {
		btcall_tts_event_proc(msg);
		return;
	}
#endif
}

void launcher_restore_last_player(void)
{
	launcher_app_t *app = launcher_app_get();

	if (app->cur_player != LCMUSIC_PLAYER && app->cur_player != BTMUSIC_PLAYER) {
		SYS_LOG_INF("restore player %d\n", app->prev_player);
		app->cur_player = app->prev_player;
	#ifdef CONFIG_LCMUSIC_APP
		lcmusic_init(CONFIG_APP_FAT_DISK"/");
	#endif
		if(app->cur_player == BTMUSIC_PLAYER) {
	#ifdef CONFIG_BT_PLAYER
			btmusic_check_remote_state();
	#endif
		}
	}
}

void _launcher_app_loop(void *p1, void *p2, void *p3)
{
	launcher_app_t *app = NULL;
	struct app_msg msg = {0};
	bool terminated = false;
	int timeout;

	SYS_LOG_INF(APP_ID_LAUNCHER " enter");

	if (_launcher_app_init()) {
		SYS_LOG_ERR(APP_ID_LAUNCHER " init failed");
		_launcher_app_exit();
		goto out_exit;
	}

#ifdef CONFIG_ALARM_APP
	if (alarm_wakeup_source_check()) {
		alarm_start_msg_send();
	}
#endif
#ifdef CONFIG_ACTS_DVFS_DYNAMIC_LEVEL
	dvfs_unset_level(DVFS_LEVEL_HIGH_PERFORMANCE, "init");
	dvfs_set_level(DVFS_LEVEL_NORMAL, "launcher");
#endif
	app = launcher_app_get();

	while (!terminated) {
		timeout = (app->suspended == 2) ?
				OS_FOREVER : thread_timer_next_timeout();

		if (receive_msg(&msg, timeout)) {
			switch (msg.type) {
			case MSG_EXIT_APP:
				_launcher_app_exit();
				terminated = true;
				break;

			case MSG_EARLY_SUSPEND_APP:
				_launcher_app_early_suspend();
				break;
			case MSG_LATE_RESUME_APP:
				_launcher_app_late_resume();
				break;
			case MSG_SUSPEND_APP:
				_launcher_app_suspend();
				break;
			case MSG_RESUME_APP:
				_launcher_app_resume();
				break;
			case MSG_KEY_INPUT:
				_launcher_key_event_handle(VIEW_INVALID_ID, msg.value);
				break;
			case MSG_INPUT_EVENT:
				_launcher_input_event_handle(&msg);
				break;

		#ifdef CONFIG_LCMUSIC_APP
			case MSG_LCMUSIC_EVENT:
				lcmusic_event_proc(&msg);
				break;
		#endif
			case MSG_TTS_EVENT:
				_launcher_tts_event_handle(&msg);
				break;

		#ifdef CONFIG_ALARM_APP
			case MSG_ALARM_EVENT:
				_launcher_alarm_event_handle(&msg);
				break;
		#endif

			case MSG_SWITCH_LAST_PLAYER:
				launcher_restore_last_player();
				break;

		#if defined(CONFIG_UI_MANAGER) && !defined(CONFIG_SIMULATOR)
			case MSG_SYS_EVENT:
				if (msg.cmd == SYS_EVENT_BATTERY_LOW) {
					system_create_low_power_view();
				}
				break;
			case MSG_BAT_CHARGE_EVENT:
				if (msg.cmd == BAT_CHG_EVENT_DC5V_IN) {
					system_delete_low_power_view();
				}
				break;
		#endif /* defined(CONFIG_UI_MANAGER) && !defined(CONFIG_SIMULATOR) */

		#ifdef CONFIG_BT_MANAGER
			case MSG_BT_EVENT:
				_launcher_bt_event_handle(&msg);
				break;
			case MSG_BT_MGR_EVENT:
				_launcher_bt_mgr_event_handle(&msg);
				break;
		#endif /* CONFIG_BT_MANAGER */

			default:
				break;
			}

			if (msg.callback != NULL)
				msg.callback(&msg, 0, NULL);
		}

		thread_timer_handle_expired();
	}

out_exit:
	SYS_LOG_INF(APP_ID_LAUNCHER " exit");
}

APP_DEFINE(launcher, share_stack_area, sizeof(share_stack_area),
		CONFIG_APP_PRIORITY, DEFAULT_APP | FOREGROUND_APP, NULL, NULL, NULL,
		_launcher_app_loop, NULL);
