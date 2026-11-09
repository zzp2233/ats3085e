/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file clock_selector_ui.h
 */

#ifndef SRC_LAUNCHER_CLOCK_SELECTOR_UI_H_
#define SRC_LAUNCHER_CLOCK_SELECTOR_UI_H_

/*********************
 *      INCLUDES
 *********************/
#include <app_ui.h>

/**********************
 *      TYPEDEFS
 **********************/
typedef struct clock_selector_ui_presenter {
	uint8_t (*get_clock_id)(void);
	const char *(*get_clock_name)(uint8_t id);
	void (*set_clock_id)(uint8_t id);
} clock_selector_ui_presenter_t;

extern const clock_selector_ui_presenter_t g_clocksel_ui_presenter;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
/**
 * CLOCKSEL_UI proc entry
 */
int clock_selector_ui_proc(void * scr, uint8_t msg_id, void * msg_data);

#endif /* SRC_LAUNCHER_CLOCK_SELECTOR_UI_H_ */
