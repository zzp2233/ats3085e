/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file music_presenter.c
 */

/*********************
 *      INCLUDES
 *********************/
#include <os_common_api.h>
#include "desktop_ui.h"
#include "../launcher_app.h"

#ifdef CONFIG_AUDIO
#  include <audio_system.h>
#  include <audio_policy.h>
#  include <volume_manager.h>
#endif

#ifdef CONFIG_BT_PLAYER
#  include <bt_player/bt_player.h>
#  include <bt_manager.h>
#endif

#ifdef CONFIG_BT_TRANSMIT
#  include <bt_transmit/bt_transmit.h>
#endif

#ifdef CONFIG_LCMUSIC_APP
#  include <lcmusic/lcmusic.h>
#endif

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
	uint8_t player;
	uint8_t volume;
	int playing_time;

	bool remote_vol_updated;
	bool remote_id3_updated;
	music_presenter_id3_info_t id3_info;
} music_presenter_priv_data_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static int _music_presenter_init(void);
static void _music_presenter_deinit(void);

static uint8_t _music_get_play_type(void);
static int _music_set_play_type(uint8_t type);

static uint8_t _music_get_play_mode(void);
static int _music_set_play_mode(uint8_t mode);

static void _music_get_volume_range(uint8_t * min_vol, uint8_t * max_vol);
static uint8_t _music_get_volume(void);
static int _music_set_volume(uint8_t vol);

static bool _music_get_id3_info(music_presenter_id3_info_t * info);
static void _music_get_play_time(uint32_t * p_cur_time, uint32_t * p_total_time);

static bool _music_is_remote_id3_updated(void);
static bool _music_is_remote_vol_updated(void);
static int _music_check_remote_play_state(void);
static int _music_check_remote_play_time(void);

static bool _music_is_playing(void);
static int _music_pause(bool forced);
static int _music_play(void);
static int _music_play_prev(void);
static int _music_play_next(void);
static int _music_seek(uint16_t percent);
static bool _music_is_seeking_time(void);

#ifdef CONFIG_BT_PLAYER
static void _sync_btmusic_play_info(const char *song_name, const char *album, uint32_t total_time);
static void _sync_btmusic_play_pos(uint32_t cur_time);
static void _sync_btmusic_volume(int volume);
#endif /* CONFIG_BT_PLAYER */

/**********************
 *  GLOBAL VARIABLES
 **********************/
const music_presenter_t g_music_ui_presenter = {
	.init = _music_presenter_init,
	.deinit = _music_presenter_deinit,

	.get_play_type = _music_get_play_type,
	.set_play_type = _music_set_play_type,

	.get_play_mode = _music_get_play_mode,
	.set_play_mode = _music_set_play_mode,

	.get_volume_range = _music_get_volume_range,
	.get_volume = _music_get_volume,
	.set_volume = _music_set_volume,

	.get_id3_info = _music_get_id3_info,
	.get_play_time = _music_get_play_time,

	.is_remote_id3_updated = _music_is_remote_id3_updated,
	.is_remote_vol_updated = _music_is_remote_vol_updated,
	.check_remote_state = _music_check_remote_play_state,
	.check_remote_playpos = _music_check_remote_play_time,

	.is_playing = _music_is_playing,
	.pause = _music_pause,
	.play = _music_play,
	.play_prev = _music_play_prev,
	.play_next = _music_play_next,
	.seek = _music_seek,
	.is_seeking_time = _music_is_seeking_time,
};

/**********************
 *   STATIC VARIABLES
 **********************/
static music_presenter_priv_data_t s_music_priv = {
	.player = LCMUSIC_PLAYER,
};

#ifdef CONFIG_BT_PLAYER
static const btmusic_callback_t s_btmusic_callback = {
	.set_play_info = _sync_btmusic_play_info,
	.set_play_pos = _sync_btmusic_play_pos,
	.set_volume = _sync_btmusic_volume,
};
#endif

