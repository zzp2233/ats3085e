/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*********************
 *      INCLUDES
 *********************/
#include "desktop_ui.h"
#include "../launcher_app.h"
#include "../clock_selector/clock_selector.h"

#ifdef CONFIG_PROPERTY
#  include <property_manager.h>
#endif
#ifdef CONFIG_SYS_WAKELOCK
#  include <sys_wakelock.h>
#endif
#ifdef CONFIG_SENSOR_MANAGER
#  include <sensor_manager.h>
#endif

#ifndef CONFIG_SIMULATOR
#  include <soc.h>
#  include <drivers/power_supply.h>
#  include <power_manager.h>
#else
#  include <native_window.h>
#endif

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void _get_time(struct rtc_time * time);
static uint32_t _get_heart_rate(void);
static uint32_t _get_step_count(void);
static uint32_t _get_calories(void);
static uint32_t _get_sleep_time(void);
static uint32_t _get_distance(void);
static uint8_t _get_temperature(void);
static uint8_t _get_weather(void);
static uint8_t _get_battery_percent(void);
static bool _get_sleep_never(void);
static void _set_sleep_never(bool never);
static void _set_rtc(bool en, uint16_t period_ms);
static int _open_clock_preview(void);

/**********************
 *  GLOBAL VARIABLES
 **********************/
const desktop_ui_presenter_t g_desktop_ui_presenter = {
	.get_time = _get_time,
	.get_clock_id = clocksel_get_current_id,
	.get_heart_rate = _get_heart_rate,
	.get_step_count = _get_step_count,
	.get_calories = _get_calories,
	.get_sleep_time = _get_sleep_time,
	.get_distance = _get_distance,
	.get_temperature = _get_temperature,
	.get_weather = _get_weather,
	.get_battery_percent = _get_battery_percent,
	.get_sleep_never = _get_sleep_never,
	.set_sleep_never = _set_sleep_never,

	.set_rtc = _set_rtc,
	.open_clock_preview = _open_clock_preview,
};

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void _get_time(struct rtc_time * time)
{
	launcher_app_t *app = launcher_app_get();

#ifdef CONFIG_RTC_ACTS
	if (app->rtc_dev)
		rtc_get_time(app->rtc_dev, &app->rtc_time);
#elif defined(CONFIG_SIMULATOR)
	native_window_get_local_time(&app->rtc_time);
#endif /* CONFIG_RTC_ACTS */

	memcpy(time, &app->rtc_time, sizeof(*time));
}

static uint8_t _get_battery_percent(void)
{
#ifdef CONFIG_POWER
	return power_manager_get_battery_capacity();
#else
	return 100;
#endif
}

static uint32_t _get_heart_rate(void)
{
	launcher_app_t *app = launcher_app_get();

	return app->heart_rate;
}

static uint32_t _get_step_count(void)
{
	launcher_app_t *app = launcher_app_get();

	return app->step_count;
}

static uint32_t _get_calories(void)
{
	return 789;
}

static uint32_t _get_sleep_time(void)
{
	return 310;
}

static uint32_t _get_distance(void)
{
	return 12345;
}

static uint8_t _get_temperature(void)
{
	return 26;
}

static uint8_t _get_weather(void)
{
	return SUNNY;
}

static bool _get_sleep_never(void)
{
#ifdef CONFIG_PROPERTY
	return property_get_int(CFG_SLEEP_NEVER, 0) != 0;
#else
	return false;
#endif /* CONFIG_PROPERTY */
}

static void _set_sleep_never(bool never)
{
#ifdef CONFIG_SYS_WAKELOCK
	if (never) {
		sys_wake_lock(FULL_WAKE_LOCK);
	} else {
		sys_wake_unlock(FULL_WAKE_LOCK);
	}
#endif /* CONFIG_SYS_WAKELOCK */

#ifdef CONFIG_PROPERTY
	property_set_int(CFG_SLEEP_NEVER, never);
	property_flush(CFG_SLEEP_NEVER);
#endif /* CONFIG_PROPERTY */
}

#ifdef CONFIG_SIMULATOR
static void _nwin_rtc_timer_period_handler(void * user_data)
{
	app_ui_paint(CLOCK_SCENE, NULL);
}
#endif /* CONFIG_SIMULATOR */

static void _set_rtc(bool en, uint16_t period_ms)
{
	SYS_LOG_INF("rtc: en %d, period %u", en, period_ms);

#ifndef CONFIG_SIMULATOR
	launcher_set_rtc_period(en, period_ms);
#else
	if (en) {
		native_window_register_callback(NWIN_CB_CLOCK, _nwin_rtc_timer_period_handler, NULL);
	} else {
		native_window_unregister_callback(NWIN_CB_CLOCK);
	}
#endif /* CONFIG_SIMULATOR */
}

static int _open_clock_preview(void)
{
	return clocksel_ui_enter();
}
