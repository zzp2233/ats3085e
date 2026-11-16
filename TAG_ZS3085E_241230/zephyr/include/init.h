/* 启动注册机制：SYS_INIT生成只读init_entry，并放入带阶段与优先级的链接段；kernel/device.c按链接器边界顺序执行。board.c在PRE_KERNEL_1优先级5注册，SoC启动优先级0先执行。包含保护和extern C使C/C++可共同引用。 */
/*
 * Copyright (c) 2015 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_INIT_H_
#define ZEPHYR_INCLUDE_INIT_H_

#include <toolchain.h>
#include <kernel.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动阶段：PRE_KERNEL_1/2在内核初始化上下文使用中断栈；之后的阶段在内核主线程执行。此顺序定义须与kernel/device.c阶段表一致。 */

#define _SYS_INIT_LEVEL_PRE_KERNEL_1	0    /* 启动阶段顺序索引；SMP开关会改变POWER_MAX索引。 */
#define _SYS_INIT_LEVEL_PRE_KERNEL_2	1    /* 启动阶段顺序索引；SMP开关会改变POWER_MAX索引。 */
#define _SYS_INIT_LEVEL_POST_KERNEL	2    /* 启动阶段顺序索引；SMP开关会改变POWER_MAX索引。 */
#define _SYS_INIT_LEVEL_APPLICATION	3    /* 启动阶段顺序索引；SMP开关会改变POWER_MAX索引。 */

#ifdef CONFIG_SMP
#define _SYS_INIT_LEVEL_SMP		4    /* 启动阶段顺序索引；SMP开关会改变POWER_MAX索引。 */
#define _SYS_INIT_LEVEL_POWER_MAX	5    /* 启动阶段顺序索引；SMP开关会改变POWER_MAX索引。 */
#else
#define _SYS_INIT_LEVEL_POWER_MAX	4    /* 启动阶段顺序索引；SMP开关会改变POWER_MAX索引。 */
#endif

struct device;

/** @brief 每个设备或系统服务的静态启动项；框架调用init(dev)，服务项允许dev为空。 */
struct init_entry {
	/** 初始化回调，由启动分派器在指定阶段调用；dev为同项设备或NULL服务参数。返回0表示成功，非0会记入设备初始化状态。 */
	int (*init)(const struct device *dev);
	/** 设备对象地址；普通SYS_INIT服务不关联设备，填NULL。指向静态对象，不转移所有权。 */
	const struct device *dev;
};

/**
  * @brief  按链接段顺序执行指定启动阶段的全部回调。
  * @param  level：[输入] _SYS_INIT_LEVEL_*有效索引，调用者保证范围，函数未检查越界。
  * @retval 无。
  * @note   设备回调结果存入设备state，服务项无设备状态可记录。
  */
void z_sys_init_run_level(int32_t level);

/* 使用__COUNTER__生成唯一符号，允许同一C文件以相同函数注册多个启动项。 */
#define Z_SYS_NAME(_init_fn) _CONCAT(_CONCAT(sys_init_, _init_fn), __COUNTER__)

/**
  * @brief  生成只读启动项并放入阶段/优先级对应的链接段。
  * @param  _entry_name：[输入] 生成项的唯一符号名，不是运行时字符串。
  * @param  _init_fn：[输入] int函数地址，接收const struct device*；成功返回0。
  * @param  _device：[输入] 静态设备对象或NULL服务项。
  * @param  _level：[输入] 启动阶段标识，参与段名字符串拼接。
  * @param  _prio：[输入] 同阶段优先级，0～99的十进制字面量或展开为字面量的宏。
  * @retval 无；此宏声明对象，不是函数调用。
  * @note   内部宏不可从用户态访问，驱动使用DEVICE_DEFINE，系统服务使用SYS_INIT；__used及section属性使链接器保留并排序项。
  */
#define Z_INIT_ENTRY_DEFINE(_entry_name, _init_fn, _device, _level, _prio)	\
	static const Z_DECL_ALIGN(struct init_entry)			\
		_CONCAT(__init_, _entry_name) __used			\
	__attribute__((__section__(".z_init_" #_level STRINGIFY(_prio)"_"))) = { \
		.init = (_init_fn),					\
		.dev = (_device),					\
	}

/**
  * @brief  将无设备对象的系统初始化函数注册到启动流程。
  * @param  _init_fn：[输入] int回调，接收const struct device*，此宏传NULL；成功返回0。
  * @param  _level：[输入] PRE_KERNEL_1、PRE_KERNEL_2、POST_KERNEL、APPLICATION或配置支持的SMP、POWER_MAX。
  * @param  _prio：[输入] 同阶段优先级0～99，越小越早；必须是无前导零/符号的十进制字面量或等价宏，不能写加法表达式。
  * @retval 无；展开为静态启动项定义。
  * @note   PRE_KERNEL_1只依赖SoC硬件，PRE_KERNEL_2可依赖前一阶段，两者不能依赖尚未建立的内核服务；POST_KERNEL及应用阶段可使用内核服务。SMP阶段仅多核配置存在。
  */
#define SYS_INIT(_init_fn, _level, _prio)					\
	Z_INIT_ENTRY_DEFINE(Z_SYS_NAME(_init_fn), _init_fn, NULL, _level, _prio)

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_INIT_H_ */
