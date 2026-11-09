/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_WATCH_SRC_LAUNCHER_GPS_UI_H_
#define BT_WATCH_SRC_LAUNCHER_GPS_UI_H_

/*********************
 *      INCLUDES
 *********************/
#include <app_ui.h>
#ifdef CONFIG_GPS_MANAGER
#include <gps_manager.h>
#endif

/**********************
 *      TYPEDEFS
 **********************/
typedef struct gps_view_presenter {
#ifdef CONFIG_GPS_MANAGER
	gps_res_t* (*get_gps_data)(void);
#endif
	void (*reserved)(void);
} gps_view_presenter_t;

extern const gps_view_presenter_t gps_view_presenter;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void gps_ui_init(void);
void gps_ui_enter(int anim_type);

int gps_ui_proc(void * scr, uint8_t msg_id, void * msg_data);

#endif /* BT_WATCH_SRC_LAUNCHER_GPS_UI_H_ */
