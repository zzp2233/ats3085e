/*
 * Copyright (c) 2016 Intel Corporation.
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 磁盘后端向文件系统等上层暴露的块设备契约。Flash、RAM 盘等介质均可注册同一种描述结构，按各自的操作表完成实际读写。
 */

/* 头文件保护避免重复定义磁盘结构和控制编号。 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_DISK_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISK_H_

/**
 * @brief 磁盘驱动操作表与注册接口
 * @defgroup disk_driver_interface 磁盘驱动接口
 * @{
 */

/* 文件系统及块设备之间通过同一驱动接口分发请求。 */
/* 内核与定宽整数类型用于回调参数，双向链表节点用于运行期注册表。 */
#include <kernel.h>
#include <zephyr/types.h>
#include <sys/dlist.h>

/* 为 C++ 调用者保留 C 链接名。 */
#ifdef __cplusplus
extern "C" {
#endif

/* ioctl 的命令号由公共层传到选定后端；每个后端只实现自己支持的请求。 */

/* 取可寻址的逻辑扇区数。 */
#define DISK_IOCTL_GET_SECTOR_COUNT		1
/* 取单个逻辑扇区的字节数。 */
#define DISK_IOCTL_GET_SECTOR_SIZE		2
/* 查询整盘字节数，编号 3 在当前头中仍然有效；并非所有后端均实现。 */
#define DISK_IOCTL_GET_DISK_SIZE		3
/* 查询一个物理擦除块所占的扇区数。NOR 后端该命令实际写回 NOR_SECTOR_SIZE，调用时见其实现。 */
#define DISK_IOCTL_GET_ERASE_BLOCK_SZ		4
/* 将后端缓存提交到介质。 */
#define DISK_IOCTL_CTRL_SYNC			5

/* 检测介质是否存在，以后端约定填写返回缓冲。 */
#define DISK_IOCTL_HW_DETECT			10

/* 请求存储设备进入高速模式。 */
#define DISK_IOCTL_ENTER_HIGH_SPEED		11
/* 请求设备退出高速模式。 */
#define DISK_IOCTL_EXIT_HIGH_SPEED		12

/* 请求后端执行周期性热插拔检测。 */
#define DISK_IOCTL_HOTPLUG_PERIOD_DETECT	13

/* 状态查询的位掩码。无故障时为零，其他位分别描述未初始化、无介质和写保护。 */

/* 介质已就绪。 */
#define DISK_STATUS_OK			0x00
/* 驱动尚未完成初始化。 */
#define DISK_STATUS_UNINIT		0x01
/* 设备没有可用介质。 */
#define DISK_STATUS_NOMEDIA		0x02
/* 介质处于写保护状态。 */
#define DISK_STATUS_WR_PROTECT		0x04

/* 前置声明使磁盘实例可以持有稍后定义的操作表指针。 */
struct disk_operations;

/* 每个磁盘实例把名称、逻辑扇区范围、实际后端和操作函数集中到一个持久对象中。 */
struct disk_info {
	/* 注册表嵌入式链表节点，访问层可由 node 还原整个实例。 */
	sys_dnode_t node;
	/* 以 NUL 结束的注册名，用于精确匹配。 */
	char *name;
	uint16_t sector_size;      /* 每个逻辑扇区的字节数。 */
	uint32_t sector_offset;    /* 此分区起点相对底层存储起点的扇区偏移。 */
	uint32_t sector_cnt;       /* 此磁盘实例可寻址的逻辑扇区总数。 */

	/* 后端提供的回调表；访问层查表后调用对应入口。 */
	const struct disk_operations *ops;
	/* 驱动可在此保存 Zephyr 设备；本项目 NOR 后端另用模块级 nor_disk 保存绑定。 */
	const struct device *dev;
};

/* 驱动实现的块操作。回调收到访问层按名称查出的同一个 disk_info。 */
struct disk_operations {
	/**
	 * @brief  供磁盘访问层在首次 I/O 前初始化该实例的后端。
	 * @param  disk 已注册的磁盘实例。
	 * @retval 0 成功，负值为后端错误；本项目 NOR 未绑定设备时返回 -ENODEV。
	 */
	int (*init)(struct disk_info *disk);
	/**
	 * @brief  由磁盘访问层查询当前实例的介质状态。
	 * @param  disk 已注册的磁盘实例。
	 * @retval 返回 DISK_STATUS_OK 或表示未初始化、无介质、写保护等状态位。
	 */
	int (*status)(struct disk_info *disk);
	/**
	 * @brief  由访问层将逻辑扇区读取请求分发给驱动。
	 * @param  disk 决定介质和分区范围的已注册实例。
	 * @param  data_buf 至少可容纳全部请求扇区的输出地址。
	 * @param  start_sector 本实例内的起始逻辑扇区。
	 * @param  num_sector 请求读取的扇区数量。
	 * @retval 0 成功，负值为驱动报告的读取错误。
	 */
	int (*read)(struct disk_info *disk, uint8_t *data_buf,
		    uint32_t start_sector, uint32_t num_sector);
	/**
	 * @brief  由访问层将逻辑扇区写请求分发给驱动。
	 * @param  disk 决定介质和分区范围的已注册实例。
	 * @param  data_buf 调用期间可读的连续输入数据。
	 * @param  start_sector 本实例内的起始逻辑扇区。
	 * @param  num_sector 待写入的扇区数量。
	 * @retval 0 成功，负值为驱动错误；支持回写缓存的后端可延后落盘。
	 */
	int (*write)(struct disk_info *disk, const uint8_t *data_buf,
		     uint32_t start_sector, uint32_t num_sector);
	/**
	 * @brief  执行此驱动支持的同步、属性或设备控制。
	 * @param  disk 目标实例。
	 * @param  cmd 控制编号。
	 * @param  buff 控制所需的输入或输出空间，由命令与后端约定。
	 * @retval 0 成功，负值为后端错误，NOR 遇到不支持的命令返回 -EINVAL。
	 */
	int (*ioctl)(struct disk_info *disk, uint8_t cmd, void *buff);
};

/**
 * @brief  将磁盘实例加入系统注册表，以便消费者通过名称访问。
 * @param  disk 待注册的持久实例，名称非空且唯一，节点和操作表要在使用期间保持有效。
 * @retval 0 成功，空实例、空名称或同名实例已注册时为 -EINVAL。
 */
int disk_access_register(struct disk_info *disk);

/**
 * @brief  从系统注册表中摘除给定实例。
 * @param  disk 最初传给注册函数的同一实例
 * @param  调用方须与正在使用该实例的线程协调其生命周期。
 * @retval 0 成功，空实例、空名称或查询不到该名称时为 -EINVAL。
 * @note   实现按名称查存在性，但实际移除的是传入实例自身的链表节点。
 */
int disk_access_unregister(struct disk_info *disk);

/**
 * @brief  从互斥保护的注册表查找完全同名的磁盘。
 * @param  pdrv 非空、以 NUL 结束的注册名称。
 * @retval 成功返回注册方拥有的磁盘实例，未找到时为 NULL。
 * @note   返回前已经解锁；调用方应保证实例不会在使用期间注销并销毁。
 */
struct disk_info *disk_access_get_di(const char *pdrv);


#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISK_H_ */
