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
#include "../clock_selector/clock_selector.h"

#define CLOCK_SWITCH_LONG_PRESSED_CNT \
		((1000 - LV_INDEV_DEF_LONG_PRESS_TIME) / LV_INDEV_DEF_LONG_PRESS_REP_TIME)

enum {
	INFO_STEPCOUNT = 0,
	INFO_HEARTRATE,
	INFO_CALORIES,
	INFO_DISTANCE,
	INFO_SLEEPTIME,

	INFO_BATTERY,

	INFO_TEMPERATURE,
	INFO_WEATHER,

	INFO_ANIM,
	INFO_DATE,
	INFO_LUNAR,

	NUM_INFO,
};

typedef struct {
	lvgl_res_group_t grp_hour;
	lvgl_res_group_t grp_minute;
	lvgl_res_group_t grp_second;
	lvgl_res_group_t grp_stepcount;
	lvgl_res_group_t grp_heartrate;
	lvgl_res_group_t grp_calories;
	lvgl_res_group_t grp_distance;
	lvgl_res_group_t grp_sleeptime;
	lvgl_res_group_t grp_battery;
	lvgl_res_group_t grp_temperature;

	lv_point_t pt_colon;
} clock_ui_res_t;

typedef struct {
	bool is_digital;
	uint16_t period;
	const clock_dsc_t * clock_dsc;

	/* previous value */
	uint8_t tm_mday;
	uint8_t tm_mon;
	uint16_t tm_year;
	uint32_t distance;
	uint32_t sleep_time;

	lv_obj_t *obj_clock;
	lv_obj_t *obj_info[NUM_INFO];

	lv_font_t font;
	lv_style_t sty_text[2];

	/* lvgl resource */
	lv_img_dsc_t img_bg;
	lv_img_dsc_t img_tm_colon; /* : between hour and minute */
	lv_img_dsc_t *img_tm_hour; /* 0~9 */
	lv_img_dsc_t *img_tm_min;  /* 0~9 */
	lv_img_dsc_t *img_tm_sec;  /* 0~9 */

	lv_img_dsc_t *img_stepcount; /* 0~9 */
	lv_img_dsc_t *img_heartrate; /* 0~9 */
	lv_img_dsc_t *img_calories;  /* 0~9 */
	lv_img_dsc_t *img_distance;  /* 0~9 . k m */
	lv_img_dsc_t *img_sleeptime; /* 0~9 h m */
	lv_img_dsc_t *img_battery;   /* 0~9 */
	lv_img_dsc_t *img_temperature; /* 0~9 */

	lvgl_res_string_t res_str[2];
	lvgl_res_picregion_t prg_anim;
	lvgl_res_group_t grp_weather;
	lvgl_res_scene_t scene;

	/* click event */
	int8_t pressed_cnt;
	bool preview_invoked;

	const desktop_ui_presenter_t *presenter;
} clock_ui_data_t;

static int _clock_paint(lv_obj_t * scr, bool forced);

static const uint32_t pic_common_ids[] = {
	PIC_0, PIC_1, PIC_2, PIC_3, PIC_4, PIC_5, PIC_6, PIC_7, PIC_8, PIC_9,
};

static const uint32_t pic_weather_ids[] = {
	PIC_SUNNY, PIC_CLOUDY, PIC_HAIL, PIC_SANDSTORM, PIC_FOG, PIC_HEAVYRAIN,
	PIC_HEAVYSNOW, PIC_THUNDERSTORM, PIC_CLEAR_TO_OVERCAST,
};

static const uint32_t pic_distance_ids[] = {
	PIC_0, PIC_1, PIC_2, PIC_3, PIC_4, PIC_5, PIC_6, PIC_7, PIC_8, PIC_9, PIC_DOT, PIC_K, PIC_M,
};

static const uint32_t pic_sleeptime_ids[] = {
	PIC_0, PIC_1, PIC_2, PIC_3, PIC_4, PIC_5, PIC_6, PIC_7, PIC_8, PIC_9, PIC_H, PIC_M,
};

static const uint32_t str_ids[] = { STR_DATE, STR_LUNAR };