static OS_MUTEX_DEFINE(s_music_mutex);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static int _music_presenter_init(void)
{
#ifdef CONFIG_BT_PLAYER
	btmusic_set_play_callback(&s_btmusic_callback);
#endif

	return 0;
}

static void _music_presenter_deinit(void)
{
#ifdef CONFIG_BT_PLAYER
	btmusic_set_play_callback(NULL);
#endif

	os_mutex_lock(&s_music_mutex, OS_FOREVER);
	memset(&s_music_priv.id3_info, 0, sizeof(s_music_priv.id3_info));
	os_mutex_unlock(&s_music_mutex);
}

static uint8_t _music_get_play_type(void)
{
	return s_music_priv.player;
}

static int _music_set_play_type(uint8_t type)
{
	s_music_priv.player = type;
	return 0;
}

static uint8_t _music_get_play_mode(void)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		return lcmusic_get_play_mode(); /* enum MPLAYER_PLAY_MODE */
#endif

	return 0;
}

static int _music_set_play_mode(uint8_t mode)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		lcmusic_set_play_mode();
#endif

	return -ENOTSUP;
}

static void _music_get_volume_range(uint8_t * min_vol, uint8_t * max_vol)
{
	*min_vol = 0;

#ifdef CONFIG_AUDIO
	*max_vol = audio_policy_get_volume_level();
#else
	*max_vol = 15;
#endif
}

static uint8_t _music_get_volume(void)
{
#ifdef CONFIG_AUDIO
	if (s_music_priv.player == LCMUSIC_PLAYER) {
		s_music_priv.volume = system_volume_get(AUDIO_STREAM_LOCAL_MUSIC);
	} else {
		s_music_priv.volume = system_volume_get(AUDIO_STREAM_MUSIC);
	}
#endif

	s_music_priv.remote_vol_updated = false;
	return s_music_priv.volume;
}

static int _music_set_volume(uint8_t vol)
{
	if (s_music_priv.volume == vol)
		return 0;

#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		lcmusic_vol_sync(vol);
#endif

#ifdef CONFIG_BT_PLAYER
	if (s_music_priv.player == BTMUSIC_PLAYER)
		btmusic_vol_sync(vol);
#endif

	s_music_priv.volume = vol;
	return 0;
}

static bool _music_get_id3_info(music_presenter_id3_info_t * info)
{
#if defined(CONFIG_LCMUSIC_APP) || defined(CONFIG_BT_PLAYER)
	music_presenter_id3_info_t *id3_info = &s_music_priv.id3_info;
#endif
	bool updated = false;

#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER) {
		char *song_name = NULL;
		uint16_t len = 0;

		lcmusic_get_song_name(&song_name, &len);

		os_mutex_lock(&s_music_mutex, OS_FOREVER);

		if (len > 0) {
			if (len >= sizeof(id3_info->name))
				len = sizeof(id3_info->name) - 1;

			memcpy(id3_info->name, song_name, len);
			memcpy(id3_info->album, song_name, len);

			updated = true;
		}

		id3_info->name[len] = 0;
		id3_info->album[len] = 0;
		id3_info->duration = 0;

		if (updated)
			memcpy(info, id3_info, sizeof(*info));

		os_mutex_unlock(&s_music_mutex);
	}
#endif /* CONFIG_LCMUSIC_APP */

#ifdef CONFIG_BT_PLAYER
	if (s_music_priv.player == BTMUSIC_PLAYER) {
		os_mutex_lock(&s_music_mutex, OS_FOREVER);

		updated = s_music_priv.remote_id3_updated;
		s_music_priv.remote_id3_updated = false;
		memcpy(info, id3_info, sizeof(*info));

		os_mutex_unlock(&s_music_mutex);
	}
#endif

	return updated;
}

static void _music_get_play_time(uint32_t * p_cur_time, uint32_t * p_total_time)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		lcmusic_get_song_bp(&s_music_priv.id3_info.duration, &s_music_priv.playing_time);
#endif

	if (p_total_time)
		*p_total_time = s_music_priv.id3_info.duration;
	if (p_cur_time)
		*p_cur_time = s_music_priv.playing_time;
}

