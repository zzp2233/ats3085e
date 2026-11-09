/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_WATCH_SRC_LAUNCHER_VIBRATION_UI_H_
#define BT_WATCH_SRC_LAUNCHER_VIBRATION_UI_H_

/*********************
 *      INCLUDES
 *********************/
#include <app_ui.h>

/**********************
 *      TYPEDEFS
 **********************/
typedef struct vibration_view_presenter {
	void (*start)(void);
	void (*stop)(void);
	void (*set_level)(uint8_t value);
	uint8_t (*get_level)(void);
	void (*set_freq)(uint32_t value);
} vibration_view_presenter_t;

extern const vibration_view_presenter_t vibration_view_presenter;

enum {
	LEVEL_1 = 0,
	LEVEL_2,
	LEVEL_3,
	LEVEL_NUM,
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void vibration_ui_enter(int anim_type);
int vibration_ui_proc(void * scr, uint8_t msg_id, void * msg_data);

#endif /* BT_WATCH_SRC_LAUNCHER_GPS_UI_H_ */
