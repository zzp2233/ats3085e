/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file health_presenter.h
 */

/*********************
 *      INCLUDES
 *********************/
#include <os_common_api.h>
#include "health_ui.h"

/**********************
 *   STATIC PROTOTYPES
 **********************/
static void _bp_get_bp_range(uint8_t *low, uint8_t *hight);
static uint8_t _spo2_get_sop2(void);

/**********************
 *  GLOBAL VARIABLES
 **********************/
const bp_ui_presenter_t g_bp_ui_presenter = {
	.get_bp_range = _bp_get_bp_range,
};

const spo2_ui_presenter_t g_spo2_ui_presenter = {
	.get_spo2 = _spo2_get_sop2,
};

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void _bp_get_bp_range(uint8_t *low, uint8_t *high)
{
	*low = 70;
	*high = 113;
}

static uint8_t _spo2_get_sop2(void)
{
	return 99;
}
