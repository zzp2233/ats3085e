/*
 * Copyright (c) 2019 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief system monkey test.
 */
#include <os_common_api.h>
#include <mem_manager.h>
#include <msg_manager.h>
#include <stdio.h>
#include <string.h>
#include <shell/shell.h>
#include <stdlib.h>
#include "system_app.h"
#include <drivers/input/input_dev.h>
#include <drivers/power_supply.h>
#include <random/rand32.h>
#include <soc.h>
#include <app_ui.h>

#define CONFIG_MONKEY_WORK_Q_STACK_SIZE 1280
#define INPUT_EVENT_INTERVAL 			10
#define MAX_POINTER_TRAVEL 				10
#define STEP_LENGTH                     20
/** onoff key short up*/
#define DEFAULT_KEY_EVENT 				0x4000001

K_THREAD_STACK_DEFINE(monkey_work_q_stack, CONFIG_MONKEY_WORK_Q_STACK_SIZE);

struct monkey_context_t {
	uint16_t monkey_start:1;
	uint16_t monkey_event_finished:1;
	uint16_t monkey_event_step;
	short x_res;
	short y_res;
	uint16_t monkey_event;
	uint16_t monkey_event_interval;
	struct k_work_q monkey_work_q;
	struct k_delayed_work monkey_work;
} monkey_context;

enum MONKEY_TYPE {
	KEY_EVENT = 0,
	DRAG_DOWN,
	DRAG_UP,
	DRAG_LEFT,
	DRAG_RIGHT,
	CLICK,
	REMIND_EVENT,

	NUM_MONKEY_TYPES,
};

struct tp_pointer_t {
	short x;
	short y;
	uint16_t pressed;
};

extern int tpkey_put_point(struct input_value *value, uint32_t timestamp);

static uint32_t _hardware_randomizer_gen_rand(void)
{
	uint32_t trng_low, trng_high;

	se_trng_init();
	se_trng_process(&trng_low, &trng_high);
	se_trng_deinit();

	return trng_low;
}

static int get_tp_pointer(struct tp_pointer_t *tp_pointer, uint16_t event, uint16_t *event_step)
{
	int ret = 0;
	int e_step = *event_step;
	switch (event) {
	case DRAG_DOWN: {
		tp_pointer->x = monkey_context.x_res / 2;
		tp_pointer->y = 0 + e_step * STEP_LENGTH;
		tp_pointer->pressed =  ((e_step == MAX_POINTER_TRAVEL) ? 0 : 1);
		e_step++;
		break;
	}
	case DRAG_UP: {
		tp_pointer->x = monkey_context.x_res / 2;
		tp_pointer->y = monkey_context.y_res - *event_step * STEP_LENGTH;
		tp_pointer->pressed =  ((e_step == MAX_POINTER_TRAVEL) ? 0 : 1);
		e_step++;
		break;
	}

	case DRAG_LEFT: {
		tp_pointer->x = monkey_context.x_res - e_step * STEP_LENGTH;
		tp_pointer->y = monkey_context.y_res / 2;
		tp_pointer->pressed =  ((e_step == MAX_POINTER_TRAVEL) ? 0 : 1);
		e_step++;
		break;
	}

	case DRAG_RIGHT: {
		tp_pointer->x = e_step * STEP_LENGTH;
		tp_pointer->y = monkey_context.y_res / 2;
		tp_pointer->pressed =  ((e_step == MAX_POINTER_TRAVEL) ? 0 : 1);
		e_step++;
		break;
	}

	case CLICK: {
		tp_pointer->x = _hardware_randomizer_gen_rand() % monkey_context.x_res;
		tp_pointer->y = _hardware_randomizer_gen_rand() % monkey_context.y_res;
		tp_pointer->pressed =  ((*event_step == 1) ? 1 : 0);
		e_step++;
		if(e_step > 1) {
			ret = 1;
		}
		break;
	}
	}
	if(e_step > MAX_POINTER_TRAVEL) {
		ret = 1;
	}
	if (tp_pointer->x > monkey_context.x_res - 1) {
		tp_pointer->x = monkey_context.x_res - 1;
	}
	if (tp_pointer->x < 0) {
		tp_pointer->x = 0;
	}
	if (tp_pointer->y > monkey_context.y_res - 1) {
		tp_pointer->y = monkey_context.y_res - 1;
	}
	if (tp_pointer->y < 0) {
		tp_pointer->y = 0;
	}
	//os_printk("(%d %d pressed %d) event %d e_step %d ret %d \n", tp_pointer->x, tp_pointer->y, tp_pointer->pressed,event,e_step,ret);
	*event_step = e_step;
	return ret;
}

