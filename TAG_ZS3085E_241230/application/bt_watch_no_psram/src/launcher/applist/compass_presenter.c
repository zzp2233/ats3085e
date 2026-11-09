/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*********************
 *      INCLUDES
 *********************/
#include <os_common_api.h>
#include "../launcher_app.h"
#include "compass_ui.h"

/**********************
 *   STATIC PROTOTYPES
 **********************/
static uint16_t _compass_view_get_bearing(void);

/**********************
 *  GLOBAL VARIABLES
 **********************/
const compass_ui_presenter_t g_compass_ui_presenter = {
	.get_bearing = _compass_view_get_bearing,
};

/**********************
 *   STATIC FUNCTIONS
 **********************/
static uint16_t _compass_view_get_bearing(void)
{
	launcher_app_t *app = launcher_app_get();

	return app->bearing;
}
