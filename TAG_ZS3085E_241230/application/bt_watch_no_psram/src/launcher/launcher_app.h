/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_WATCH_SRC_LAUNCHER_LAUNCHER_APP_H_
#define BT_WATCH_SRC_LAUNCHER_LAUNCHER_APP_H_

#include <os_common_api.h>
#include <thread_timer.h>
#include <drivers/rtc.h>
#ifdef CONFIG_SENSOR_MANAGER
#include <sensor_manager.h>
#endif
#include <msg_manager.h>

enum launcher_player_type {
	LCMUSIC_PLAYER = 0,
	BTMUSIC_PLAYER,
	BTCALL_PLAYER,
	ALARM_PLAYER,

	NUM_PLAYERS,
};

typedef struct {
	struct rtc_time rtc_time;

#ifdef CONFIG_RTC_ACTS
	const struct device *rtc_dev;
	struct rtc_alarm_period_config rtc_config;
#elif defined(CONFIG_SIMULATOR)
	/* nothing */
#elif defined(CONFIG_THREAD_TIMER)
	struct thread_timer rtc_timer;
#endif

	uint16_t in_aod_view : 1;
	uint16_t prev_player : 3;/*0--lcmusic player, 1--btmusic player*/
	uint16_t cur_player : 3;/*0--lcmusic player, 1--btmusic player*/
	uint16_t alarm_en : 1;/*0--normal, 1--alarm enable*/
	uint16_t suspended : 2;/*0--active, 1--early-suspended, 2--suspended */

	uint32_t step_count;
	uint32_t heart_rate;
	uint32_t calories;
	uint32_t bearing;
#ifdef CONFIG_SENSOR_MANAGER
	sensor_res_t sensor_res;
#endif
} launcher_app_t;

static inline launcher_app_t *launcher_app_get(void)
{
	extern launcher_app_t g_launcher_app;

	return &g_launcher_app;
}

static inline uint8_t launcher_get_cur_player(void)
{
	launcher_app_t *app = launcher_app_get();

	return app->cur_player;
}

static inline bool launcher_is_resumed(void)
{
	extern launcher_app_t g_launcher_app;

	return (g_launcher_app.suspended == 0) ? true : false;
}

void launcher_restore_last_player(void);
void launcher_set_rtc_period(bool en, uint16_t period_ms);

#endif /* BT_WATCH_SRC_LAUNCHER_LAUNCHER_APP_H_ */
