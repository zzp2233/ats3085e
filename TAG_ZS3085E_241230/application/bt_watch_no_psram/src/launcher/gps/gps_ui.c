/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <os_common_api.h>
#include <app_ui.h>
#ifdef CONFIG_GPS_MANAGER
#include <gps_manager.h>
#endif
#include "gps_ui.h"

#ifdef CONFIG_GPS_MANAGER
static gps_res_t _gps_view_res;
static gps_res_t* _gps_view_get_rate(void);
#endif

const gps_view_presenter_t gps_view_presenter = {
#ifdef CONFIG_GPS_MANAGER
	.get_gps_data = _gps_view_get_rate,
#endif
	.reserved = NULL,
};

#ifdef CONFIG_GPS_MANAGER
static gps_res_t* _gps_view_get_rate(void)
{
	return &_gps_view_res;
}
#endif

#ifdef CONFIG_GPS_MANAGER
static int _gps_res_callback(int evt_id, gps_res_t *res)
{
	SYS_LOG_INF("gps_res_t %d %d %d\n",res->gga_data.time.hours,
		res->gga_data.time.minutes,res->gga_data.time.seconds);

	memcpy(&_gps_view_res, res, sizeof(gps_res_t));
	ui_view_paint2(GPS_VIEW, 0, NULL);

	return 0;
}
#endif

void gps_ui_init(void)
{
#ifdef CONFIG_GPS_MANAGER
	gps_manager_add_callback(_gps_res_callback);
#endif
}

void gps_ui_enter(int anim_type)
{
	app_ui_stack_forward(GPS_UI, anim_type);
}
