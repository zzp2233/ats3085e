/*
 * Copyright (c) 2015, Wind River Systems, Inc.
 * Copyright (c) 2017, Oticon A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 通用C实现的MMIO访问：当前ATS Cortex-M构建由aarch32/arch.h选中。
 * 通过volatile指针访问硬件；没有锁、CPU屏障、缓存维护、读回确认或错误检查。 */

#ifndef ZEPHYR_INCLUDE_ARCH_COMMON_SYS_IO_H_
#define ZEPHYR_INCLUDE_ARCH_COMMON_SYS_IO_H_    /* 防止内联定义重复展开。 */

#ifndef _ASMLANGUAGE

#include <toolchain.h>
#include <zephyr/types.h>
#include <sys/sys_io.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  从指定MMIO地址读取8位原始寄存器值。
  * @param  addr：[输入] 有效可读MMIO地址，按1字节对齐，访问可能触发硬件读副作用。
  * @retval 寄存器的8位读数；没有独立错误码。
  * @note   volatile保留C层访问，不保证总线已完成，也不提供其他内存访问的屏障或互斥。
  */
static ALWAYS_INLINE uint8_t sys_read8(mem_addr_t addr)
{
	return *(volatile uint8_t *)addr;    /* 用8位volatile指针读取目标寄存器，不缓存前次读数。 */
}

/**
  * @brief  向指定MMIO地址写入8位原始数值。
  * @param  data：[输入] 要写入的8位值，字段/清除语义由外设决定。
  * @param  addr：[输入] 有效可写MMIO地址，按1字节对齐。
  * @retval 无；不会检测写入失败。
  * @note   写后不读回确认，不提供外部同步；需要时由调用者按硬件手册添加屏障/读回等步骤。
  */
static ALWAYS_INLINE void sys_write8(uint8_t data, mem_addr_t addr)
{
	*(volatile uint8_t *)addr = data;    /* 按8位访问宽度写硬件地址，保留寄存器自身的写入语义。 */
}

/**
  * @brief  从指定MMIO地址读取16位原始寄存器值。
  * @param  addr：[输入] 有效可读MMIO地址，按2字节对齐，访问可能触发硬件读副作用。
  * @retval 寄存器的16位读数；没有独立错误码。
  * @note   volatile保留C层访问，不保证总线已完成，也不提供其他内存访问的屏障或互斥。
  */
static ALWAYS_INLINE uint16_t sys_read16(mem_addr_t addr)
{
	return *(volatile uint16_t *)addr;    /* 用16位volatile指针读取目标寄存器，不缓存前次读数。 */
}

/**
  * @brief  向指定MMIO地址写入16位原始数值。
  * @param  data：[输入] 要写入的16位值，字段/清除语义由外设决定。
  * @param  addr：[输入] 有效可写MMIO地址，按2字节对齐。
  * @retval 无；不会检测写入失败。
  * @note   写后不读回确认，不提供外部同步；需要时由调用者按硬件手册添加屏障/读回等步骤。
  */
static ALWAYS_INLINE void sys_write16(uint16_t data, mem_addr_t addr)
{
	*(volatile uint16_t *)addr = data;    /* 按16位访问宽度写硬件地址，保留寄存器自身的写入语义。 */
}

/**
  * @brief  从指定MMIO地址读取32位原始寄存器值。
  * @param  addr：[输入] 有效可读MMIO地址，按4字节对齐，访问可能触发硬件读副作用。
  * @retval 寄存器的32位读数；没有独立错误码。
  * @note   volatile保留C层访问，不保证总线已完成，也不提供其他内存访问的屏障或互斥。
  */
static ALWAYS_INLINE uint32_t sys_read32(mem_addr_t addr)
{
	return *(volatile uint32_t *)addr;    /* 用32位volatile指针读取目标寄存器，不缓存前次读数。 */
}

/**
  * @brief  向指定MMIO地址写入32位原始数值。
  * @param  data：[输入] 要写入的32位值，字段/清除语义由外设决定。
  * @param  addr：[输入] 有效可写MMIO地址，按4字节对齐。
  * @retval 无；不会检测写入失败。
  * @note   写后不读回确认，不提供外部同步；需要时由调用者按硬件手册添加屏障/读回等步骤。
  */
static ALWAYS_INLINE void sys_write32(uint32_t data, mem_addr_t addr)
{
	*(volatile uint32_t *)addr = data;    /* 按32位访问宽度写硬件地址，保留寄存器自身的写入语义。 */
}

#ifdef __cplusplus
}
#endif

#endif    /* _ASMLANGUAGE */

#endif    /* ZEPHYR_INCLUDE_ARCH_COMMON_SYS_IO_H_ */
