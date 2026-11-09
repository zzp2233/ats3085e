/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*********************
 *      INCLUDES
 *********************/
#include <os_common_api.h>
#include "launcher_app.h"
#include "clock_selector.h"
#include "clock_selector_ui.h"
#ifdef CONFIG_PROPERTY
#  include <property_manager.h>
#endif
#ifndef CONFIG_SIMULATOR
#  include <soc.h>
#endif

/**********************
 *   STATIC PROTOTYPES
 **********************/
static const char *_clock_selector_get_clock_name(uint8_t id);
static void _clock_selector_set_clock_id(uint8_t id);

/**********************
 *  GLOBAL VARIABLES
 **********************/
const clock_selector_ui_presenter_t g_clocksel_ui_presenter = {
	.get_clock_id = clocksel_get_current_id,
	.get_clock_name = _clock_selector_get_clock_name,
	.set_clock_id = _clock_selector_set_clock_id,
};

/**********************
 *  STATIC VARIABLES
 **********************/
static const clock_dsc_t clock_dsc[] = {
	{ "analog", SCENE_CLOCK_VIEW, 100 },
	{ "digital", SCENE_DIGITAL_CLOCK_VIEW, 1000 },
#ifdef SCENE_AOD_CLOCK_VIEW
	{ "aod", SCENE_AOD_CLOCK_VIEW, 60000 },
#endif
};

static uint8_t g_clock_id = 0;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void clocksel_init_clocks(void)
{
#ifdef CONFIG_PROPERTY
	g_clock_id = (uint8_t)property_get_int(CFG_CLOCK_ID, 0);
	if (g_clock_id  >= ARRAY_SIZE(clock_dsc))
		g_clock_id = 0;

	if (clocksel_get_aod_clock_dsc() != NULL)
		soc_set_aod_mode((uint8_t)property_get_int(CFG_AOD_MODE, 0));
#endif
}

uint8_t clocksel_get_current_id(void)
{
	return g_clock_id;
}

const clock_dsc_t * clocksel_get_clock_dsc(uint8_t id)
{
	if (id < ARRAY_SIZE(clock_dsc))
		return &clock_dsc[id];

	return NULL;
}

const clock_dsc_t * clocksel_get_aod_clock_dsc(void)
{
#ifdef SCENE_AOD_CLOCK_VIEW
	return &clock_dsc[ARRAY_SIZE(clock_dsc) - 1];
#else
	return NULL;
#endif
}

int clocksel_ui_enter(void)
{
	SYS_LOG_INF("clocksel view enter");
	return app_ui_stack_forward(CLOCKSEL_UI, APP_UI_LOAD_ANIM_NONE);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static const char *_clock_selector_get_clock_name(uint8_t id)
{
	return clock_dsc[id].name;
}

static void _clock_selector_set_clock_id(uint8_t id)
{
	SYS_LOG_INF("select clock %d", id);

	if (id != g_clock_id && id < ARRAY_SIZE(clock_dsc)) {
#ifdef CONFIG_PROPERTY
		property_set_int(CFG_CLOCK_ID, id);
		property_flush(CFG_CLOCK_ID);
#endif
		g_clock_id = id;
	}

	app_ui_stack_back(APP_UI_LOAD_ANIM_NONE);
}
