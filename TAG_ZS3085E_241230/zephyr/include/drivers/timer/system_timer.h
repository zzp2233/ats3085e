/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 系统定时器驱动与内核之间的接口。
 *
 * 驱动提供硬件计数/下一事件，内核接收tick推进；接口声明不代表所有驱动均完整实现可选动作。
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SYSTEM_TIMER_H_
#define ZEPHYR_INCLUDE_DRIVERS_SYSTEM_TIMER_H_    /* 避免重复展开接口声明。 */

#include <stdbool.h>
#include <device.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 系统时钟接口
 * @defgroup clock_apis 系统时钟接口
 * @{
 */

/**
  * @brief  初始化系统时钟设备。
  * @param  dev：[输入] 启动框架提供的时钟设备指针，具体可空性由驱动实现决定。
  * @retval 通常0成功，非零为驱动初始化错误；当前Actions实现固定0。
  * @note   是时钟设备初始化回调；框架可提供弱空实现，实际硬件行为以选中驱动为准。
  */
extern int sys_clock_driver_init(const struct device *dev);

/**
  * @brief  声明旧式系统时钟电源控制回调。
  * @param  dev：[输入] 有效时钟设备对象。
  * @param  state：[输入] 请求的旧式电源状态。
  * @retval 0通常表示完成，其他状态由对应实现约定。
  * @note   此声明沿用pm_device_state；当前Actions实际入口名为sys_clock_device_ctrl并接收pm_device_action，不能把本声明当作它的定义。
  */
extern int clock_device_ctrl(const struct device *dev,
			     enum pm_device_state state);

/**
  * @brief  通知驱动最迟需要报告下一次tick的时间预算。
  * @param  ticks：[输入] 相对tick数；0/负值通用契约表示尽快，K_TICKS_FOREVER表示不要求未来事件，INT_MAX可用于尽量远期且保留正确计时。
  * @param  idle：[输入] 设置后是否即将空闲的提示。
  * @retval 无。
  * @note   允许驱动提前或多次报告；正常预算应在到期前一个tick内上报。SMP可从任意CPU调用，驱动须避免重复报告同一tick。具体Actions驱动对负值的处理有限，见其实现。
  */
extern void sys_clock_set_timeout(int32_t ticks, bool idle);

/**
  * @brief  通知定时器驱动系统正在退出空闲。
  * @param  无。
  * @retval 无。
  * @note   驱动可据此恢复计数与更新内部状态；旧驱动可补报tick，当前Actions实现为空。
  */
extern void sys_clock_idle_exit(void);

/**
  * @brief  向内核报告自上次报告后经过的完整tick。
  * @param  ticks：[输入] 新经过的tick数，不是硬件cycle；首次调用相对系统启动。
  * @retval 无。
  * @note   通常从定时器中断调用，尽可能接近tick边界；内核在此推进超时队列并执行到期回调，驱动须确保上报串行。
  */
extern void sys_clock_announce(int32_t ticks);

/**
  * @brief  查询自最近一次报告后已经经过但尚未报告的完整tick。
  * @param  无。
  * @retval 未报告的完整tick数；周期驱动可返回0。
  * @note   内核按接口约定提供适当同步，驱动返回瞬时读数；Actions实现另用自身锁保护累计值。
  */
extern uint32_t sys_clock_elapsed(void);
/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_DRIVERS_SYSTEM_TIMER_H_ */