static const char * txt_wday[7] = {
	//"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
	"周日", "周一", "周二", "周三", "周四", "周五", "周六"
};

static int _load_pictures(lvgl_res_scene_t *scene, uint32_t group_id,
		lvgl_res_group_t *group, const uint32_t *pic_ids,
		int num_ids, lv_img_dsc_t **images)
{
	*images = app_mem_malloc(num_ids * sizeof(lv_img_dsc_t));
	if (*images == NULL) {
		return -ENOMEM;
	}

	if (lvgl_res_load_group_from_scene(scene, group_id, group)) {
		goto fail_free_image;
	}

	if (lvgl_res_load_pictures_from_group(group, pic_ids, *images, NULL, num_ids)) {
		lvgl_res_unload_group(group);
		goto fail_free_image;
	}

	lvgl_res_unload_group(group);
	return 0;
fail_free_image:
	app_mem_free(*images);
	*images = NULL;

	return -ENOENT;
}

static void _unload_pictures(lv_img_dsc_t **images, int num_images)
{
	if (*images) {
		lvgl_res_unload_pictures(*images, num_images);
		app_mem_free(*images);
		*images = NULL;
	}
}

static int _clock_load_resource(clock_ui_data_t * data, clock_ui_res_t * tmp_res)
{
	uint32_t pic_id;
	int ret;

	/* scene */
	ret = lvgl_res_load_scene(data->clock_dsc->scene, &data->scene,
			DEF_STY_FILE, DEF_RES_FILE, DEF_STR_FILE);
	if (ret < 0) {
		SYS_LOG_ERR("scene 0x%x not found", data->clock_dsc->scene);
		return -ENOENT;
	}

	//test scene is digital or not
	ret = lvgl_res_load_group_from_scene(&data->scene, RES_DIGITAL_ID, &tmp_res->grp_hour);
	if (ret == 0) {
		data->is_digital = true;
		lvgl_res_unload_group(&tmp_res->grp_hour);
	}

	/* background picture */
	pic_id = PIC_BACKGROUND;
	ret = lvgl_res_load_pictures_from_scene(&data->scene, &pic_id, &data->img_bg, NULL, 1);

	/* hour */
	ret = _load_pictures(&data->scene, RES_TM_HOUR, &tmp_res->grp_hour,
			pic_common_ids, data->is_digital ? 10 : 1, &data->img_tm_hour);
	if (ret < 0) {
		return -ENOENT;
	}

	/* minute */
	ret = _load_pictures(&data->scene, RES_TM_MINUTE, &tmp_res->grp_minute,
			pic_common_ids, data->is_digital ? 10 : 1, &data->img_tm_min);
	if (ret < 0) {
		return -ENOENT;
	}

	/* second */
	ret = _load_pictures(&data->scene, RES_TM_SECOND, &tmp_res->grp_second,
			pic_common_ids, data->is_digital ? 10 : 1, &data->img_tm_sec);

	/* time colon */
	pic_id = PIC_TM_COLON;
	lvgl_res_load_pictures_from_scene(&data->scene, &pic_id,
			&data->img_tm_colon, &tmp_res->pt_colon, 1);

	/* step count */
	_load_pictures(&data->scene, RES_STEP_COUNT, &tmp_res->grp_stepcount,
			pic_common_ids, 10, &data->img_stepcount);
	/* heart rate */
	_load_pictures(&data->scene, RES_HEART_RATE, &tmp_res->grp_heartrate,
			pic_common_ids, 10, &data->img_heartrate);
	/* calories */
	_load_pictures(&data->scene, RES_CALORIES, &tmp_res->grp_calories,
			pic_common_ids, 10, &data->img_calories);
	/* distance */
	_load_pictures(&data->scene, RES_DISTANCE, &tmp_res->grp_distance,
			pic_distance_ids, ARRAY_SIZE(pic_distance_ids), &data->img_distance);
	/* sleep time */
	_load_pictures(&data->scene, RES_SLEEP_TIME, &tmp_res->grp_sleeptime,
			pic_sleeptime_ids, ARRAY_SIZE(pic_sleeptime_ids), &data->img_sleeptime);
	/* battery */
	_load_pictures(&data->scene, RES_BATTERY, &tmp_res->grp_battery,
			pic_common_ids, 10, &data->img_battery);
	/* temperature */
	_load_pictures(&data->scene, RES_TEMPERATURE, &tmp_res->grp_temperature,
			pic_common_ids, 10, &data->img_temperature);

	/* weather */
	lvgl_res_load_group_from_scene(&data->scene, RES_WEATHER, &data->grp_weather);
	/* animation */
	lvgl_res_load_picregion_from_scene(&data->scene, RES_ANIMATION, &data->prg_anim);
	/* date, weak and lunar */
	lvgl_res_load_strings_from_scene(&data->scene, str_ids, data->res_str, ARRAY_SIZE(str_ids));

	if (data->res_str[0].width > 0) {
		if (LVGL_FONT_OPEN_DEFAULT(&data->font, DEF_FONT_SIZE_SMALL) < 0) {
			return -ENOENT;
		}
	}

	return 0;
}

