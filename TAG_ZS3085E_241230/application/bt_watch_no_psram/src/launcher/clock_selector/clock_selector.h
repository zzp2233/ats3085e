/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file clock_selector.h
 */

#ifndef SRC_LAUNCHER_CLOCK_SELECTOR_H_
#define SRC_LAUNCHER_CLOCK_SELECTOR_H_

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>

/**********************
 *      TYPEDEFS
 **********************/
typedef struct clock_dsc {
	const char *name;
	uint32_t scene; /* resource scene id */
	uint16_t period; /* time update period */
} clock_dsc_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
/**
 * Initial clocks, including active clock and aod clock.
 */
void clocksel_init_clocks(void);

/**
 * Get current clock ID
 */
uint8_t clocksel_get_current_id(void);

/**
 * Get clock descriptor structure of specific clock ID
 */
const clock_dsc_t * clocksel_get_clock_dsc(uint8_t id);

/**
 * Get AOD clock descriptor structure
 */
const clock_dsc_t * clocksel_get_aod_clock_dsc(void);

/**
 * Enter clock selector UI
 */
int clocksel_ui_enter(void);

#endif /* SRC_LAUNCHER_CLOCK_SELECTOR_H_ */
