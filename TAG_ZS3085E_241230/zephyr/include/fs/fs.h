/*
 * Copyright (c) 2016 Intel Corporation.
 * Copyright (c) 2020-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Zephyr 虚拟文件系统（VFS）的公共接口。上层发送文件时通过这些接口按路径打开并读取文件，挂载对象决定具体后端。 */
#ifndef ZEPHYR_INCLUDE_FS_FS_H_    /* 限定此头文件在同一编译单元中只展开一次。 */
#define ZEPHYR_INCLUDE_FS_FS_H_

#include <sys/types.h>    /* 提供 size_t、ssize_t、off_t 等接口所需类型。 */

#include <sys/dlist.h>          /* 挂载项通过双向链表节点接入 VFS。 */
#include <fs/fs_interface.h>    /* 导入文件和目录句柄、名称长度和模式类型。 */

#ifdef __cplusplus    /* 使用 C++ 编译时为下列函数保留 C 链接名。 */
extern "C" {
#endif

/**
 * @brief 访问已挂载卷的文件、目录及容量信息。
 * @defgroup file_system_api 文件系统接口
 * @{
 */
struct fs_file_system_t;    /* 前置声明挂载点引用的后端操作表，成员定义见 fs_sys.h。 */

enum fs_dir_entry_type {    /* 供 fs_dirent.type 区分文件和子目录。 */
	/** @brief 表示文件的目录项。 */
	FS_DIR_ENTRY_FILE = 0,
	/** @brief 子目录项。 */
	FS_DIR_ENTRY_DIR
};

/** @brief 各后端使用唯一类型号注册；外部文件系统从 FS_TYPE_EXTERNAL_BASE 起自行分配，避免与其他实现重复。 */
enum {
	/** @brief FatFs 适配层注册时使用的编号。 */
	FS_FATFS = 0,

	/** @brief LittleFS 后端的注册编号。 */
	FS_LITTLEFS,

	FS_SDFS,    /* 项目内 SDFS 文件系统的注册编号。 */

	/** @brief 应用自定义后端的起始编号。 */
	FS_TYPE_EXTERNAL_BASE,
};

/** @brief 未识别到文件系统时禁止自动格式化。 */
#define FS_MOUNT_FLAG_NO_FORMAT BIT(0)
/** @brief 按只读属性挂载，常规创建、写方式打开、改名及删除会先经过 VFS 检查。 */
#define FS_MOUNT_FLAG_READ_ONLY BIT(1)
/** @brief 预定义 fstab 项的启动自动挂载标记；手工构造的挂载对象自行调用 fs_mount。 */
#define FS_MOUNT_FLAG_AUTOMOUNT BIT(2)


/** @brief 挂载描述由调用方提供，并在成功挂载到卸载之间保持有效，多个挂载点可以使用同一后端类型。 */
struct fs_mount_t {
	sys_dnode_t node;         /* 挂载成功后由内核链入挂载表。 */
	int type;                 /* 与 fs_register 登记的后端编号对应。 */
	const char *mnt_point;    /* 借用的绝对挂载路径，例如 /NOR:，挂载期间保持有效。 */
	void *fs_data;            /* 由具体后端解释；FatFs 使用调用方提供的 FATFS 卷对象。 */
	void *storage_dev;        /* 指向后端使用的存储设备或后端约定的设备标识。 */
	/* 以下长度和操作表由 VFS 在挂载成功时填写。 */
	size_t mountp_len;                    /* 成功挂载后缓存路径字节数，加速前缀比较。 */
	const struct fs_file_system_t *fs;    /* 成功挂载后指向该类型的实际操作表。 */
	uint8_t flags;                        /* 调用方设置的只读、禁止格式化等位掩码。 */
};

/** @brief fs_stat 或 fs_readdir 填写的目录项，名称缓冲位于结构体内部。 */
struct fs_dirent {
	enum fs_dir_entry_type type;     /* 真实目录来自后端，虚拟根目录的挂载项记为目录。 */
	char name[MAX_FILE_NAME + 1];    /* 预留终止字符；遍历结束时首字节为零。 */
	size_t size;                     /* 文件长度，单位字节；虚拟挂载目录项填零。 */
};

/** @brief 卷容量信息。传输块与分配单元的大小可以不同，容量按分配单元计数。 */
struct fs_statvfs {
	unsigned long f_bsize;     /* 后端的传输块大小，单位字节。 */
	unsigned long f_frsize;    /* 一个分配单元对应的字节数，FatFs 中通常为一簇。 */
	unsigned long f_blocks;    /* 卷的总分配单元数，乘 f_frsize 得到容量字节数。 */
	unsigned long f_bfree;     /* 当前可用的分配单元数，单位与 f_blocks 相同。 */
};


/**
 * @name fs_open 的访问、创建和追加标志
 * @{
 */
/* 允许读取。 */
#define FS_O_READ       0x01
/** @brief 允许写入。 */
#define FS_O_WRITE      0x02
/** @brief 同时允许读写。 */
#define FS_O_RDWR       (FS_O_READ | FS_O_WRITE)
/** @brief 提取读写权限位。 */
#define FS_O_MODE_MASK  0x03

/** @brief 文件不存在时创建，现有文件的内容保留。 */
#define FS_O_CREATE     0x10
/** @brief 请求在每次写入前定位到末尾；需要创建时另设 FS_O_CREATE。 */
#define FS_O_APPEND     0x20
/** @brief 提取创建及追加标志。 */
#define FS_O_FLAGS_MASK 0x30

/** @brief 本接口识别的全部打开标志。 */
#define FS_O_MASK       (FS_O_MODE_MASK | FS_O_FLAGS_MASK)
/**
 * @}
 */

/**
 * @name fs_seek 的定位基点
 * @{
 */
#ifndef FS_SEEK_SET
/** @brief 相对文件开头。 */
#define FS_SEEK_SET	0
#endif
#ifndef FS_SEEK_CUR
/** @brief 相对当前游标。 */
#define FS_SEEK_CUR	1
#endif
#ifndef FS_SEEK_END
/** @brief 相对文件末尾。 */
#define FS_SEEK_END	2
#endif
/**
 * @}
 */

/* 从 zephyr,fstab 子节点的 automount、read_only、no_format 属性组合出 fs_mount_t.flags。 */
#define FSTAB_ENTRY_DT_MOUNT_FLAGS(node_id)				\
	((DT_PROP(node_id, automount) ? FS_MOUNT_FLAG_AUTOMOUNT : 0)	\
	 | (DT_PROP(node_id, read_only) ? FS_MOUNT_FLAG_READ_ONLY : 0)	\
	 | (DT_PROP(node_id, no_format) ? FS_MOUNT_FLAG_NO_FORMAT : 0))

/** @brief 把设备树节点标识拼入符号名，生成其挂载结构的名称。 */
#define FS_FSTAB_ENTRY(node_id) _CONCAT(z_fsmp_, node_id)

/** @brief 声明与设备树 fstab 项对应的外部 fs_mount_t 对象，由定义它的编译单元提供存储。 */
#define FS_FSTAB_DECLARE_ENTRY(node_id)		\
	extern struct fs_mount_t FS_FSTAB_ENTRY(node_id)

/**
  * @brief  初始化供 fs_open 使用的文件对象。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 无。
  * @note   首次打开前调用；已有后端句柄须先关闭。
  */
static inline void fs_file_t_init(struct fs_file_t *zfp)
{
	*zfp = (struct fs_file_t){ 0 };    /* 清空后端文件指针、挂载指针和打开模式。 */
}

/**
  * @brief  初始化目录对象以便首次打开。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @retval 无。
  * @note   仅用于未打开或已关闭的对象。
  */
static inline void fs_dir_t_init(struct fs_dir_t *zdp)
{
	*zdp = (struct fs_dir_t){ 0 };    /* 清空挂载点与目录游标，供首次打开时检查。 */
}

/**
  * @brief  按路径选择挂载卷并打开文件，需要时创建。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  file_name：[输入]以 / 开头、含文件名的零终止字符串。
  * @param  flags：[输入]读、写、创建和追加标志的按位组合，也可为 0，以无读写权限尝试打开已有文件。
  * @retval 0 表示成功。-EINVAL 路径无效，-EBUSY 对象已打开，-EROFS 只读卷拒绝修改，-ENOENT 未匹配挂载点，后端不可用或操作失败返回相应负值。
  * @note   首次使用先初始化对象。FS_O_CREATE 保留已有文件；FS_O_APPEND 要求每次写入前定位到末尾。
  */
int fs_open(struct fs_file_t *zfp, const char *file_name, fs_mode_t flags);

/**
  * @brief  经已绑定的卷关闭文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 0 表示已关闭或本来未打开；未提供关闭方法时可返回 -ENOTSUP，其他负值来自后端。
  * @note   成功后可重新使用同一文件对象。若关闭失败，前端仍保留挂载点。
  */
int fs_close(struct fs_file_t *zfp);

/**
  * @brief  删除指定文件或可删除的目录项。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 只读卷返回 -EROFS，其他删除错误由后端决定。
  * @note   挂载表查找会等待互斥锁，应在线程上下文调用。
  */
int fs_unlink(const char *path);

/**
  * @brief  将同卷目录项移动或重命名。
  * @param  from：[输入]非空的源绝对路径。
  * @param  to：[输入]非空的目标绝对路径。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 卷前缀不符亦返回 -EINVAL，源卷只读返回 -EROFS。
  * @note   前端只比较源挂载前缀长度。FatFs 后端会先删除已存在的目标。
  */
int fs_rename(const char *from, const char *to);

/**
  * @brief  从打开的文件当前位置读取数据。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  ptr：[输出]至少可写 size 字节的目标区。
  * @param  size：[输入]本次最多读取的字节数。
  * @retval 成功返回实际读取字节数，短读可能到达文件尾，0 表示未取得数据。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   上传端需要固定长度时须校验返回值是否等于请求长度。
  */
ssize_t fs_read(struct fs_file_t *zfp, void *ptr, size_t size);

/**
  * @brief  把连续缓冲写入已打开的文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  ptr：[输入]至少可读 size 字节的源数据。
  * @param  size：[输入]计划写入的字节数。
  * @retval 成功返回实际写入字节数，可以短写。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   调用者负责识别短写并按需同步，返回的长度并不代表已经写回介质。
  */
ssize_t fs_write(struct fs_file_t *zfp, const void *ptr, size_t size);

/**
  * @brief  按所选基点调整文件游标。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  offset：[输入]相对所选基点的有符号字节偏移。
  * @param  whence：[输入]FS_SEEK_SET、FS_SEEK_CUR 或 FS_SEEK_END。
  * @retval 0 表示成功。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。 本项目 FatFs 对越界位置或无效基点返回 -EINVAL。
  * @note   移动游标不修改文件内容。
  */
int fs_seek(struct fs_file_t *zfp, off_t offset, int whence);

/**
  * @brief  读取打开文件的当前字节位置。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 成功返回从文件开头计算的非负字节偏移。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   调用者须提供已初始化的有效对象。
  */
off_t fs_tell(struct fs_file_t *zfp);

/**
  * @brief  将打开文件缩短或延长到给定长度。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  length：[输入]目标文件长度，单位字节，应为非负值。
  * @retval 0 表示成功。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   当前 FatFs 在扩展定位后仅同步，不填零。空间不足时实际长度可能小于请求值。
  */
int fs_truncate(struct fs_file_t *zfp, off_t length);

/**
  * @brief  请求后端把打开文件的缓存写回介质。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 0 表示成功。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   需要保留句柄继续访问时可在关闭前同步。
  */
int fs_sync(struct fs_file_t *zfp);

/**
  * @brief  在路径对应的挂载卷创建目录。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 只读卷返回 -EROFS，已有同名项或介质错误由后端返回。
  * @note   挂载表查找会等待互斥锁，应在线程上下文调用。
  */
int fs_mkdir(const char *path);

/**
  * @brief  打开挂载卷中的目录或枚举虚拟根目录。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @retval 0 表示成功。-EINVAL 路径无效，-EBUSY 对象已有游标，-ENOENT 无匹配卷，其他负值来自后端或操作检查。
  * @note   根路径 / 枚举挂载表，其他路径按卷分派。
  */
int fs_opendir(struct fs_dir_t *zdp, const char *path);

/**
  * @brief  从卷目录或虚拟根目录读取下一个可见目录项。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  entry：[输出]非空、可写的目录项，用于接收名称、类型和长度。
  * @retval 0 表示取得一项或遍历结束，结束时 name[0] 为 0。根目录游标对应的挂载项已移除时返回 -ENOENT，其他负值来自操作检查或后端。
  * @note   过滤真实目录中的 . 和 ..；虚拟根目录以挂载点名称构造目录项。
  */
int fs_readdir(struct fs_dir_t *zdp, struct fs_dirent *entry);

/**
  * @brief  结束一次目录遍历并释放游标。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @retval 0 为成功，缺少方法时可返回 -ENOTSUP，其他负值来自后端。
  * @note   虚拟根目录只需丢弃链表游标，真实目录由挂载卷关闭。
  */
int fs_closedir(struct fs_dir_t *zdp);

/**
  * @brief  按注册类型挂载一个卷，成功后加入路径匹配表。
  * @param  mp：[输入/输出]非空的挂载描述，路径、操作表及卷对象在整个挂载期间须有效。
  * @retval 0 为成功，-EINVAL 为描述或路径无效，-EBUSY 为同名挂载点，-ENOENT 为类型未注册，-ENOTSUP 为缺少挂载方法，其他负值来自后端。
  * @note   挂载名须以 / 开头且长于单个斜线。对象及其字符串由调用者保持到成功卸载。FatFs 后端使用配置的卷名或数字卷号。
  */
int fs_mount(struct fs_mount_t *mp);

/**
  * @brief  卸载指定卷并移除其挂载点。
  * @param  mp：[输入/输出]非空的挂载描述，路径、操作表及卷对象在整个挂载期间须有效。
  * @retval 0 为成功，-EINVAL 为描述为空或尚未挂载，-ENOTSUP 为不支持卸载，其他负值来自后端。
  * @note   先结束使用该卷的文件和目录对象；后端卸载成功后才清理挂载表。
  */
int fs_unmount(struct fs_mount_t *mp);

/**
  * @brief  按索引依次读取当前挂载点名称。
  * @param  index：[输入/输出]非空的可写索引，首项为 0，成功后自增。
  * @param  name：[输出]非空的指针变量，接收借用的挂载名或 NULL。
  * @retval 0 为取得名称，-ENOENT 为没有相应项。
  * @note   名称仍属于挂载对象；挂载表在两次调用之间发生变化时，索引可对应不同对象。
  */
int fs_readmount(int *index, const char **name);

/**
  * @brief  按路径读取单个文件或目录的元数据。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @param  entry：[输出]非空、可写的目录项，用于接收名称、类型和长度。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 不存在的对象也可返回 -ENOENT。
  * @note   对象不存在是可预期的查询结果，前端原样返回该状态。
  */
int fs_stat(const char *path, struct fs_dirent *entry);

/**
  * @brief  取得指定卷的分配单位、容量及剩余空间。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @param  stat：[输出]非空、可写的容量结构，各字段单位见 struct fs_statvfs。
  * @retval 0 通常表示成功，非法路径返回 -EINVAL，未匹配到卷返回 -ENOENT，已实现的后端返回自身状态。
  * @note   后端未安装 statvfs 时本实现沿用挂载点查询的 0，调用者不能据此认定输出已填充。
  */
int fs_statvfs(const char *path, struct fs_statvfs *stat);

/**
  * @brief  将文件系统类型与其操作表加入固定容量的注册表。
  * @param  type：[输入]注册表内唯一的文件系统类型编号。
  * @param  fs：[输入]非空的只读操作表，须在注册和卷使用期间持续有效。
  * @retval 0 为成功，-EALREADY 为类型已注册，-ENOSPC 为注册表已满。
  * @note   注册只登记类型，挂载后路径才能被 fs_open 等接口匹配。
  */
int fs_register(int type, const struct fs_file_system_t *fs);

/**
  * @brief  注销类型与操作表均匹配的注册项。
  * @param  type：[输入]注册表内唯一的文件系统类型编号。
  * @param  fs：[输入]注册时传入的同一操作表地址。
  * @retval 0 为已移除，-EINVAL 为类型未注册或表地址不匹配。
  * @note   先卸载依赖该表的卷，此处仅清注册项，不遍历已挂载卷。
  */
int fs_unregister(int type, const struct fs_file_system_t *fs);

/**
  * @brief  以簇号和目录偏移定位遍历起点。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @param  cluster：[输入]FAT 目录的起始簇号。
  * @param  blk_ofs：[输入]目录内的字节偏移，须落在有效目录项边界。
  * @retval -EINVAL 为无效路径，-ENOENT 为无匹配卷；已安装扩展且调用成功返回 0，失败返回后端错误码。
  * @note   根目录 / 总是从挂载表枚举。匹配卷后先填 mp；缺少扩展时会沿用匹配成功的 0 而未打开后端对象；后端失败时也保留 mp。
  */
int fs_opendir_cluster(struct fs_dir_t *zdp, const char *path, unsigned int cluster, unsigned int blk_ofs);

/**
  * @brief  按目录簇号和目录项偏移打开文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  dir：[输入]非空、以斜线开头的卷或目录路径，用于选择挂载点。
  * @param  cluster：[输入]FAT 目录的起始簇号。
  * @param  blk_ofs：[输入]目录内的字节偏移，须落在有效目录项边界。
  * @retval -EINVAL 为无效路径，-ENOENT 为无匹配卷；已安装扩展且调用成功返回 0，失败返回后端错误码。
  * @note   匹配卷后先填 mp；缺少扩展时会沿用匹配成功的 0 而未打开后端对象；后端失败时也保留 mp。
  */
int fs_open_cluster(struct fs_file_t *zfp, char *dir, unsigned int cluster, unsigned int blk_ofs);

/**
  * @brief  由已挂载的后端查询介质是否在位。
  * @param  str：[输入]非空、以斜线开头的路径字符串。
  * @param  state：[输出]非空的单字节状态，实际位值由底层驱动定义。
  * @retval 无效路径返回 -EINVAL，未匹配卷返回 -ENOENT；成功分派时返回底层检测结果。
  * @note   本项目 FatFs 检测失败会直接返回非零 FRESULT；未安装检测方法时返回 0 而不填写 state。
  */
int fs_disk_detect(const char *str, unsigned char *state);

/**
 * @}
 */


#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_FS_FS_H_ */