static int send_remind(uint16_t *event_step)
{
	struct app_msg msg = { 0 };

	/* simulate lowpower event */
	if (*event_step == 0) {
		SYS_LOG_INF("monkey remind");

		msg.type = MSG_SYS_EVENT;
		msg.cmd = SYS_EVENT_BATTERY_LOW;
	} else {
		msg.type = MSG_BAT_CHARGE_EVENT;
		msg.cmd = BAT_CHG_EVENT_DC5V_IN;
	}

	*event_step += 1;

	send_async_msg(APP_ID_LAUNCHER, &msg);

	return (*event_step == 1) ? 0 : 1;
}

static void _monkey_work(struct k_work *work)
{
	struct tp_pointer_t tp_pointer = {0};

	if (monkey_context.monkey_event_finished) {
		os_sleep(monkey_context.monkey_event_interval);
		monkey_context.monkey_event = _hardware_randomizer_gen_rand() % NUM_MONKEY_TYPES;
		monkey_context.monkey_event_step = 0;
		monkey_context.monkey_event_finished = 0;
		//os_printk("new monkey event %d \n",monkey_context.monkey_event);
	}

	if (monkey_context.monkey_event == KEY_EVENT) {
		//os_printk("key event send\n");
		sys_event_report_input(DEFAULT_KEY_EVENT);
		monkey_context.monkey_event_finished = 1;
	} else if (monkey_context.monkey_event == REMIND_EVENT) {
		if (send_remind(&monkey_context.monkey_event_step)) {
			monkey_context.monkey_event_finished = 1;
		}
	} else {
		if (get_tp_pointer(&tp_pointer, monkey_context.monkey_event, &monkey_context.monkey_event_step)) {
			monkey_context.monkey_event_finished = 1;
		}

		struct input_value val;
		val.point.loc_x = tp_pointer.x;
		val.point.loc_y = tp_pointer.y;
		val.point.pessure_value = tp_pointer.pressed;
		val.point.gesture = monkey_context.monkey_event;
		tpkey_put_point(&val, k_cycle_get_32());
	}

	if (monkey_context.monkey_start) {
		k_delayed_work_submit_to_queue(&monkey_context.monkey_work_q, &monkey_context.monkey_work, Z_TIMEOUT_MS(INPUT_EVENT_INTERVAL));
	}
}

int system_monkey_test_start(uint16_t interval)
{
	if (!monkey_context.monkey_start) {
		k_delayed_work_submit_to_queue(&monkey_context.monkey_work_q,&monkey_context.monkey_work, Z_TIMEOUT_MS(INPUT_EVENT_INTERVAL));
		monkey_context.monkey_start = 1;
		monkey_context.monkey_event_interval = interval;
		os_printk("monkey start ...\n");
	} else {
		os_printk("monkey already start ...\n");
	}
	return 0;
}

int system_monkey_test_stop(void)
{
	if (monkey_context.monkey_start) {
		k_delayed_work_cancel(&monkey_context.monkey_work);
		monkey_context.monkey_start = 0;
		os_printk("monkey stop ...\n");
	} else {
		os_printk("monkey not start ...\n");
	}
	return 0;
}

int system_monkey_init(const struct device * dev)
{
	ARG_UNUSED(dev);
	k_work_queue_start(&monkey_context.monkey_work_q,
			monkey_work_q_stack, K_THREAD_STACK_SIZEOF(monkey_work_q_stack), 2, NULL);

	k_delayed_work_init(&monkey_context.monkey_work, _monkey_work);

	monkey_context.x_res = DEF_UI_WIDTH;
	monkey_context.y_res = DEF_UI_HEIGHT;
	monkey_context.monkey_start = 0;
	monkey_context.monkey_event_finished = 1;
	return 0;
}

SYS_INIT(system_monkey_init, APPLICATION, 5);

