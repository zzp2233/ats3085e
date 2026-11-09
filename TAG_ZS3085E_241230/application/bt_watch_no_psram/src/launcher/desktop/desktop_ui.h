/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file desktop_ui.h
 */

#ifndef SRC_LAUNCHER_DESKTOP_DESKTOP_UI_H_
#define SRC_LAUNCHER_DESKTOP_DESKTOP_UI_H_

/*********************
 *      INCLUDES
 *********************/
#include <lvgl/lvgl.h>
#include <drivers/rtc.h>
#include <app_ui.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      TYPEDEFS
 **********************/
/**
 * @brief weather values
 */
enum {
	SUNNY = 0,
	CLOUDY,
	HAIL,
	SANDSTORM,
	FOG,
	HEAVY_RAIN,
	HEAVY_SNOW,
	THUNDERSTORM,
	CLEAR_TO_OVERCAST,

	NUM_WEATHER_STA,
};

/**
 * @brief desktop view presenter
 */
typedef struct {
	void (*get_time)(struct rtc_time * time);
	uint8_t (*get_clock_id)(void);
	uint32_t (*get_heart_rate)(void);
	uint32_t (*get_step_count)(void);
	uint32_t (*get_calories)(void);
	uint32_t (*get_sleep_time)(void); /* in minutes */
	uint32_t (*get_distance)(void);   /* in meters */
	uint8_t (*get_temperature)(void);
	uint8_t (*get_weather)(void);
	uint8_t (*get_battery_percent)(void);

	bool (*get_sleep_never)(void);
	void (*set_sleep_never)(bool never);

	void (*set_rtc)(bool en, uint16_t period_ms);
	int (*open_clock_preview)(void);
} desktop_ui_presenter_t;

extern const desktop_ui_presenter_t g_desktop_ui_presenter;

typedef struct {
	char name[256];  /* complete name */
	char album[256]; /* album name */
	uint32_t duration; /* total time in ms */
} music_presenter_id3_info_t;

typedef struct {
	/* Initialization */
	int (* init)(void);
	void (* deinit)(void);

	/* play type: LCMUSIC_PLAYER or BTMUSIC_PLAYER */
	uint8_t (* get_play_type)(void);
	int (* set_play_type)(uint8_t type);

	/* play mode */
	uint8_t (* get_play_mode)(void);
	int (* set_play_mode)(uint8_t mode);

	/* volume */
	void (* get_volume_range)(uint8_t * min_vol, uint8_t * max_vol);
	uint8_t (* get_volume)(void);
	int (* set_volume)(uint8_t vol);

	/* get ID3 info: return true if the ID3 is updated since last called */
	bool (* get_id3_info)(music_presenter_id3_info_t * info);
	void (* get_play_time)(uint32_t * cur_time, uint32_t * total_time);

	/* remote state */
	bool (* is_remote_id3_updated)(void);
	bool (* is_remote_vol_updated)(void);
	int (* check_remote_state)(void);
	int (* check_remote_playpos)(void);

	/* control */
	bool (* is_playing)(void);
	int (* pause)(bool forced);
	int (* play)(void);
	int (* play_prev)(void);
	int (* play_next)(void);
	int (* seek)(uint16_t percent);
	bool (* is_seeking_time)(void);
} music_presenter_t;

extern const music_presenter_t g_music_ui_presenter;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Proc of UI scenes on MAIN_UI
 */
int sport_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
int clock_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
int heart_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
int music_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
int ctrl_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
int message_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
int alipay_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);
int wxpay_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data);

/**
 * @brief Initialize music presenter
 *
 * @revtal 0 on success else negative code.
 */
static inline int music_ui_presenter_init(void)
{
	return g_music_ui_presenter.init();
}

/**
 * @brief DeInitialize music presenter
 *
 * @revtal N/A.
 */
static inline void music_ui_presenter_deinit(void)
{
	g_music_ui_presenter.deinit();
}

/**
 * @brief Get current play type by music presenter
 *
 * @revtal play type.
 */
static inline uint8_t music_ui_presenter_get_default_player(void)
{
	return g_music_ui_presenter.get_play_type();
}

/**
 * @brief Enter AOD clock UI
 *
 * @revtal 0 on success else negative code.
 */
int aod_clock_ui_enter(void);

/**
 * @brief Exit AOD clock UI
 *
 * @revtal 0 on success else negative code.
 */
int aod_clock_ui_exit(void);

/**
 * @brief AOD clock UI proc callback
 *
 * @revtal 0 on success else negative code.
 */
int aod_clock_ui_proc(void * scr, uint8_t msg_id, void * msg_data);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* SRC_LAUNCHER_DESKTOP_DESKTOP_UI_H_ */
