/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_battery.h"

namespace gx {
BatteryInfo get_charge_state() { return RT_BATTERY_STATUS_NOT_CHARGING; }
int get_battery_level() { return 0; }
} // namespace gx
