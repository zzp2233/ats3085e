/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

namespace gx {
enum BatteryInfo {
    RT_BATTERY_STATUS_CHARGING,
    RT_BATTERY_STATUS_DISCHARGING,
    RT_BATTERY_STATUS_NOT_CHARGING,
    RT_BATTERY_STATUS_FULL,
};
/* 获取当前电池的状态 */
BatteryInfo get_charge_state();
/* 获取当前当前电池的电量，取值范围 0 ~ 100 */
int get_battery_level();
} // namespace gx
