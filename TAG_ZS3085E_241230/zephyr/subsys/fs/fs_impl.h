/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 供文件系统实现使用的路径辅助接口。 */

#ifndef ZEPHYR_SUBSYS_FS_FS_IMPL_H_    /* 防止在同一编译单元中重复声明。 */
#define ZEPHYR_SUBSYS_FS_FS_IMPL_H_

#include <fs/fs.h>    /* 导入挂载描述及其所属的公共接口。 */

#ifdef __cplusplus    /* C++ 使用方沿用 C 的符号名。 */
extern "C" {
#endif

/**
  * @brief  从已匹配的绝对路径去掉挂载点前缀。
  * @param  path：[输入]可为空；非空时须指向已通过前缀匹配的绝对路径。
  * @param  mp：[输入]可为空，非空时提供该挂载点的字节长度。
  * @retval 任一输入为空时原样返回 path；匹配整个路径时返回常量根路径，否则返回指向输入字符串内部的后缀指针。
  * @note   调用方保证前缀及长度正确。返回值不分配内存，引用输入时不能超过输入字符串的生命周期。
  */
const char *fs_impl_strip_prefix(const char *path,
				 const struct fs_mount_t *mp);


#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_SUBSYS_FS_FS_IMPL_H_ */
