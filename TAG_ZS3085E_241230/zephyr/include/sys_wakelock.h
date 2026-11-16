/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 系统唤醒锁公共接口；包含保护避免重复定义。stdint.h 和 stdbool.h 提供定宽整数及布尔类型，C++ 分支保留 C 链接规则。 */
#ifndef _SYS_WAKELOCK_H
#define _SYS_WAKELOCK_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{

	PARTIAL_WAKE_LOCK,    /* 部分唤醒策略：CPU 保持运行，显示和触摸仍可关闭。 */

	FULL_WAKE_LOCK,    /* 完整唤醒策略：用于保持系统完整活动，具体屏幕行为由待机管理策略决定。 */

	MAX_WAKE_LOCK_TYPE,    /* 唤醒锁类型数量，不是有效锁类型。 */
};

enum
{
	BT_WAKE_LOCK_USER,         /* 蓝牙模块的引用计数槽。 */
	MEDIA_WAKE_LOCK_USER,      /* 媒体模块的引用计数槽。 */
	USB_WAKE_LOCK_USER,        /* USB 模块的引用计数槽。 */
	POWER_WAKE_LOCK_USER,      /* 电源模块的引用计数槽。 */
	SYS_WAKE_LOCK_USER,        /* 系统模块的引用计数槽。 */
	APP_WAKE_LOCK_USER,        /* 默认 sys_wake_lock/unlock 使用的应用计数槽。 */
	DISPLAY_WAKE_LOCK_USER,    /* 显示模块的引用计数槽。 */
	MAX_WAKE_LOCK_USER,        /* 使用者槽数量，不是有效索引。 */
};

/**
  * @brief  以 APP_WAKE_LOCK_USER 身份增加指定唤醒锁引用。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 0：成功；-EEXIST：类型未找到。
  * @note   与 sys_wake_unlock 配对；持锁阻止相应待机策略，不是对 SPI 总线的互斥锁。
  */
int sys_wake_lock(int wake_lock_type);

/**
  * @brief  在中断临界区内增加指定类型和使用者的唤醒锁计数。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @param  wake_lock_user：[输入]0～MAX_WAKE_LOCK_USER-1 的使用者索引，本实现不检查越界。
  * @retval 0：成功；-EEXIST：类型未找到。
  * @note   总引用计数须小于 0xFFFF，否则断言；持锁后清零释放时间戳。
  */
int sys_wake_lock_ext(int wake_lock_type, int wake_lock_user);

/**
  * @brief  以 APP_WAKE_LOCK_USER 身份减少指定唤醒锁引用。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 0：成功；-ESRCH：类型未找到。
  * @note   须与本使用者的加锁匹配；不是强制把所有使用者引用清零。
  */
int sys_wake_unlock(int wake_lock_type);

/**
  * @brief  减少使用者及总引用计数，更新时间戳并在全部释放后唤醒等待者。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @param  wake_lock_user：[输入]0～MAX_WAKE_LOCK_USER-1 的使用者索引，本实现不检查越界。
  * @retval 0：成功；-ESRCH：类型未找到。
  * @note   总引用必须大于零；本实现未单独验证使用者计数。FULL 类型还按配置清除快速待机状态，内部用中断锁保护计数。
  */
int sys_wake_unlock_ext(int wake_lock_type, int wake_lock_user);

/**
  * @brief  读取指定类型当前总引用计数。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 非负引用计数；管理禁用或类型未找到时为 0。
  * @note   使用中断临界区保护读取；0 可能是禁用，不必然证明所有对象原始引用都为零。
  */
int sys_wakelocks_check(int wake_lock_type);

/**
  * @brief  计算指定类型已无引用的持续时间。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 无引用时返回毫秒差加 1；仍持锁或类型不存在返回 0；管理禁用返回 0xFFFFFFFF。
  * @note   使用 k_uptime_get_32 的毫秒时间；无符号减法按 32 位回绕。不是 OSA 的 5 ms tick。
  */
uint32_t sys_wakelocks_get_free_time(int wake_lock_type);

/**
  * @brief  等待全局唤醒锁通知信号量。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 取得信号量后固定返回 0。
  * @note   参数当前未参与选择；所有类型共享同一信号量，K_FOREVER 可永久阻塞，不可在中断中等待。
  */
int sys_wakelocks_wait(int wake_lock_type);

/**
  * @brief  给出一次全局唤醒锁通知许可。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 固定返回 0。
  * @note   参数当前未参与选择；共享容量 1 的信号量，不是每个类型独立通知队列。
  */
int sys_wakelocks_wake(int wake_lock_type);

/**
  * @brief  打印全部已支持类型及各使用者引用，供诊断未释放的唤醒锁。
  * @param  无。
  * @retval 无。
  * @note   诊断打印没有整体快照锁，不能保证多字段同时刻一致。
  */
void sys_wakelocks_dump(void);

/**
  * @brief  设置唤醒锁查询机制的使能状态。
  * @param  enable：[输入]0 禁用，非零启用。
  * @retval 无。
  * @note   只更新管理使能标志，不清除现存引用计数。
  */
void sys_wakelocks_enable(int enable);

#ifdef __cplusplus
}
#endif

#endif

