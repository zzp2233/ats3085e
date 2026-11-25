/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* VFS 与各文件系统后端共享的句柄布局和缓冲区长度。公共函数原型见 fs.h。 */
#ifndef ZEPHYR_INCLUDE_FS_FS_INTERFACE_H_    /* 避免重复定义公共句柄类型。 */
#define ZEPHYR_INCLUDE_FS_FS_INTERFACE_H_

#include <stdint.h>    /* 为打开标志提供固定宽度的 uint8_t。 */

#ifdef __cplusplus    /* C++ 调用端使用 C 链接约定。 */
extern "C" {
#endif

#if 0                                                     /* 保留早期按后端配置推导名称容量的方案，此段不参与编译。 */
#if (CONFIG_FILE_SYSTEM_MAX_FILE_NAME - 0) > 0            /* 旧方案优先采用显式指定的文件名上限。 */
#define MAX_FILE_NAME CONFIG_FILE_SYSTEM_MAX_FILE_NAME    /* 使用配置给出的最大文件名长度。 */
#else                                                     /* CONFIG_FILE_SYSTEM_MAX_FILE_NAME */
/* 旧方案按已启用的文件系统选择默认上限。 */
#if defined(CONFIG_FILE_SYSTEM_LITTLEFS)         /* 旧方案选择 LittleFS 时预留较长名称。 */
#define MAX_FILE_NAME 256                        /* LittleFS 分支预留 256 个名称字符。 */
#elif defined(CONFIG_FAT_FILESYSTEM_ELM)         /* 旧方案选择 FatFs 时按长文件名配置调整上限。 */
#if defined(CONFIG_FS_FATFS_LFN)                 /* 判断是否启用长文件名。 */
#define MAX_FILE_NAME CONFIG_FS_FATFS_MAX_LFN    /* 使用配置的 FatFs 长名称上限。 */
#else                                            /* CONFIG_FS_FATFS_LFN */
#define MAX_FILE_NAME 12                         /* 8.3 短文件名至多 12 个字符，包括点。 */
#endif                                           /* CONFIG_FS_FATFS_LFN */
#else                                            /* 旧方案未指定后端 */
/* 未显式选用后端时，以 8.3 短文件名为默认。 */
#define MAX_FILE_NAME 12    /* 保留 8.3 短文件名所需空间。 */
#endif                      /* 结束按后端选择 */
#endif                      /* CONFIG_FILE_SYSTEM_MAX_FILE_NAME */
#endif

/* fs_open 访问模式与创建标志的存储类型。 */
typedef uint8_t fs_mode_t;    /* 文件打开标志由一个字节存放，可按位组合。 */

#if CONFIG_XSFN_OPT == 0     /* 项目当前使用这组条件确定实际缓冲上限。 */
#define MAX_FILE_NAME 256    /* 不启用 XSFN 优化时的文件名字符上限。 */
#define MAX_URL_LEN   512    /* 相同配置下 URL 上限为 512。 */
#else
	#if _USE_XSFN                        /* 在优化分支内按是否使用扩展短文件名再选择。 */
		#define MAX_FILE_NAME 32     /* 启用扩展短文件名时名称上限为 32。 */
		#define MAX_URL_LEN   256    /* 扩展短文件名配置的 URL 上限。 */
		// #define MAX_FILE_NAME 256  旧的候选文件名上限，未启用。
	#else
		#define MAX_FILE_NAME 12     /* 常规 8.3 短文件名上限包含分隔点。 */
		#define MAX_URL_LEN   256    /* 常规短文件名配置的 URL 上限。 */
	#endif
#endif

struct fs_mount_t;    /* 这里只保留指针，挂载描述在 fs.h 中定义。 */

/**
 * @addtogroup file_system_api
 * @{
 */

/** @brief 文件句柄在首次打开前须用 fs_file_t_init 初始化，整个打开期间由调用者提供存储。 */
struct fs_file_t {
	void *filep;                    /* 打开后由后端维护；FatFs 在固定内存池中分配 FIL。 */
	const struct fs_mount_t *mp;    /* 绑定当前文件所在的卷，读写通过该卷的操作表分派。 */
	fs_mode_t flags;                /* 保存 fs_open 收到的访问和追加标志。 */
};

/** @brief 目录句柄在首次使用前须用 fs_dir_t_init 初始化，遍历期间保存当前位置。 */
struct fs_dir_t {
	void *dirp;                     /* 真实目录存后端对象；VFS 根目录存下一个挂载链表节点。 */
	const struct fs_mount_t *mp;    /* 绑定真实卷；遍历虚拟根目录时为 NULL。 */
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_FS_FS_INTERFACE_H_ */
