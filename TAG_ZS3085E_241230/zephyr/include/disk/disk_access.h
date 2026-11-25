/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 旧路径的兼容头。实际磁盘 API 已移至 storage/disk_access.h；保护宏避免从旧路径多次引入声明。 */

#ifndef ZEPHYR_INCLUDE_DISK_DISK_ACCESS_H_
#define ZEPHYR_INCLUDE_DISK_DISK_ACCESS_H_

/* 关闭兼容包含选项时，仍会转发到新头，同时由预处理器提示迁移。 */
#ifndef CONFIG_COMPAT_INCLUDES
#warning "This header file has moved, include <storage/disk_access.h> instead."
#endif

/* 通过正式路径取得同一套磁盘类型、控制码及读写入口。 */
#include <storage/disk_access.h>

#endif /* ZEPHYR_INCLUDE_DISK_DISK_ACCESS_H_ */
