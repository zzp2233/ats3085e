/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_battery.h"
#include <power_manager.h>
#include <drivers/power_supply.h>

namespace gx {

BatteryInfo get_charge_state() {
	int status = power_manager_get_charge_status();
	if (status == POWER_SUPPLY_STATUS_BAT_NOTEXIST) {
		return RT_BATTERY_STATUS_NOT_CHARGING;
	} else if (status == POWER_SUPPLY_STATUS_DISCHARGE) {
		return RT_BATTERY_STATUS_DISCHARGING;
	} else if (status == POWER_SUPPLY_STATUS_CHARGING) {
		return RT_BATTERY_STATUS_CHARGING;
	} else if (status == POWER_SUPPLY_STATUS_FULL) {
		return RT_BATTERY_STATUS_FULL;
	}

	return RT_BATTERY_STATUS_NOT_CHARGING;
}

int get_battery_level() {
	return power_manager_get_battery_capacity();
}

} // namespace gx
