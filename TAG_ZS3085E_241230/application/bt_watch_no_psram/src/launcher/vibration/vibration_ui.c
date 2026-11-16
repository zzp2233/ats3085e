/* 振动 UI 的呈现接口实现；SPI2 使用 GPIO57 后板级禁用振动设备，因此硬件调用前必须检查设备绑定。模拟器构建只维护 UI 档位，不访问振动硬件。 */
/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <os_common_api.h>
#include <app_ui.h>
#include "vibration_ui.h"

#ifndef CONFIG_SIMULATOR
#include "drivers/vibrator.h"
#endif

#define PWM_CHAN 1    /* 振动硬件调用使用的 PWM 通道号。 */

static uint8_t current_level = 0;    /* 当前档位从 LEVEL_1 的索引 0 开始。 */

/**
  * @brief  查找振动设备，并按当前档位启动 PWM 通道。
  * @param  无。
  * @retval 无。
  * @note   非模拟器分支访问硬件；设备未注册则返回。current_level 必须在 0～LEVEL_NUM-1，本函数未检查索引。
  */
static void vibration_start(void);

/**
  * @brief  查找振动设备并停止指定 PWM 通道。
  * @param  无。
  * @retval 无。
  * @note   设备禁用或绑定失败时直接返回；未传播驱动返回值。
  */
static void vibration_stop(void);

/**
  * @brief  更新缓存档位，并通过启动路径应用对应振动强度。
  * @param  level：[输入]档位索引，应为 LEVEL_1～LEVEL_3；本函数未检查越界。
  * @retval 无。
  * @note   相同档位不重复启动；新档位会调用 vibration_start，不仅仅保存设置。
  */
static void vibration_set_level(uint8_t level);

/**
  * @brief  读取当前缓存的振动档位。
  * @param  无。
  * @retval 当前档位索引。
  * @note   读取的是软件缓存，不能据此判断禁用的硬件是否在振动。
  */
static uint8_t vibration_get_level(void);

/**
  * @brief  在硬件存在时转交振动 PWM 频率设置。
  * @param  freq：[输入]传给 vibrat_set_freq_param 的频率值；UI 层不换算也不验证。
  * @retval 无。
  * @note   仅非模拟器分支访问驱动，设备缺失时返回；具体单位和合法范围应遵循驱动实现。
  */
static void vibration_set_freq(uint32_t freq);

static uint8_t vib_strength[LEVEL_NUM] = {
		40,    /* 第一档占空比为 40%，索引 LEVEL_1。 */
		60,    /* 第二档占空比为 60%，索引 LEVEL_2。 */
		90,    /* 第三档占空比为 90%，索引 LEVEL_3。 */
};

const vibration_view_presenter_t vibration_view_presenter = {
	.start = vibration_start,            /* 呈现层启动回调绑定到本文件硬件包装。 */
	.stop = vibration_stop,              /* 呈现层停止回调绑定到本文件硬件包装。 */
	.set_level = vibration_set_level,    /* 设置档位时同步缓存并应用振动强度。 */
	.get_level = vibration_get_level,    /* 获取当前缓存档位。 */
	.set_freq = vibration_set_freq       /* 频率设置转交本文件的可选硬件包装。 */
};

/**
  * @brief  查找振动设备，并按当前档位启动 PWM 通道。
  * @param  无。
  * @retval 无。
  * @note   非模拟器分支访问硬件；设备未注册则返回。current_level 必须在 0～LEVEL_NUM-1，本函数未检查索引。
  */
static void vibration_start(void)
{
#ifndef CONFIG_SIMULATOR
	const struct device *dev = device_get_binding(CONFIG_VIBRATOR_DEV_NAME);    /* 查找已注册且初始化成功的振动设备；SPI 释放引脚后的构建通常返回空。 */
    if (!dev) {                                                                     /* 设备未注册时提前退出，禁止解引用空设备指针。 */
        return;                                                                     /* 当前路径无需后续操作，返回调用者。 */
	}
	vibrat_start(dev, PWM_CHAN, vib_strength[current_level]);                   /* 通过驱动 API 按当前档位占空比启动 PWM。 */
#endif
	return;                                                                     /* 结束当前包装；硬件不存在或模拟器分支无需继续操作。 */
}

