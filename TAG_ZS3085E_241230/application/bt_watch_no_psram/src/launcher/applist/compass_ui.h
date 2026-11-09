/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SRC_LAUNCHER_APPLIST_COMPASS_UI_H_
#define SRC_LAUNCHER_APPLIST_COMPASS_UI_H_

/*********************
 *      INCLUDES
 *********************/
#include <app_ui.h>

/**********************
 *      TYPEDEFS
 **********************/
typedef struct compass_ui_presenter {
	uint16_t (*get_bearing)(void); /* in degree */
} compass_ui_presenter_t;

extern const compass_ui_presenter_t g_compass_ui_presenter;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
/**
 * Enter COMPASS_UI
 */
int compass_ui_proc(void * scr, uint8_t msg_id, void * msg_data);

#endif /* SRC_LAUNCHER_APPLIST_COMPASS_UI_H_ */
