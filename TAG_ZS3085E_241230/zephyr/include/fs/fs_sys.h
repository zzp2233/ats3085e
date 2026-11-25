/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_FS_FS_SYS_H_    /* 防止同一编译单元重复定义文件系统操作表。 */
#define ZEPHYR_INCLUDE_FS_FS_SYS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup file_system_api
 * @{
 */

/** @brief 文件系统操作表。挂载后由 fs_* 按挂载点分派，具体实现注册静态函数表。同步读写返回实际字节数或负错误码，普通控制操作通常以 0 表示成功。 */
struct fs_file_system_t {
	/* 打开的文件保存后端对象与挂载点，调用方负责在关闭前保持描述符有效。 */
	int (*open)(struct fs_file_t *filp, const char *fs_path,
		    fs_mode_t flags);                                          /* 打开或创建 fs_path 指向的文件，flags 指定读写方式，结果保存在可写 filp 中。 */
	ssize_t (*read)(struct fs_file_t *filp, void *dest, size_t nbytes);    /* 从已打开的 filp 读取最多 nbytes 字节到 dest；短读和 EOF 由返回长度表示。 */
	ssize_t (*write)(struct fs_file_t *filp,
					const void *src, size_t nbytes);    /* 从只读 src 向文件写最多 nbytes 字节，调用者须核实实际写入量。 */
	int (*lseek)(struct fs_file_t *filp, off_t off, int whence);        /* 按 whence 选取基点，加 off 字节定位。 */
	off_t (*tell)(struct fs_file_t *filp);                              /* 返回当前文件位置（字节）或负错误码。 */
	int (*truncate)(struct fs_file_t *filp, off_t length);              /* 将文件调整为 length 字节，后端决定是否支持扩展。 */
	int (*sync)(struct fs_file_t *filp);                                /* 请求写回该文件的缓存。 */
	int (*close)(struct fs_file_t *filp);                               /* 刷出并关闭文件，结束后端句柄生命周期。 */
	/* 目录游标从打开持续到关闭，每次 readdir 向调用者提供一个目录项。 */
	int (*opendir)(struct fs_dir_t *dirp, const char *fs_path);        /* 打开 fs_path 的目录，后端状态存入 dirp。 */
	int (*readdir)(struct fs_dir_t *dirp, struct fs_dirent *entry);    /* 从当前游标取得下一条记录，写入可写 entry。 */
	int (*closedir)(struct fs_dir_t *dirp);                            /* 关闭目录并释放其遍历资源。 */
	/* 卷级操作以 mountp 选择目标卷，路径由上层先按挂载点解析。 */
	int (*mount)(struct fs_mount_t *mountp);                       /* 把 mountp 描述的卷接入后端。 */
	int (*unmount)(struct fs_mount_t *mountp);                     /* 注销已挂载的卷，调用者须先结束使用。 */
	int (*unlink)(struct fs_mount_t *mountp, const char *name);    /* 在挂载卷中删除 name 指定的文件或目录。 */
	int (*rename)(struct fs_mount_t *mountp, const char *from,
					const char *to);              /* 在该卷中将 from 指定的路径改名为 to。 */
	int (*mkdir)(struct fs_mount_t *mountp, const char *name);    /* 在挂载卷内创建 name 指定的目录。 */
	int (*stat)(struct fs_mount_t *mountp, const char *path,
					struct fs_dirent *entry);    /* 查询 path 的名称、类型和长度，写入 entry。 */
	int (*statvfs)(struct fs_mount_t *mountp, const char *path,
					struct fs_statvfs *stat);    /* 查询目标卷的容量和可用空间，写入 stat。 */
	int (*disk_detect)(struct fs_mount_t *mountp, const char *path,
					uint8_t *state);    /* 查询介质插拔状态，结果写入 state。 */
	int (*open_cluster)(struct fs_file_t *zfp, char *dir,
					uint32_t cluster, uint32_t blk_ofs);    /* 按簇号和块内偏移定位文件，结果写入 zfp。 */
	int (*opendir_cluster)(struct fs_dir_t *zdp, const char *path,
					uint32_t cluster, uint32_t blk_ofs);    /* 按簇号和块内偏移定位目录，结果写入 zdp。 */
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_FS_FS_SYS_H_ */
