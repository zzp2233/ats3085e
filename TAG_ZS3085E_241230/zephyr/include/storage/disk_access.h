/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 磁盘访问层的公共 API。调用者用注册磁盘名和逻辑扇区寻址，访问层再选择实际驱动的操作表。
 */

/* 保护宏防止同一翻译单元重复引入公共声明。 */
#ifndef ZEPHYR_INCLUDE_STORAGE_DISK_ACCESS_H_
#define ZEPHYR_INCLUDE_STORAGE_DISK_ACCESS_H_

/**
 * @brief 磁盘访问层接口
 * @defgroup disk_access_interface 磁盘访问接口
 * @{
 */

/* 访问层将注册名映射到驱动实例，文件系统无需直接持有设备指针。 */
/* 驱动头提供 disk_info、disk_operations 及 DISK_IOCTL_*、DISK_STATUS_* 编码。 */
#include <drivers/disk.h>

/* C++ 调用方使用与 Zephyr 的 C 实现相同的链接名。 */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  按注册名称查找磁盘，执行后端初始化。
 * @param  pdrv 非空、以 NUL 结尾的磁盘名，本机 NOR 的名称为 NOR。
 * @retval 0 表示成功。找不到磁盘、操作表或初始化入口时返回 -EINVAL，后端的初始化错误原样返回。
 */
int disk_access_init(const char *pdrv);

/**
 * @brief  查询指定磁盘的后端状态。
 * @param  pdrv 非空、以 NUL 结尾的注册名称。
 * @retval 就绪为 DISK_STATUS_OK，其他介质状态由 DISK_STATUS_* 表示。名称未注册或缺少状态入口时返回 -EINVAL。
 */
int disk_access_status(const char *pdrv);

/**
 * @brief  从指定磁盘的逻辑扇区读取连续数据。
 * @param  pdrv 非空的注册磁盘名。
 * @param  data_buf 调用方可写缓冲，容量至少为请求扇区数乘以每扇区字节数。
 * @param  start_sector 以该磁盘起点为零的逻辑扇区号。
 * @param  num_sector 请求读取的扇区数。
 * @retval 0 表示后端报告成功，缺少磁盘或读取入口为 -EINVAL，其余状态由后端返回。
 * @note   分发层只检查磁盘及操作表，读区间和缓冲条件由调用方及后端处理。
 */
int disk_access_read(const char *pdrv, uint8_t *data_buf,
		     uint32_t start_sector, uint32_t num_sector);

/**
 * @brief  将连续的逻辑扇区交付给指定磁盘后端。
 * @param  pdrv 非空的注册磁盘名。
 * @param  data_buf 调用期间保持可读的输入数据。
 * @param  start_sector 起始逻辑扇区号，从零起算。
 * @param  num_sector 待写扇区数，缓冲至少覆盖对应字节数。
 * @retval 0 表示后端接受写入，缺少磁盘或写入口为 -EINVAL，其余状态由后端返回。
 * @note   后端可能保留脏缓存，返回成功和已写入非易失介质是不同阶段。
 */
int disk_access_write(const char *pdrv, const uint8_t *data_buf,
		      uint32_t start_sector, uint32_t num_sector);

/**
 * @brief  向指定磁盘发出容量查询、缓存同步或其他设备控制请求。
 * @param  pdrv 非空的注册磁盘名。
 * @param  cmd DISK_IOCTL_* 命令编号。
 * @param  buff 与命令约定匹配的参数或输出缓冲，无参数命令可传 NULL。
 * @retval 0 表示后端报告成功，找不到磁盘或回调时返回 -EINVAL，后端错误原样返回。
 */
int disk_access_ioctl(const char *pdrv, uint8_t cmd, void *buff);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_STORAGE_DISK_ACCESS_H_ */