static void _clock_unload_resource(clock_ui_data_t * data)
{
	if (data->res_str[0].width > 0) {
		LVGL_FONT_CLOSE(&data->font);
	}

	_unload_pictures(&data->img_tm_hour, data->is_digital ? 10 : 1);
	_unload_pictures(&data->img_tm_min, data->is_digital ? 10 : 1);
	_unload_pictures(&data->img_tm_sec, data->is_digital ? 10 : 1);
	_unload_pictures(&data->img_stepcount, 10);
	_unload_pictures(&data->img_heartrate, 10);
	_unload_pictures(&data->img_calories, 10);
	_unload_pictures(&data->img_distance, ARRAY_SIZE(pic_distance_ids));
	_unload_pictures(&data->img_sleeptime, ARRAY_SIZE(pic_sleeptime_ids));
	_unload_pictures(&data->img_battery, 10);
	_unload_pictures(&data->img_temperature, 10);

	lvgl_res_unload_pictures(&data->img_bg, 1);
	lvgl_res_unload_pictures(&data->img_tm_colon, 1);

	lvgl_res_unload_group(&data->grp_weather);
	lvgl_res_unload_picregion(&data->prg_anim);
	lvgl_res_unload_strings(data->res_str, ARRAY_SIZE(data->res_str));
	lvgl_res_unload_scene(&data->scene);
}

static void _clock_event_handler(lv_event_t * e)
{
	lv_event_code_t event = lv_event_get_code(e);
	clock_ui_data_t *data = lv_event_get_user_data(e);
	const desktop_ui_presenter_t * presenter = data->presenter;

	if (data->preview_invoked)
		return;

	if (event == LV_EVENT_LONG_PRESSED) {
		data->pressed_cnt = CLOCK_SWITCH_LONG_PRESSED_CNT;
		data->preview_invoked = (data->pressed_cnt <= 0);
	} else if (event == LV_EVENT_LONG_PRESSED_REPEAT) {
		data->pressed_cnt--;
		data->preview_invoked = (data->pressed_cnt <= 0);
	}

	if (data->preview_invoked) {
		lv_indev_wait_release(lv_event_get_param(e));

		if (presenter->open_clock_preview) {
			presenter->open_clock_preview();
		}
	}
}

