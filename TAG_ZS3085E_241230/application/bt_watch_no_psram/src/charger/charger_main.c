/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file system app main
 */
#include <mem_manager.h>
#include <msg_manager.h>
#include <fw_version.h>
#include <sys_event.h>
#include "app_switch.h"
#include "app_ui.h"
#include <hotplug_manager.h>
#include <input_manager.h>
#include <thread_timer.h>
#include <stream.h>
#include <property_manager.h>
#include <usb/usb_device.h>
#include <usb/class/usb_msc.h>
#include <soc.h>
#include <ui_manager.h>
#include <sys_wakelock.h>
#ifdef CONFIG_PLAYTTS
#include "tts_manager.h"
#endif

#ifdef CONFIG_TOOL
#include "tool_app.h"
#endif
#include "charger.h"

#include <power_manager.h>
#ifndef CONFIG_SIMULATOR
#include <drivers/power_supply.h>
#endif

#define CHARGER_TIMER_PERIOD 1000

#define MINICHARGE_VOLT      (3600*1000)

static bool terminaltion = false;
struct thread_timer charger_timer;

static void exit_charger_mode(void)
{
	terminaltion = true;
}

static void charger_timer_handle(struct thread_timer *ttimer, void *expiry_fn_arg)
{
	//SYS_LOG_INF("charger_timer_handle in\n");

    int vol = power_manager_get_battery_vol();
    SYS_LOG_INF("mini charger get vol: %u uv.\n", vol);
	if(vol > MINICHARGE_VOLT) {
	    SYS_LOG_INF("mini charger vol: %u uv > %u uv, exit mini charger. \n", vol, MINICHARGE_VOLT);
		exit_charger_mode();
	}
}

static void charger_suspend(void)
{
	SYS_LOG_INF("mini charger suspend\n");
    power_manger_set_init_charge_consume_ma(0);
}

static void charger_resume(void)
{
	SYS_LOG_INF("mini charger resume\n");
    power_manger_set_init_charge_consume_ma(1);
}

int charger_mode_check(void)
{
#ifdef CONFIG_ACTS_LEOPARD_BATTERY_SUPPLY_INTERNAL
	struct app_msg msg = {0};
	int result = 0;
	
    int vol = power_manager_get_battery_vol();
    SYS_LOG_INF("mini charger get vol: %u uv.\n", vol);
	if(vol > MINICHARGE_VOLT) {
	    SYS_LOG_INF("mini charger vol: %u uv > %u uv, exit mini charger. \n", vol, MINICHARGE_VOLT);
        return 0;
    }

	if (!soc_pmu_get_dc5v_status()) {
		sys_pm_poweroff();
	}

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_unlock(FULL_WAKE_LOCK);
#endif

    charger_resume();
	charger_view_init();
	thread_timer_init(&charger_timer, charger_timer_handle, NULL);
	thread_timer_start(&charger_timer, CHARGER_TIMER_PERIOD, CHARGER_TIMER_PERIOD);
	
	while (!terminaltion) {
		if (receive_msg(&msg, thread_timer_next_timeout())) {
			switch (msg.type) {
            case MSG_KEY_INPUT:
				#ifdef CONFIG_SYS_WAKELOCK
					sys_wake_lock(FULL_WAKE_LOCK);
					sys_wake_unlock(FULL_WAKE_LOCK);
				#endif
				break;
			case MSG_EARLY_SUSPEND_APP:
				charger_suspend();
				break;
			case MSG_LATE_RESUME_APP:
				charger_resume();
				break;
		    case MSG_BAT_CHARGE_EVENT:
		    	if (msg.cmd == BAT_CHG_EVENT_DC5V_OUT) {
		            sys_pm_poweroff();
                }
                if (msg.cmd == BAT_CHG_EVENT_EXIT_MINI_CHARGE) {
                    SYS_LOG_INF("mini charger get exit mini charge msg...\n");
                    exit_charger_mode();
                }
                break;
			default:
				//SYS_LOG_ERR("error type: 0x%x! \n", msg.type);
				continue;
			}
			if (msg.callback != NULL)
				msg.callback(&msg, result, NULL);
		}
		thread_timer_handle_expired();
	}

	thread_timer_stop(&charger_timer);
	charger_view_deinit();

#ifdef CONFIG_SYS_WAKELOCK
	sys_wake_lock(FULL_WAKE_LOCK);
#endif

	SYS_LOG_INF("exit mini charger.\n");
#endif //CONFIG_ACTS_LEOPARD_BATTERY_SUPPLY_INTERNAL

	return 0;

}