/**
  * @brief  查找振动设备并停止指定 PWM 通道。
  * @param  无。
  * @retval 无。
  * @note   设备禁用或绑定失败时直接返回；未传播驱动返回值。
  */
static void vibration_stop(void)
{
#ifndef CONFIG_SIMULATOR
	const struct device *dev = device_get_binding(CONFIG_VIBRATOR_DEV_NAME);    /* 查找已注册且初始化成功的振动设备；SPI 释放引脚后的构建通常返回空。 */
    if (!dev) {                                                                     /* 设备未注册时提前退出，禁止解引用空设备指针。 */
        return;                                                                     /* 当前路径无需后续操作，返回调用者。 */
	}
	vibrat_stop(dev, PWM_CHAN);                                                 /* 通过驱动 API 停止对应 PWM 通道。 */
#endif
	return;                                                                     /* 结束当前包装；硬件不存在或模拟器分支无需继续操作。 */
}

/**
  * @brief  更新缓存档位，并通过启动路径应用对应振动强度。
  * @param  level：[输入]档位索引，应为 LEVEL_1～LEVEL_3；本函数未检查越界。
  * @retval 无。
  * @note   相同档位不重复启动；新档位会调用 vibration_start，不仅仅保存设置。
  */
static void vibration_set_level(uint8_t level)
{
	if(current_level == level)    /* 档位未改变时避免重复启动硬件。 */
		return;               /* 结束当前包装；硬件不存在或模拟器分支无需继续操作。 */
	// 先保存新档位，再复用启动路径应用该档位的占空比。
	current_level = level;        /* 先更新缓存索引，后续启动据此读取占空比。 */
	vibration_start();            /* 复用含空设备保护的启动路径应用新强度。 */
	return;                       /* 结束当前包装；硬件不存在或模拟器分支无需继续操作。 */
}

/**
  * @brief  读取当前缓存的振动档位。
  * @param  无。
  * @retval 当前档位索引。
  * @note   读取的是软件缓存，不能据此判断禁用的硬件是否在振动。
  */
static uint8_t vibration_get_level(void)
{
	return current_level;    /* 返回软件档位索引。 */
}

/**
  * @brief  在硬件存在时转交振动 PWM 频率设置。
  * @param  freq：[输入]传给 vibrat_set_freq_param 的频率值；UI 层不换算也不验证。
  * @retval 无。
  * @note   仅非模拟器分支访问驱动，设备缺失时返回；具体单位和合法范围应遵循驱动实现。
  */
static void vibration_set_freq(unsigned int freq)
{
#ifndef CONFIG_SIMULATOR
	const struct device *dev = device_get_binding(CONFIG_VIBRATOR_DEV_NAME);    /* 查找已注册且初始化成功的振动设备；SPI 释放引脚后的构建通常返回空。 */
    if (!dev) {                                                                     /* 设备未注册时提前退出，禁止解引用空设备指针。 */
        return;                                                                     /* 当前路径无需后续操作，返回调用者。 */
	}
	vibrat_set_freq_param(dev, PWM_CHAN, freq);                                 /* 通过设备函数表转交频率参数，不在 UI 层换算。 */
#endif
	return;                                                                     /* 结束当前包装；硬件不存在或模拟器分支无需继续操作。 */
}

/**
  * @brief  通过 UI 栈进入振动设置界面。
  * @param  anim_type：[输入]界面栈使用的切换动画类型。
  * @retval 无。
  * @note   当前函数只转交界面导航，不配置 SPI 或振动硬件。
  */
void vibration_ui_enter(int anim_type)
{
	app_ui_stack_forward(VIB_UI, anim_type);    /* 把振动界面压入 UI 导航栈并应用动画类型。 */
}