static bool _music_is_remote_id3_updated(void)
{
	return s_music_priv.remote_id3_updated;
}

static bool _music_is_remote_vol_updated(void)
{
	return s_music_priv.remote_vol_updated;
}

static int _music_check_remote_play_state(void)
{
#ifdef CONFIG_BT_PLAYER
	btmusic_check_remote_state();
#endif

	return 0;
}

static int _music_check_remote_play_time(void)
{
#ifdef CONFIG_BT_PLAYER
	return bt_manager_avrcp_get_playback_pos();
#else
	return 0;
#endif
}

static bool _music_is_playing(void)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		return lcmusic_is_playing();
#endif

#ifdef CONFIG_BT_PLAYER
	if (s_music_priv.player == BTMUSIC_PLAYER)
		return btmusic_get_play_state();
#endif

	return false;
}

static int _music_pause(bool forced)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		lcmusic_stop();
#endif

#ifdef CONFIG_BT_PLAYER
	if (s_music_priv.player == BTMUSIC_PLAYER)
		btmusic_stop(forced);
#endif

	return 0;
}

static int _music_play(void)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		lcmusic_start_player();
#endif

#ifdef CONFIG_BT_PLAYER
	if (s_music_priv.player == BTMUSIC_PLAYER)
		btmusic_start();
#endif

	return 0;
}

static int _music_play_prev(void)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		lcmusic_play_prev();
#endif

#ifdef CONFIG_BT_PLAYER
	if (s_music_priv.player == BTMUSIC_PLAYER)
		btmusic_play_prev();
#endif

	return 0;
}

static int _music_play_next(void)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		lcmusic_play_next();
#endif

#ifdef CONFIG_BT_PLAYER
	if (s_music_priv.player == BTMUSIC_PLAYER)
		btmusic_play_next();
#endif

	return 0;
}

static int _music_seek(uint16_t percent)
{
#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER) {
		uint32_t total_time = 0;
		int cur_time = 0;
		int seek_time;

		lcmusic_get_song_bp(&total_time, &cur_time);
		seek_time = (percent * total_time) / 100;
		lcmusic_seek(seek_time, cur_time, true);

		SYS_LOG_INF("time: total %u, cur %d, seek %d",
				total_time, cur_time, seek_time - cur_time);
		return 0;
	}
#endif

	return -ENOTSUP;
}

static bool _music_is_seeking_time(void)
{
	#ifdef CONFIG_LCMUSIC_APP
	if (s_music_priv.player == LCMUSIC_PLAYER)
		return lcmusic_is_seeking_time();
	#endif 

	return false;
}

#ifdef CONFIG_BT_PLAYER
static void _sync_btmusic_play_info(const char *song_name, const char *album, uint32_t total_time)
{
	music_presenter_id3_info_t *id3_info = &s_music_priv.id3_info;
	int name_len = strlen(song_name);
	int album_len = strlen(album);

	if (name_len >= sizeof(id3_info->name))
		name_len = sizeof(id3_info->name) - 1;

	if (album_len >= sizeof(id3_info->album))
		album_len = sizeof(id3_info->album) - 1;

	os_mutex_lock(&s_music_mutex, OS_FOREVER);

	memcpy(id3_info->name, song_name, name_len);
	id3_info->name[name_len] = 0;

	memcpy(id3_info->album, album, album_len);
	id3_info->album[album_len] = 0;

	id3_info->duration = total_time;
	s_music_priv.remote_id3_updated = true;

	os_mutex_unlock(&s_music_mutex);
}

static void _sync_btmusic_play_pos(uint32_t cur_time)
{
	s_music_priv.playing_time = cur_time;
}

static void _sync_btmusic_volume(int volume)
{
	s_music_priv.remote_vol_updated = true;
}

#endif /* CONFIG_BT_PLAYER */