static int _clock_create(lv_obj_t * scr, const desktop_ui_presenter_t * presenter)
{
	clock_ui_res_t res;
	int i;

	clock_ui_data_t * data = app_mem_malloc(sizeof(*data));
	if (data == NULL)
		return -ENOMEM;

	memset(data, 0, sizeof(*data));
	data->presenter = presenter;
	data->clock_dsc = clocksel_get_clock_dsc(presenter->get_clock_id());
	data->period = data->clock_dsc->period;

	if (_clock_load_resource(data, &res)) {
		_clock_unload_resource(data);
		app_mem_free(data);
		return -ENOENT;
	}

	data->obj_clock = watch_clock_create(scr);
	watch_clock_set_type(data->obj_clock,
			data->is_digital ? DIGITAL_CLOCK : ANALOG_CLOCK,
			data->img_tm_sec ? true : false);
	watch_clock_set_24hour(data->obj_clock, data->is_digital);

	lv_obj_set_size(data->obj_clock, data->scene.width, data->scene.height);
	lv_obj_center(data->obj_clock);

	if (data->img_bg.data) {
		lv_obj_set_style_bg_img_src(data->obj_clock, &data->img_bg, LV_PART_MAIN);
		lv_obj_set_style_bg_img_opa(data->obj_clock, LV_OPA_COVER, LV_PART_MAIN);
	} else {
		lv_obj_set_style_bg_color(data->obj_clock, data->scene.background, LV_PART_MAIN);
		lv_obj_set_style_bg_opa(data->obj_clock, LV_OPA_COVER, LV_PART_MAIN);
	}

	lv_obj_add_event_cb(data->obj_clock, _clock_event_handler, LV_EVENT_ALL, data);

	if (data->is_digital) {
		lv_area_t area;
		lv_area_set(&area, res.grp_hour.x, res.grp_hour.y,
				res.grp_hour.x + res.grp_hour.width - 1,
				res.grp_hour.y + res.grp_hour.height - 1);
		watch_clock_set_digit_images(data->obj_clock, CLOCK_HOUR, 10, data->img_tm_hour, &area);

		lv_area_set(&area, res.grp_minute.x, res.grp_minute.y,
				res.grp_minute.x + res.grp_minute.width - 1,
				res.grp_minute.y + res.grp_minute.height - 1);
		watch_clock_set_digit_images(data->obj_clock, CLOCK_MIN, 10, data->img_tm_min, &area);

		if (data->img_tm_sec) {
			lv_area_set(&area, res.grp_second.x, res.grp_second.y,
					res.grp_second.x + res.grp_second.width - 1,
					res.grp_second.y + res.grp_second.height - 1);
			watch_clock_set_digit_images(data->obj_clock, CLOCK_SEC, 10, data->img_tm_sec, &area);
		}

		if (data->img_tm_colon.data) {
			watch_clock_set_separator_image(data->obj_clock,
					&data->img_tm_colon, res.pt_colon.x, res.pt_colon.y);
		}
	} else {
		lv_coord_t pivot_x, pivot_y;

		pivot_x = (data->scene.width / 2) - res.grp_hour.x;
		pivot_y = (data->scene.height / 2) - res.grp_hour.y;
		watch_clock_set_pointer_images(data->obj_clock, CLOCK_HOUR, 1, data->img_tm_hour, pivot_x, pivot_y);

		pivot_x = (data->scene.width / 2) - res.grp_minute.x;
		pivot_y = (data->scene.height / 2) - res.grp_minute.y;
		watch_clock_set_pointer_images(data->obj_clock, CLOCK_MIN, 1, data->img_tm_min, pivot_x, pivot_y);

		if (data->img_tm_sec) {
			pivot_x = (data->scene.width / 2) - res.grp_second.x;
			pivot_y = (data->scene.height / 2) - res.grp_second.y;
			watch_clock_set_pointer_images(data->obj_clock, CLOCK_SEC, 1, data->img_tm_sec, pivot_x, pivot_y);
		}
	}

	if (data->img_stepcount) {
		data->obj_info[INFO_STEPCOUNT] = img_number_create(data->obj_clock);
		if (data->obj_info[INFO_STEPCOUNT]) {
			lv_obj_set_pos(data->obj_info[INFO_STEPCOUNT], res.grp_stepcount.x, res.grp_stepcount.y);
			lv_obj_set_size(data->obj_info[INFO_STEPCOUNT], res.grp_stepcount.width, res.grp_stepcount.height);
			img_number_set_src(data->obj_info[INFO_STEPCOUNT], data->img_stepcount, 10);
		}
	}

	if (data->img_heartrate) {
		data->obj_info[INFO_HEARTRATE] = img_number_create(data->obj_clock);
		if (data->obj_info[INFO_HEARTRATE]) {
			lv_obj_set_pos(data->obj_info[INFO_HEARTRATE], res.grp_heartrate.x, res.grp_heartrate.y);
			lv_obj_set_size(data->obj_info[INFO_HEARTRATE], res.grp_heartrate.width, res.grp_heartrate.height);
			img_number_set_src(data->obj_info[INFO_HEARTRATE], data->img_heartrate, 10);
		}
	}

	if (data->img_calories) {
		data->obj_info[INFO_CALORIES] = img_number_create(data->obj_clock);
		if (data->obj_info[INFO_CALORIES]) {
			lv_obj_set_pos(data->obj_info[INFO_CALORIES], res.grp_calories.x, res.grp_calories.y);
			lv_obj_set_size(data->obj_info[INFO_CALORIES], res.grp_calories.width, res.grp_calories.height);
			img_number_set_src(data->obj_info[INFO_CALORIES], data->img_calories, 10);
		}
	}

	if (data->img_battery) {
		data->obj_info[INFO_BATTERY] = img_number_create(data->obj_clock);
		if (data->obj_info[INFO_BATTERY]) {
			lv_obj_set_pos(data->obj_info[INFO_BATTERY], res.grp_battery.x, res.grp_battery.y);
			lv_obj_set_size(data->obj_info[INFO_BATTERY], res.grp_battery.width, res.grp_battery.height);
			img_number_set_src(data->obj_info[INFO_BATTERY], data->img_battery, 10);
			img_number_set_align(data->obj_info[INFO_BATTERY], LV_ALIGN_RIGHT_MID);
		}
	}

	if (data->img_temperature) {
		data->obj_info[INFO_TEMPERATURE] = img_number_create(data->obj_clock);
		if (data->obj_info[INFO_TEMPERATURE]) {
			lv_obj_set_pos(data->obj_info[INFO_TEMPERATURE], res.grp_temperature.x, res.grp_temperature.y);
			lv_obj_set_size(data->obj_info[INFO_TEMPERATURE], res.grp_temperature.width, res.grp_temperature.height);
			img_number_set_src(data->obj_info[INFO_TEMPERATURE], data->img_temperature, 10);
		}
	}

	if (data->img_distance) {
		data->obj_info[INFO_DISTANCE] = img_label_create(data->obj_clock);
		if (data->obj_info[INFO_DISTANCE]) {
			lv_obj_set_pos(data->obj_info[INFO_DISTANCE], res.grp_distance.x, res.grp_distance.y);
			lv_obj_set_size(data->obj_info[INFO_DISTANCE], res.grp_distance.width, res.grp_distance.height);
			img_label_set_align(data->obj_info[INFO_DISTANCE], LV_ALIGN_RIGHT_MID);
			img_label_set_src(data->obj_info[INFO_DISTANCE], data->img_distance, ARRAY_SIZE(pic_distance_ids));
		}
	}

	if (data->img_sleeptime) {
		data->obj_info[INFO_SLEEPTIME] = img_label_create(data->obj_clock);
		if (data->obj_info[INFO_SLEEPTIME]) {
			lv_obj_set_pos(data->obj_info[INFO_SLEEPTIME], res.grp_sleeptime.x, res.grp_sleeptime.y);
			lv_obj_set_size(data->obj_info[INFO_SLEEPTIME], res.grp_sleeptime.width, res.grp_sleeptime.height);
			img_label_set_align(data->obj_info[INFO_SLEEPTIME], LV_ALIGN_LEFT_MID);
			img_label_set_src(data->obj_info[INFO_SLEEPTIME], data->img_sleeptime, ARRAY_SIZE(pic_sleeptime_ids));
		}
	}

	if (data->grp_weather.width > 0) {
		data->obj_info[INFO_WEATHER] = anim_img_create(data->obj_clock);
		if (data->obj_info[INFO_WEATHER]) {
			lv_obj_set_pos(data->obj_info[INFO_WEATHER], data->grp_weather.x, data->grp_weather.y);
			lv_obj_set_size(data->obj_info[INFO_WEATHER], data->grp_weather.width, data->grp_weather.height);
			anim_img_set_preload(data->obj_info[INFO_WEATHER], false);
			anim_img_set_src_picgroup(data->obj_info[INFO_WEATHER], data->clock_dsc->scene, &data->grp_weather,
					pic_weather_ids, ARRAY_SIZE(pic_weather_ids));
		}
	}

	if (data->prg_anim.width > 0) {
		data->obj_info[INFO_ANIM] = anim_img_create(data->obj_clock);
		if (data->obj_info[INFO_ANIM]) {
			lv_obj_set_pos(data->obj_info[INFO_ANIM], data->prg_anim.x, data->prg_anim.y);
			lv_obj_set_size(data->obj_info[INFO_ANIM], data->prg_anim.width, data->prg_anim.height);
			anim_img_set_src_picregion(data->obj_info[INFO_ANIM], data->clock_dsc->scene, &data->prg_anim);
			anim_img_set_duration(data->obj_info[INFO_ANIM], 16, 64 * data->prg_anim.frames);
			anim_img_set_repeat(data->obj_info[INFO_ANIM], 0, LV_ANIM_REPEAT_INFINITE);
		}
	}

	for (i = 0; i < ARRAY_SIZE(data->sty_text); i++) {
		lv_style_init(&data->sty_text[i]);
		lv_style_set_text_font(&data->sty_text[i], &data->font);
		lv_style_set_text_color(&data->sty_text[i], data->res_str[i].color);
		lv_style_set_text_align(&data->sty_text[i], LV_TEXT_ALIGN_LEFT);
		lv_style_set_bg_opa(&data->sty_text[i], LV_OPA_TRANSP);
	}

	if (data->res_str[0].width > 0) {
		data->obj_info[INFO_DATE] = lv_label_create(data->obj_clock);
		if (data->obj_info[INFO_DATE]) {
			lv_obj_set_pos(data->obj_info[INFO_DATE], data->res_str[0].x, data->res_str[0].y);
			lv_obj_set_height(data->obj_info[INFO_DATE], data->res_str[0].height);
			lv_obj_add_style(data->obj_info[INFO_DATE], &data->sty_text[0], LV_PART_MAIN);
			lv_label_set_text(data->obj_info[INFO_DATE], "周五 05-12");
		}
	}

	if (data->res_str[1].width > 0) {
		data->obj_info[INFO_LUNAR] = lv_label_create(data->obj_clock);
		if (data->obj_info[INFO_LUNAR]) {
			lv_obj_set_pos(data->obj_info[INFO_LUNAR], data->res_str[1].x, data->res_str[1].y);
			lv_obj_set_height(data->obj_info[INFO_LUNAR], data->res_str[1].height);
			lv_obj_add_style(data->obj_info[INFO_LUNAR], &data->sty_text[1], LV_PART_MAIN);
			lv_label_set_text(data->obj_info[INFO_LUNAR], "正月初五");
		}
	}

	for (i = 0; i < ARRAY_SIZE(data->obj_info); i++) {
		if (data->obj_info[i])
			watch_clock_add_info(data->obj_clock, data->obj_info[i]);
	}

	lv_obj_set_user_data(scr, data);

	/* update values */
	_clock_paint(scr, true);

	return 0;
}

