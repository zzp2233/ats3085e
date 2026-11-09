/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file health_ui.h
 */

#ifndef SRC_LAUNCHER_HEALTH_HEALTH_UI_H_
#define SRC_LAUNCHER_HEALTH_HEALTH_UI_H_

/*********************
 *      INCLUDES
 *********************/
#include <app_ui.h>

/**********************
 *      TYPEDEFS
 **********************/
typedef struct bp_ui_presenter {
	void (*get_bp_range)(uint8_t *low, uint8_t *high);
} bp_ui_presenter_t;

typedef struct spo2_ui_presenter {
	uint8_t (*get_spo2)(void);
} spo2_ui_presenter_t;

extern const bp_ui_presenter_t g_bp_ui_presenter;
extern const spo2_ui_presenter_t g_spo2_ui_presenter;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
/**
 * Proc of UI scenes on HEALTH_UI
 */
int health_bp_ui_proc(void * tile, uint8_t msg_id, void *msg_data);
int health_spo2_ui_proc(void * tile, uint8_t msg_id, void *msg_data);

#endif /* SRC_LAUNCHER_HEALTH_HEALTH_UI_H_ */
