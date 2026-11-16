/* 端口I/O与内存映射寄存器接口契约；此头以@fn描述接口，实际内联定义来自选中的架构头。
 * 当前ATS Cortex-M采用arch/common/sys_io.h与sys_bitops.h；MMIO地址须已映射且按访问宽度对齐，
 * volatile访问不等于内存屏障，也不保证读改写原子性，外设读清零/写1清零语义需由调用者处理。 */

/*
 * Copyright (c) 2015 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_SYS_IO_H_
#define ZEPHYR_INCLUDE_SYS_SYS_IO_H_    /* 保护共享地址类型及接口文档。 */

#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t io_port_t;      /* 独立I/O端口编号；是否支持端口I/O由架构决定。 */
typedef uintptr_t mm_reg_t;      /* 可容纳寄存器地址的无符号整型，宽度与指针相同。 */
typedef uintptr_t mem_addr_t;    /* 内存地址值类型，不自动验证地址可访问性。 */

/* 独立端口I/O接口；Cortex-M通常使用MMIO，不因此声明就保证存在这些端口指令。 */

/**
  * @fn static inline void sys_out8(uint8_t data, io_port_t port);
  * @brief  向指定I/O端口写入8位数值。
  * @param  data：[输入] 待写入的8位值。
  * @param  port：[输入] 有效端口编号。
  * @retval 无。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline uint8_t sys_in8(io_port_t port);
  * @brief  从指定I/O端口读取8位数值。
  * @param  port：[输入] 有效端口编号。
  * @retval 读取的8位原始值；没有独立错误返回。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline void sys_out16(uint16_t data, io_port_t port);
  * @brief  向指定I/O端口写入16位数值。
  * @param  data：[输入] 待写入的16位值。
  * @param  port：[输入] 有效端口编号。
  * @retval 无。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline uint16_t sys_in16(io_port_t port);
  * @brief  从指定I/O端口读取16位数值。
  * @param  port：[输入] 有效端口编号。
  * @retval 读取的16位原始值；没有独立错误返回。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline void sys_out32(uint32_t data, io_port_t port);
  * @brief  向指定I/O端口写入32位数值。
  * @param  data：[输入] 待写入的32位值。
  * @param  port：[输入] 有效端口编号。
  * @retval 无。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline uint32_t sys_in32(io_port_t port);
  * @brief  从指定I/O端口读取32位数值。
  * @param  port：[输入] 有效端口编号。
  * @retval 读取的32位原始值；没有独立错误返回。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline void sys_io_set_bit(io_port_t port, unsigned int bit)
  * @brief  置1目标位。
  * @param  port：[输入] 有效端口编号。
  * @param  bit：[输入] 从0开始的端口位号，范围取决于架构。
  * @retval 无。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline void sys_io_clear_bit(io_port_t port, unsigned int bit)
  * @brief  清零目标位。
  * @param  port：[输入] 有效端口编号。
  * @param  bit：[输入] 从0开始的端口位号，范围取决于架构。
  * @retval 无。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline int sys_io_test_bit(io_port_t port, unsigned int bit)
  * @brief  检查目标位是否置位。
  * @param  port：[输入] 有效端口编号。
  * @param  bit：[输入] 从0开始的端口位号，范围取决于架构。
  * @retval 目标位的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline int sys_io_test_and_set_bit(io_port_t port, unsigned int bit)
  * @brief  读取目标位旧值并置1目标位。
  * @param  port：[输入] 有效端口编号。
  * @param  bit：[输入] 从0开始的端口位号，范围取决于架构。
  * @retval 目标位修改前的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   测试后修改由多次访问组成，不保证原子性；与中断或其他CPU并发时须在外部同步。
  */


/**
  * @fn static inline int sys_io_test_and_clear_bit(io_port_t port, unsigned int bit)
  * @brief  读取目标位旧值并清零目标位。
  * @param  port：[输入] 有效端口编号。
  * @param  bit：[输入] 从0开始的端口位号，范围取决于架构。
  * @retval 目标位修改前的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   测试后修改由多次访问组成，不保证原子性；与中断或其他CPU并发时须在外部同步。
  */


/* 内存映射寄存器读写接口；一次C访问的实际总线事务数由架构/宽度决定。 */