static int _clock_paint(lv_obj_t * scr, bool forced)
{
	clock_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return -EINVAL;

	const desktop_ui_presenter_t * presenter = data->presenter;
	if (presenter == NULL)
		return -EINVAL;

	struct rtc_time time;
	presenter->get_time(&time);

	/* adjust rtc period */
	if (data->clock_dsc->period > 1000) {
		uint16_t period = data->clock_dsc->period / 1000;
		uint16_t sec_remain = (time.tm_min * 60 + time.tm_sec) % period;

		if (sec_remain > 1) {
			data->period = data->clock_dsc->period - sec_remain * 1000;
			presenter->set_rtc(true, data->period);
		} else if (data->period != data->clock_dsc->period) {
			data->period = data->clock_dsc->period;
			presenter->set_rtc(true, data->period);
		}
	}

	SYS_LOG_DBG("time %02u:%02u:%02u.%03u\n", time.tm_hour, time.tm_min, time.tm_sec, time.tm_ms);

	watch_clock_set_time(data->obj_clock, time.tm_hour, time.tm_min, time.tm_sec * 1000 + time.tm_ms);

	if (data->obj_info[INFO_DATE]) {
		if (forced || (data->tm_mday != time.tm_mday) ||
			(data->tm_mon != time.tm_mon) || (data->tm_year != time.tm_year)) {
			lv_label_set_text_fmt(data->obj_info[INFO_DATE], "%s %02d-%02d",
					txt_wday[time.tm_wday], time.tm_mon + 1, time.tm_mday);

			data->tm_mday = time.tm_mday;
			data->tm_mon = time.tm_mon;
			data->tm_year = time.tm_year;
		}
	}

	if (data->obj_info[INFO_STEPCOUNT])
		img_number_set_value(data->obj_info[INFO_STEPCOUNT], presenter->get_step_count(), 0);
	if (data->obj_info[INFO_HEARTRATE])
		img_number_set_value(data->obj_info[INFO_HEARTRATE], presenter->get_heart_rate(), 0);
	if (data->obj_info[INFO_CALORIES])
		img_number_set_value(data->obj_info[INFO_CALORIES], presenter->get_calories(), 0);
	if (data->obj_info[INFO_BATTERY])
		img_number_set_value(data->obj_info[INFO_BATTERY], presenter->get_battery_percent(), 0);
	if (data->obj_info[INFO_TEMPERATURE])
		img_number_set_value(data->obj_info[INFO_TEMPERATURE], presenter->get_temperature(), 0);

	if (data->obj_info[INFO_WEATHER]) /* update weather */
		anim_img_play_static(data->obj_info[INFO_WEATHER], presenter->get_weather());

	/* update distance */
	if (data->obj_info[INFO_DISTANCE]) {
		uint32_t value = presenter->get_distance() / 10;
		if (forced || data->distance != value) {
			data->distance = value;

			uint8_t buf[16];
			int count = split_decimal(value / 100, buf, sizeof(buf), 0);
			buf[count++] = 10;
			count += split_decimal(value % 100, &buf[count], sizeof(buf) - count, 2);
			buf[count++] = 11;
			buf[count++] = 12;
			img_label_set_text(data->obj_info[INFO_DISTANCE], buf, count);
		}
	}

	/* update sleep time */
	if (data->obj_info[INFO_SLEEPTIME]) {
		uint32_t value = presenter->get_sleep_time();
		if (forced || data->sleep_time != value) {
			data->sleep_time = value;

			uint8_t buf[16];
			int count = split_decimal(value / 60, buf, sizeof(buf), 0);
			buf[count++] = 10;
			count += split_decimal(value % 60, &buf[count], sizeof(buf) - count, 2);
			buf[count++] = 11;
			img_label_set_text(data->obj_info[INFO_SLEEPTIME], buf, count);
		}
	}

	return 0;
}

