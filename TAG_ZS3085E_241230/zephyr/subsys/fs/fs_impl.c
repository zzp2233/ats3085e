/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 为需要卷内绝对路径的后端提供路径视图，复用调用者的缓冲区。 */
#include <zephyr.h>     /* 提供内核通用类型定义。 */
#include <fs/fs.h>      /* 提供 fs_mount_t 及已缓存的挂载路径长度。 */
#include "fs_impl.h"    /* 保持实现与内部接口的参数声明一致。 */

/**
  * @brief  从已匹配的绝对路径去掉挂载点前缀。
  * @param  path：[输入]可为空；非空时须指向已通过前缀匹配的绝对路径。
  * @param  mp：[输入]可为空，非空时提供该挂载点的字节长度。
  * @retval 任一输入为空时原样返回 path；匹配整个路径时返回常量根路径，否则返回指向输入字符串内部的后缀指针。
  * @note   调用方保证前缀及长度正确。返回值不分配内存，引用输入时不能超过输入字符串的生命周期。
  */
const char *fs_impl_strip_prefix(const char *path,
				 const struct fs_mount_t *mp)
{
	static const char *const root = "/";     /* 单独保存根路径，避免以空字符串表示卷根。 */

	if ((path == NULL) || (mp == NULL)) {    /* 允许没有路径或挂载描述的调用直接传递原值。 */
		return path;                     /* 保留输入，path 本身为空时结果也是 NULL。 */
	}

	path += mp->mountp_len;                  /* 依据调用者已经核对过的前缀字节数移动指针。 */
	return *path ? path : root;              /* 有后缀时直接引用它，否则返回卷根字符串。 */
}