/**
  * @fn static inline void sys_write8(uint8_t data, mm_reg_t addr);
  * @brief  向指定内存映射寄存器写入8位数值。
  * @param  data：[输入] 待写入的8位值。
  * @param  addr：[输入] 有效寄存器地址，按1字节对齐并可写。
  * @retval 无。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline uint8_t sys_read8(mm_reg_t addr);
  * @brief  从指定内存映射寄存器读取8位数值。
  * @param  addr：[输入] 有效寄存器地址，按1字节对齐并可读。
  * @retval 读取的8位原始值；没有独立错误返回。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline void sys_write16(uint16_t data, mm_reg_t addr);
  * @brief  向指定内存映射寄存器写入16位数值。
  * @param  data：[输入] 待写入的16位值。
  * @param  addr：[输入] 有效寄存器地址，按2字节对齐并可写。
  * @retval 无。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline uint16_t sys_read16(mm_reg_t addr);
  * @brief  从指定内存映射寄存器读取16位数值。
  * @param  addr：[输入] 有效寄存器地址，按2字节对齐并可读。
  * @retval 读取的16位原始值；没有独立错误返回。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline void sys_write32(uint32_t data, mm_reg_t addr);
  * @brief  向指定内存映射寄存器写入32位数值。
  * @param  data：[输入] 待写入的32位值。
  * @param  addr：[输入] 有效寄存器地址，按4字节对齐并可写。
  * @retval 无。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline uint32_t sys_read32(mm_reg_t addr);
  * @brief  从指定内存映射寄存器读取32位数值。
  * @param  addr：[输入] 有效寄存器地址，按4字节对齐并可读。
  * @retval 读取的32位原始值；没有独立错误返回。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。
  */


/**
  * @fn static inline void sys_write64(uint64_t data, mm_reg_t addr);
  * @brief  向指定内存映射寄存器写入64位数值。
  * @param  data：[输入] 待写入的64位值。
  * @param  addr：[输入] 有效寄存器地址，按8字节对齐并可写。
  * @retval 无。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。 当前ATS通用MMIO头未提供64位实现，不能据此文档假定可用。
  */


/**
  * @fn static inline uint64_t sys_read64(mm_reg_t addr);
  * @brief  从指定内存映射寄存器读取64位数值。
  * @param  addr：[输入] 有效寄存器地址，按8字节对齐并可读。
  * @retval 读取的64位原始值；没有独立错误返回。
  * @note   调用方确保地址有效及访问权限；接口本身不检查地址。 当前ATS通用MMIO头未提供64位实现。
  */


/* 单字/位数组操作；普通读改写不具备跨中断或跨CPU的原子性。 */

/**
  * @fn static inline void sys_set_bit(mem_addr_t addr, unsigned int bit)
  * @brief  置1目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 32位字内的位号0～31。
  * @retval 无。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline void sys_clear_bit(mem_addr_t addr, unsigned int bit)
  * @brief  清零目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 32位字内的位号0～31。
  * @retval 无。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline int sys_test_bit(mem_addr_t addr, unsigned int bit)
  * @brief  检查目标位是否置位。
  * @param  addr：[输入] 有效的32位对齐可读地址；位数组接口以此为首地址。
  * @param  bit：[输入] 32位字内的位号0～31。
  * @retval 目标位的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline int sys_test_and_set_bit(mem_addr_t addr, unsigned int bit)
  * @brief  读取目标位旧值并置1目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 32位字内的位号0～31。
  * @retval 目标位修改前的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   测试后修改由多次访问组成，不保证原子性；与中断或其他CPU并发时须在外部同步。
  */


/**
  * @fn static inline int sys_test_and_clear_bit(mem_addr_t addr, unsigned int bit)
  * @brief  读取目标位旧值并清零目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 32位字内的位号0～31。
  * @retval 目标位修改前的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   测试后修改由多次访问组成，不保证原子性；与中断或其他CPU并发时须在外部同步。
  */


/**
  * @fn static inline void sys_bitfield_set_bit(mem_addr_t addr, unsigned int bit)
  * @brief  置1目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 从0开始的位数组索引，地址覆盖范围须足够。
  * @retval 无。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline void sys_bitfield_clear_bit(mem_addr_t addr, unsigned int bit)
  * @brief  清零目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 从0开始的位数组索引，地址覆盖范围须足够。
  * @retval 无。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline int sys_bitfield_test_bit(mem_addr_t addr, unsigned int bit)
  * @brief  检查目标位是否置位。
  * @param  addr：[输入] 有效的32位对齐可读地址；位数组接口以此为首地址。
  * @param  bit：[输入] 从0开始的位数组索引，地址覆盖范围须足够。
  * @retval 目标位的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   修改接口采用普通读改写，测试接口返回瞬时值；MMIO的副作用与并发须由调用者处理。
  */


/**
  * @fn static inline int sys_bitfield_test_and_set_bit(mem_addr_t addr, unsigned int bit)
  * @brief  读取目标位旧值并置1目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 从0开始的位数组索引，地址覆盖范围须足够。
  * @retval 目标位修改前的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   测试后修改由多次访问组成，不保证原子性；与中断或其他CPU并发时须在外部同步。
  */


/**
  * @fn static inline int sys_bitfield_test_and_clear_bit(mem_addr_t addr, unsigned int bit)
  * @brief  读取目标位旧值并清零目标位。
  * @param  addr：[输入] 有效的32位对齐可读写地址；位数组接口以此为首地址。
  * @param  bit：[输入] 从0开始的位数组索引，地址覆盖范围须足够。
  * @retval 目标位修改前的状态；0为未置位，非0为置位。当前通用内存实现返回位掩码，不能假定非0恒为1。
  * @note   测试后修改由多次访问组成，不保证原子性；与中断或其他CPU并发时须在外部同步。
  */



#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_SYS_SYS_IO_H_ */