static int _clock_focus(lv_obj_t * scr, bool focused)
{
	clock_ui_data_t * data = lv_obj_get_user_data(scr);

	if (data == NULL)
		return -ESRCH;

	if (data->presenter)
		data->presenter->set_rtc(focused, data->clock_dsc->period);

	if (data->obj_info[INFO_ANIM]) {
		if (focused) {
			anim_img_start(data->obj_info[INFO_ANIM], true);
		} else {
			anim_img_stop(data->obj_info[INFO_ANIM]);
		}
	}

	return 0;
}

static int _clock_delete(lv_obj_t * scr)
{
	clock_ui_data_t * data = lv_obj_get_user_data(scr);
	if (data == NULL)
		return 0;

	lv_obj_clean(scr);
	lvgl_style_array_reset(data->sty_text, ARRAY_SIZE(data->sty_text));

	_clock_unload_resource(data);
	app_mem_free(data);
	return 0;
}

int clock_main_ui_proc(void * scr, uint8_t msg_id, void * msg_data)
{
	switch (msg_id) {
	case MSG_VIEW_CREATE:
		return _clock_create(scr, msg_data);
	case MSG_VIEW_PAINT:
		return _clock_paint(scr, false);
	case MSG_VIEW_FOCUS:
		return _clock_focus(scr, true);
	case MSG_VIEW_DEFOCUS:
		return _clock_focus(scr, false);
	case MSG_VIEW_DELETE:
		return _clock_delete(scr);
	default:
		break;
	}

	return 0;
}
