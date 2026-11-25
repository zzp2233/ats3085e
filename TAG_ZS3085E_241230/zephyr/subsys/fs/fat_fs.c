/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Zephyr VFS 与 FatFs 的项目适配层。把打开标志、带前导斜线的卷路径和目录项在两套接口之间转换；介质访问再由 FatFs 的 diskio 接口下沉。 */
#include <stdio.h>           /* 导入标准输入输出相关类型。 */
#include <string.h>          /* 提供内存清零和目录项名称复制。 */
#include <kernel.h>          /* 用内核定长内存池管理 FIL 与 DIR。 */
#include <zephyr/types.h>    /* 提供固定宽度的整数类型。 */
#include <errno.h>           /* 将 FatFs 的 FRESULT 转为负的 errno。 */
#include <init.h>            /* 声明系统启动阶段注册入口。 */
#include <fs/fs.h>           /* 取得公共挂载、文件和目录句柄定义。 */
#include <fs/fs_sys.h>       /* 导入具体后端必须实现的函数指针表。 */
#include <sys/__assert.h>    /* 引入内核断言定义。 */
#include <ff.h>              /* FatFs 库边界，提供 FIL、DIR、FATFS、操作标志和 f_ 系列函数。 */

/* 快速调整长度：扩展时同步，不写入零填充。 */
#define FATFS_QUICK_TRUNCATE    /* 启用本文件的快速扩展路径，新增区域不会逐字节填零。 */

#define FATFS_MAX_FILE_NAME 12    /* 常规 8.3 短文件名的最大字符数包括点。 */

/* 每个打开的真实目录占用一个 DIR 池块，虚拟根目录不经过此池。 */
K_MEM_SLAB_DEFINE(fatfs_dirp_pool, sizeof(DIR),
			CONFIG_FS_FATFS_NUM_DIRS, 4);    /* 内存块数由允许的并发目录数决定，按 4 字节对齐。 */

/* 每个并发打开的文件占用一个 FIL 池块；裸指针不需由应用分配。 */
K_MEM_SLAB_DEFINE(fatfs_filep_pool, sizeof(FIL),
			CONFIG_FS_FATFS_NUM_FILES, 4);    /* 内存块数由允许的并发文件数决定，按 4 字节对齐。 */

/**
  * @brief  将 FatFs 状态映射为 VFS 错误码。
  * @param  error：[输入]一个 FatFs FRESULT 值。
  * @retval 成功为 0；失败返回对应负 errno，未列出的状态统一返回 -EIO。
  * @note   多个底层原因可映射到同一错误码。
  */
static int translate_error(int error)
{

	/* 以公共 errno 表示库状态，应用可在 VFS 层统一处理错误。 */
	switch (error) {                /* 以 FatFs 状态选择 VFS 调用者可识别的错误。 */
	case FR_OK:                     /* FatFs 操作成功。 */
		return 0;               /* 以零表示成功。 */
	case FR_NO_FILE:                /* 未找到文件。 */
	case FR_NO_PATH:                /* 路径中缺少目录或其他分量。 */
	case FR_INVALID_NAME:           /* 非法名称与路径缺失归为同一类。 */
		return -ENOENT;         /* 向 VFS 报告名称或对象不存在。 */
	case FR_DENIED:                 /* 库因访问条件不满足拒绝操作。 */
		return -EACCES;         /* 归为访问被拒绝。 */
	case FR_EXIST:                  /* 同名对象已存在。 */
		return -EEXIST;         /* 调用者可识别同名对象已经存在。 */
	case FR_INVALID_OBJECT:         /* 文件或目录对象未生效或已失效。 */
		return -EBADF;          /* 归为无效句柄。 */
	case FR_WRITE_PROTECTED:        /* 库报告介质写保护。 */
		return -EROFS;          /* 归为只读文件系统。 */
	case FR_INVALID_DRIVE:          /* 逻辑驱动号无效。 */
	case FR_NOT_ENABLED:            /* 卷工作区尚未启用。 */
	case FR_NO_FILESYSTEM:          /* 指定介质没有可识别的文件系统。 */
		return -ENODEV;         /* 以上三种卷状态统一报告无设备。 */
	case FR_NOT_ENOUGH_CORE:        /* FatFs 内部内存不足。 */
		return -ENOMEM;         /* 向调用方报告内存不足。 */
	case FR_TOO_MANY_OPEN_FILES:    /* 达到同时打开文件的限制。 */
		return -EMFILE;         /* 向调用方报告打开文件过多。 */
	case FR_INVALID_PARAMETER:      /* 请求参数无效。 */
		return -EINVAL;         /* 保留参数错误语义。 */
	case FR_LOCKED:                 /* 文件被锁定。 */
	case FR_TIMEOUT:                /* 等待库资源超时。 */
	case FR_MKFS_ABORTED:           /* 格式化过程终止。 */
	case FR_DISK_ERR:               /* 磁盘访问失败。 */
	case FR_INT_ERR:                /* 库内部一致性检查失败。 */
	case FR_NOT_READY:              /* 驱动或介质尚未就绪。 */
		return -EIO;            /* 这些底层状态统一作为 I/O 错误上报。 */
	}

	return -EIO;                    /* 未显式映射的状态也归入 I/O 错误。 */
}

/**
  * @brief  构造传给 f_open 的 FatFs 打开标志。
  * @param  flags：[输入]VFS 的 fs_mode_t 权限与创建标志。
  * @retval 返回 FatFs 使用的八位模式掩码。
  * @note   FS_O_APPEND 单独保存在 VFS 句柄中，由写入路径处理。
  */
static uint8_t translate_flags(fs_mode_t flags)
{
	uint8_t fat_mode = 0;                                      /* 先清除所有 FatFs 标志，再按 VFS 模式置位。 */

	/* 三个访问与创建位独立映射，调用者可按位组合。 */
	fat_mode |= (flags & FS_O_READ) ? FA_READ : 0;             /* 映射读取权限。 */
	fat_mode |= (flags & FS_O_WRITE) ? FA_WRITE : 0;           /* 映射写入权限。 */
	fat_mode |= (flags & FS_O_CREATE) ? FA_OPEN_ALWAYS : 0;    /* 文件不存在则创建，已存在则保留内容。 */
	/* 追加标志由 fatfs_write 单独处理。每次写都要回到最新观察到的文件尾，仅在打开时移动一次不能满足该约定。 */

	return fat_mode;                                           /* 返回供 f_open 使用的位掩码。 */
}

/**
  * @brief  从固定池取得文件对象，转换路径并按模式打开。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  file_name：[输入]非空、带前导斜线及卷名的文件路径。
  * @param  mode：[输入]公共接口传来的权限和创建标志。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。 申请不到 FIL 返回 -ENOMEM。
  * @note   打开失败会归还池块并清除 filep。
  */
static int fatfs_open(struct fs_file_t *zfp, const char *file_name,
		      fs_mode_t mode)
{
	FRESULT res;                                                        /* 保留 FatFs 库状态，供本方法完成后转换。 */
	uint8_t fs_mode;                                                    /* 保存转换后的 FatFs 打开模式。 */
	void *ptr;                                                          /* 接收从内核固定内存池取得的对象地址。 */

	/* 内核定长池限制可并发打开的文件数，分配不会等待其他线程。 */
	if (k_mem_slab_alloc(&fatfs_filep_pool, &ptr, K_NO_WAIT) == 0) {    /* 从定长文件池立即申请一块 FIL，池空时不阻塞。 */
		(void)memset(ptr, 0, sizeof(FIL));                          /* 清空可能复用的文件对象。 */
		zfp->filep = ptr;                                           /* 将新对象接到 VFS 句柄下。 */
	} else {
		return -ENOMEM;                                             /* 池中没有空闲对象，立即报告内存不足。 */
	}

	fs_mode = translate_flags(mode);                                    /* 把 Zephyr 标志转换为 FatFs 对应位。 */

	/* FatFs 接口需要从卷名开始的路径。 */
	res = f_open(zfp->filep, &file_name[1], fs_mode);                   /* 去掉 VFS 路径最前面的斜线，保留卷名、冒号及后续路径。 */

	if (res != FR_OK) {                                                 /* FatFs 未成功完成请求，进入错误处理。 */
		k_mem_slab_free(&fatfs_filep_pool, &ptr);                   /* 普通打开失败时将已申请的 FIL 还给原池。 */
		zfp->filep = NULL;                                          /* 清除已释放对象的引用。 */
	}

	return translate_error(res);                                        /* 将本次库调用的结果转换为 VFS 状态。 */
}

/**
  * @brief  关闭 FatFs 文件并归还文件对象。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   无论 f_close 是否成功都会释放 FIL，调用者不能继续使用原后端指针。
  */
static int fatfs_close(struct fs_file_t *zfp)
{
	FRESULT res;                                        /* 保留 FatFs 库状态，供本方法完成后转换。 */

	/* 先调用库结束文件访问，再释放适配层保存的对象。 */
	res = f_close(zfp->filep);                          /* 由 FatFs 完成关闭，保存它的状态以便随后转换。 */

	/* 回收本次打开取得的池块，库的关闭状态另行返回。 */
	k_mem_slab_free(&fatfs_filep_pool, &zfp->filep);    /* 无论库关闭成功与否，均归还适配层分配的 FIL。 */
	zfp->filep = NULL;                                  /* 清除适配层文件指针。 */

	return translate_error(res);                        /* 将本次库调用的结果转换为 VFS 状态。 */
}

/**
  * @brief  按 VFS 路径删除 FatFs 目录项。
  * @param  mountp：[输入]VFS 传入的挂载描述，本回调由路径中的卷名选择目标。
  * @param  path：[输入]非空、以斜线开头的卷路径，卷名部分须符合 FatFs 配置。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   仅可写构建提供此操作；只读构建返回 -ENOTSUP。
  */
static int fatfs_unlink(struct fs_mount_t *mountp, const char *path)
{
	int res = -ENOTSUP;                /* 预置不支持，只有启用的实现分支会改写状态。 */

#if !defined(CONFIG_FS_FATFS_READ_ONLY)    /* 仅在构建配置允许写入时编译此分支。 */
	res = f_unlink(&path[1]);          /* 去掉前导斜线，将目标路径交给 FatFs 删除。 */

	res = translate_error(res);        /* 把 FatFs 状态转为 VFS 约定的结果。 */
#endif

	return res;                        /* 返回实际处理结果，未编入的分支保留不支持状态。 */
}

/**
  * @brief  用新路径替换已有名称或移动源目录项。
  * @param  mountp：[输入]VFS 传入的挂载描述，本回调由路径中的卷名选择目标。
  * @param  from：[输入]非空的源绝对路径。
  * @param  to：[输入]非空的目标绝对路径。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   仅可写构建提供此操作；只读构建返回 -ENOTSUP。 已有目标先被删除，随后的改名失败时不会恢复该目标。
  */
static int fatfs_rename(struct fs_mount_t *mountp, const char *from,
			const char *to)
{
	int res = -ENOTSUP;                             /* 预置不支持，只有启用的实现分支会改写状态。 */

#if !defined(CONFIG_FS_FATFS_READ_ONLY)                 /* 仅在构建配置允许写入时编译此分支。 */
	FILINFO fno;                                    /* 接收对目标已有目录项的查询结果。 */

	/* 已有目标先删再改名；删除成功后改名仍可能失败。 */
	res = f_stat(&to[1], &fno);                     /* 先查询新名称是否已经被占用。 */
	if (FR_OK == res) {                             /* 已有目标时进入替换路径。 */
		res = f_unlink(&to[1]);                 /* 删除旧目标，为源目录项的新名称腾出空间。 */
		if (FR_OK != res)                       /* 清理旧目标失败时保留源名称并返回。 */
			return translate_error(res);    /* 将删除失败转换成调用者能识别的错误。 */
	}

	res = f_rename(&from[1], &to[1]);               /* 目标已删除或 f_stat 未成功时尝试改名，使用库的实际结果。 */
	res = translate_error(res);                     /* 把 FatFs 状态转为 VFS 约定的结果。 */
#endif

	return res;                                     /* 返回实际处理结果，未编入的分支保留不支持状态。 */
}

/**
  * @brief  读入指定数量的字节，并传回实际长度。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  ptr：[输出]至少可写 size 字节的目标区。
  * @param  size：[输入]本次最多读取的字节数。
  * @retval 成功为实际读取字节数，读到文件尾可为 0；失败为转换后的负错误码。
  * @note   使用 VFS 已经打开且仍有效的 FIL。
  */
static ssize_t fatfs_read(struct fs_file_t *zfp, void *ptr, size_t size)
{
	FRESULT res;                                 /* 保留 FatFs 库状态，供本方法完成后转换。 */
	unsigned int br;                             /* 由 FatFs 返回实际读取的字节数。 */

	/* 读操作区分库错误与实际传输长度。 */
	res = f_read(zfp->filep, ptr, size, &br);    /* 库负责从当前游标读取，br 接收实际长度。 */
	if (res != FR_OK) {                          /* FatFs 未成功完成请求，进入错误处理。 */
		return translate_error(res);         /* 失败时交付负状态，不能把 br 当成本次成功长度。 */
	}

	return br;                                   /* 可能小于请求值，到文件尾时可以是零。 */
}

/**
  * @brief  将缓冲区写入 FatFs 文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  ptr：[输入]至少可读 size 字节的源数据。
  * @param  size：[输入]计划写入的字节数。
  * @retval 成功返回实际写入字节数，失败为负 errno，支持短写。
  * @note   仅可写构建提供此操作；只读构建返回 -ENOTSUP。 追加模式每次尝试先定位到当前文件末尾。
  */
static ssize_t fatfs_write(struct fs_file_t *zfp, const void *ptr, size_t size)
{
	int res = -ENOTSUP;                                   /* 预置不支持，只有启用的实现分支会改写状态。 */

#if !defined(CONFIG_FS_FATFS_READ_ONLY)                       /* 仅在构建配置允许写入时编译此分支。 */
	unsigned int bw;                                      /* 接收本次实际写入的字节数。 */
	off_t pos = f_size((FIL *)zfp->filep);                /* 在本次调用开始时读取文件尾的字节偏移。 */
	res = FR_OK;                                          /* 未要求追加时可直接进入常规写路径。 */

	/* VFS 的 FS_O_APPEND 要求每次写前重新取文件尾并定位，不能只在打开时定位一次。 */
	if (zfp->flags & FS_O_APPEND) {                       /* 检查 VFS 文件句柄保存的原始追加标志。 */
		res = f_lseek(zfp->filep, pos);               /* 将当前游标移到本次写入前观察到的文件尾。 */
	}

	/* 只有定位成功才写，避免写到调用者没有预期的位置。 */
	if (res == FR_OK) {                                   /* 仅在准备或定位成功时写入数据。 */
		res = f_write(zfp->filep, ptr, size, &bw);    /* 从调用者的缓冲区写入，bw 接收实际写入量。 */
	}

	if (res != FR_OK) {                                   /* FatFs 未成功完成请求，进入错误处理。 */
		res = translate_error(res);                   /* 定位或写入出错时转换错误类型。 */
	} else {
		res = bw;                                     /* 成功时返回实际字节数，调用方可据此识别短写。 */
	}
#endif

	return res;                                           /* 返回实际处理结果，未编入的分支保留不支持状态。 */
}

/**
  * @brief  将 VFS 相对位置换算为文件内的绝对字节偏移。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  offset：[输入]相对所选基点的有符号字节偏移。
  * @param  whence：[输入]FS_SEEK_SET、FS_SEEK_CUR 或 FS_SEEK_END。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。 无效基点或超出边界返回 -EINVAL。
  * @note   允许定位到文件末尾。
  */
static int fatfs_seek(struct fs_file_t *zfp, off_t offset, int whence)
{
	FRESULT res = FR_OK;                                     /* 预设成功，最终采用移动游标的库状态。 */
	off_t pos;                                               /* 保存计算出的文件内绝对字节位置。 */

	/* 将三种定位基点统一换算为绝对位置。 */
	switch (whence) {                                        /* 根据调用方给出的基点换算绝对偏移。 */
	case FS_SEEK_SET:                                        /* 以文件起点为基准。 */
		pos = offset;                                    /* offset 自身就是绝对位置。 */
		break;                                           /* 转入统一范围检查。 */
	case FS_SEEK_CUR:                                        /* 以当前字节游标为基准。 */
		pos = f_tell((FIL *)zfp->filep) + offset;        /* 将相对量加到当前读写游标上。 */
		break;                                           /* 转入统一范围检查。 */
	case FS_SEEK_END:                                        /* 以末尾为基准，往前定位须传负偏移。 */
		pos = f_size((FIL *)zfp->filep) + offset;        /* 将相对量加到当前文件长度上。 */
		break;                                           /* 计算完毕，检查位置是否合法。 */
	default:                                                 /* 基点不是这三种约定之一。 */
		return -EINVAL;                                  /* 返回非法参数，游标未移动。 */
	}

	if ((pos < 0) || (pos > f_size((FIL *)zfp->filep))) {    /* 适配层只接受文件现有内容范围及恰好等于文件尾的位置。 */
		return -EINVAL;                                  /* 拒绝超界偏移，避免普通定位意外扩展文件。 */
	}

	res = f_lseek(zfp->filep, pos);                          /* 将验证后的绝对位置交给 FatFs。 */

	return translate_error(res);                             /* 将本次库调用的结果转换为 VFS 状态。 */
}

/**
  * @brief  读取文件对象内的当前字节游标。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 返回相对文件起始处的偏移，单位字节。
  * @note   直接读取已打开的 FIL。
  */
static off_t fatfs_tell(struct fs_file_t *zfp)
{
	return f_tell((FIL *)zfp->filep);    /* 直接取出 FIL 当前的字节偏移。 */
}

/**
  * @brief  将可写文件截断或扩展至指定长度。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  length：[输入]目标文件长度，单位字节，应为非负值。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   仅可写构建提供此操作；只读构建返回 -ENOTSUP。 快速扩展不写零；可用空间不足时库的实际终点可能小于请求值。
  */
static int fatfs_truncate(struct fs_file_t *zfp, off_t length)
{
	int res = -ENOTSUP;                                       /* 预置不支持，只有启用的实现分支会改写状态。 */

#if !defined(CONFIG_FS_FATFS_READ_ONLY)                           /* 仅在构建配置允许写入时编译此分支。 */
	off_t cur_length = f_size((FIL *)zfp->filep);             /* 记录改动前的文件长度，用来区分扩展与缩短。 */

#ifdef FATFS_QUICK_TRUNCATE                                       /* 编译快速处理分支时，允许等长请求直接返回。 */
	/* 等长且启用快速路径时，保持已有游标并免去介质操作。 */
	if (cur_length == length) {                               /* 新旧长度相同，无需移动游标或写盘。 */
		return FR_OK;                                     /* FR_OK 为零，直接表示成功。 */
	}
#endif

	/* 在可写文件上，FatFs 首先尝试把游标移到新长度，超出原末尾时可能扩展簇链。 */
	res = f_lseek(zfp->filep, length);                        /* 先移到目标字节位置；对可写文件，向后越过末尾可触发扩容。 */
	if (res != FR_OK) {                                       /* FatFs 未成功完成请求，进入错误处理。 */
		return translate_error(res);                      /* 定位或扩容出错时停止操作。 */
	}

	/* 缩短文件调用截断接口，扩展文件按编译配置决定是否填零。 */
	if (length < cur_length) {                                /* 新长度小于原有长度时才需要释放文件尾部。 */
		res = f_truncate(zfp->filep);                     /* 从当前游标截断文件。 */
	} else {
#ifdef FATFS_QUICK_TRUNCATE                                       /* 本文件启用的快速扩展路径。 */
		/* 快速分支不覆盖新分配的区域，只请求库同步文件状态。 */
		 res = f_sync(zfp->filep);                        /* 同步扩展后的簇链及元数据，新区内容未在本分支填零。 */
#else                                                             /* 未启用快速路径时，使用逐字节填零分支。 */
		/* 普通扩展分支按实际新长度填零，避免空间不足时越过实际文件尾。 */
		length = f_tell((FIL *)zfp->filep);               /* 以实际到达的位置为终点，空间不足时它可能小于请求长度。 */

		res = f_lseek(zfp->filep, cur_length);            /* 回到原文件末尾开始处理新区域。 */
		if (res != FR_OK) {                               /* FatFs 未成功完成请求，进入错误处理。 */
			return translate_error(res);              /* 回定位失败时直接交付转换后的错误。 */
		}

		/* 复用 FatFs 的写缓存，从一个零字节逐次填充以避免额外大缓冲。 */
		unsigned int bw;                                  /* 接收单次写入字节数。 */
		uint8_t c = 0U;                                   /* 作为填充源的一个零字节。 */

		for (int i = cur_length; i < length; i++) {       /* 在旧文件尾与实际新尾之间依次写入。 */
			res = f_write(zfp->filep, &c, 1, &bw);    /* 每次交给 FatFs 一个零字节。 */
			if (res != FR_OK) {                       /* FatFs 未成功完成请求，进入错误处理。 */
				break;                            /* 一旦 FatFs 报错就停止填充。 */
			}
		}
#endif
	}

	res = translate_error(res);                               /* 把 FatFs 状态转为 VFS 约定的结果。 */
#endif

	return res;                                               /* 返回实际处理结果，未编入的分支保留不支持状态。 */
}

/**
  * @brief  将 FatFs 文件的待写数据同步到设备。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   仅可写构建提供此操作；只读构建返回 -ENOTSUP。
  */
static int fatfs_sync(struct fs_file_t *zfp)
{
	int res = -ENOTSUP;                /* 预置不支持，只有启用的实现分支会改写状态。 */

#if !defined(CONFIG_FS_FATFS_READ_ONLY)    /* 仅在构建配置允许写入时编译此分支。 */
	res = f_sync(zfp->filep);          /* 把该文件的缓存和必要元数据同步到介质。 */
	res = translate_error(res);        /* 把 FatFs 状态转为 VFS 约定的结果。 */
#endif
	return res;                        /* 返回实际处理结果，未编入的分支保留不支持状态。 */
}

/**
  * @brief  在 FatFs 卷中创建一级目录项。
  * @param  mountp：[输入]VFS 传入的挂载描述，本回调由路径中的卷名选择目标。
  * @param  path：[输入]非空、以斜线开头的卷路径，卷名部分须符合 FatFs 配置。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   仅可写构建提供此操作；只读构建返回 -ENOTSUP。
  */
static int fatfs_mkdir(struct fs_mount_t *mountp, const char *path)
{
	int res = -ENOTSUP;                /* 预置不支持，只有启用的实现分支会改写状态。 */

#if !defined(CONFIG_FS_FATFS_READ_ONLY)    /* 仅在构建配置允许写入时编译此分支。 */
	res = f_mkdir(&path[1]);           /* 将含卷名的路径交给 FatFs 建立目录。 */
	res = translate_error(res);        /* 把 FatFs 状态转为 VFS 约定的结果。 */
#endif

	return res;                        /* 返回实际处理结果，未编入的分支保留不支持状态。 */
}

/**
  * @brief  分配目录池块并打开指定目录。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  path：[输入]非空、以斜线开头的卷路径，卷名部分须符合 FatFs 配置。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。 目录池已满返回 -ENOMEM。
  * @note   普通打开失败会归还 DIR；VFS 的虚拟根目录由上层另行处理。
  */
static int fatfs_opendir(struct fs_dir_t *zdp, const char *path)
{
	FRESULT res;                                                       /* 保留 FatFs 库状态，供本方法完成后转换。 */
	void *ptr;                                                         /* 接收从内核固定内存池取得的对象地址。 */

	/* 从与 FIL 独立的目录池取一个游标。 */
	if (k_mem_slab_alloc(&fatfs_dirp_pool, &ptr, K_NO_WAIT) == 0) {    /* 从目录池立即申请一个 DIR 对象。 */
		(void)memset(ptr, 0, sizeof(DIR));                         /* 清空复用的目录对象。 */
		zdp->dirp = ptr;                                           /* 把后端游标接到 VFS 目录句柄上。 */
	} else {
		return -ENOMEM;                                            /* 池中没有空闲对象，立即报告内存不足。 */
	}

	res = f_opendir(zdp->dirp, &path[1]);                              /* 去掉最前面的斜线并打开卷内目录。 */

	if (res != FR_OK) {                                                /* FatFs 未成功完成请求，进入错误处理。 */
		k_mem_slab_free(&fatfs_dirp_pool, &ptr);                   /* 普通目录打开失败时归还先前申请的池块。 */
		zdp->dirp = NULL;                                          /* 清掉不再有效的后端游标。 */
	}

	return translate_error(res);                                       /* 将本次库调用的结果转换为 VFS 状态。 */
}

/**
  * @brief  从 FatFs 目录游标读取一项并转换为公共结构。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  entry：[输出]非空、可写的目录项，用于接收名称、类型和长度。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   遍历结束时 entry->name 首字节为零，此时不更新类型和长度。
  */
static int fatfs_readdir(struct fs_dir_t *zdp, struct fs_dirent *entry)
{
	FRESULT res;                                                     /* 保留 FatFs 库状态，供本方法完成后转换。 */
	FILINFO fno;                                                     /* 暂存 FatFs 提供的一个目录项。 */

	/* 从库目录项映射到 VFS 结构，保留空名结束约定。 */
	res = f_readdir(zdp->dirp, &fno);                                /* FatFs 推进目录游标，同时填入元素名称、属性和长度。 */
	if (res == FR_OK) {                                              /* 成功时才使用库返回的目录项。 */
		strcpy(entry->name, fno.fname);                          /* 复制以零终止的文件名，空名称传递结束标记。 */
		if (entry->name[0] != 0) {                               /* 仅在实际存在目录项时更新类型和长度。 */
			entry->type = ((fno.fattrib & AM_DIR) ?          /* 检查 FAT 的目录属性位。 */
			       FS_DIR_ENTRY_DIR : FS_DIR_ENTRY_FILE);    /* 把目录位映射成公共文件或目录类型。 */
			entry->size = fno.fsize;                         /* 复制库报告的文件长度，单位字节。 */
		}
	}

	return translate_error(res);                                     /* 将本次库调用的结果转换为 VFS 状态。 */
}

/**
  * @brief  关闭后端目录并交还游标内存。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   即使 f_closedir 失败也会归还池块。
  */
static int fatfs_closedir(struct fs_dir_t *zdp)
{
	FRESULT res;                                      /* 保留 FatFs 库状态，供本方法完成后转换。 */

	res = f_closedir(zdp->dirp);                      /* 让 FatFs 关闭目录，保留状态供对外返回。 */

	/* 目录使用独立于文件池的固定内存池。 */
	k_mem_slab_free(&fatfs_dirp_pool, &zdp->dirp);    /* 即使库关闭出错，此处也归还已分配的 DIR。 */

	return translate_error(res);                      /* 将本次库调用的结果转换为 VFS 状态。 */
}

/**
  * @brief  取得对象类型、名称及长度。
  * @param  mountp：[输入]VFS 传入的挂载描述，本回调由路径中的卷名选择目标。
  * @param  path：[输入]非空、以斜线开头的卷路径，卷名部分须符合 FatFs 配置。
  * @param  entry：[输出]非空、可写的目录项，用于接收名称、类型和长度。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   输出名称使用库返回的文件名，缓冲区必须容纳该名称及终止字符。
  */
static int fatfs_stat(struct fs_mount_t *mountp,
		      const char *path, struct fs_dirent *entry)
{
	FRESULT res;                                                     /* 保留 FatFs 库状态，供本方法完成后转换。 */
	FILINFO fno;                                                     /* 接收指定对象的 FatFs 元数据。 */

	/* 只有查询成功才可将库输出复制给调用者。 */
	res = f_stat(&path[1], &fno);                                    /* 去掉最前面的斜线后按卷内路径查询。 */
	if (res == FR_OK) {                                              /* 查询成功才读取 FILINFO。 */
		entry->type = ((fno.fattrib & AM_DIR) ?                  /* 从 FAT 属性字节中提取目录标志。 */
			       FS_DIR_ENTRY_DIR : FS_DIR_ENTRY_FILE);    /* 转为 VFS 目录项类型。 */
		strcpy(entry->name, fno.fname);                          /* 将库返回的名称复制到调用者的目录项。 */
		entry->size = fno.fsize;                                 /* 复制文件长度，单位字节。 */
	}

	return translate_error(res);                                     /* 将本次库调用的结果转换为 VFS 状态。 */
}

/**
  * @brief  查询卷的总簇数、空闲簇数和字节单位。
  * @param  mountp：[输入]非空的挂载描述，提供带前导斜线的卷名。
  * @param  path：[输入]VFS 提供的查询路径；此实现依据挂载点选卷。
  * @param  stat：[输出]非空、可写的容量结构，各字段单位见 struct fs_statvfs。
  * @retval 0 为成功，f_getfree 失败返回 -EIO，只读构建返回 -ENOTSUP。
  * @note   总容量和空闲容量分别用 f_blocks 和 f_bfree 乘以 f_frsize 换算为字节。
  */
static int fatfs_statvfs(struct fs_mount_t *mountp,
			 const char *path, struct fs_statvfs *stat)
{
	int res = -ENOTSUP;                                             /* 预置不支持，只有启用的实现分支会改写状态。 */
#if !defined(CONFIG_FS_FATFS_READ_ONLY)                                 /* 仅在构建配置允许写入时编译此分支。 */
	FATFS *fs;                                                      /* f_getfree 除输出空闲簇数外，还返回卷工作区。 */

	/* 先取空闲簇数和卷对象，再从卷布局换算分配单位。 */
	res = f_getfree(&mountp->mnt_point[1], &stat->f_bfree, &fs);    /* 按挂载名查询空闲簇数，直接写入 f_bfree。 */
	if (res != FR_OK) {                                             /* FatFs 未成功完成请求，进入错误处理。 */
		return -EIO;                                            /* 查询失败统一报 I/O 错误，其他容量字段未填。 */
	}

	/* f_bsize 按配置的 _MIN_SS 取值；容量与空闲量均按簇计数。 */
	stat->f_bsize = _MIN_SS;                                        /* 取 FatFs 构建配置的最小扇区字节数作为传输块大小。 */
	stat->f_frsize = fs->csize * stat->f_bsize;                     /* 簇内扇区数乘传输块大小，得到分配单位的字节数。 */
	stat->f_blocks = (fs->n_fatent - 2);                            /* FAT 表头保留两个项，减去后得到数据簇总数。 */

	res = translate_error(res);                                     /* 把 FatFs 状态转为 VFS 约定的结果。 */
#endif
	return res;                                                     /* 返回实际处理结果，未编入的分支保留不支持状态。 */
}

/**
  * @brief  挂载指定 FAT 卷，在配置及挂载标志允许时建立文件系统。
  * @param  mountp：[输入/输出]非空，fs_data 指向可写 FATFS 卷对象，mnt_point 提供卷名，flags 控制格式化。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。 允许自动建卷的构建遇到无文件系统且只读的卷会返回 -EROFS。
  * @note   格式化仅在明确报告没有文件系统且允许写入和格式化时执行。
  */
static int fatfs_mount(struct fs_mount_t *mountp)
{
	FRESULT res;                                                          /* 保留 FatFs 库状态，供本方法完成后转换。 */

	/* 首先直接挂载，保留首轮返回值决定是否满足建卷条件。 */
	res = f_mount((FATFS *)mountp->fs_data, &mountp->mnt_point[1], 1);    /* 由调用者持有 FATFS 工作区，立即尝试识别挂载名对应的卷。 */

#if defined(CONFIG_FS_FATFS_MOUNT_MKFS)                                       /* 仅在允许挂载时建卷的配置中编译自动格式化逻辑。 */
	if (res == FR_NO_FILESYSTEM &&                                        /* 首次挂载明确报告无文件系统时才考虑建卷。 */
	    (mountp->flags & FS_MOUNT_FLAG_READ_ONLY) != 0) {                 /* 只读挂载不得在空白介质上写入格式信息。 */
		return -EROFS;                                                /* 报告只读限制。 */
	}
	/* 只在缺少文件系统并获准格式化时创建新的 FAT 卷。 */
	if (res == FR_NO_FILESYSTEM &&                                        /* 介质无有效文件系统且允许建卷时才进入格式化。 */
	    (mountp->flags & FS_MOUNT_FLAG_NO_FORMAT) == 0) {                 /* 挂载描述可单独禁止自动格式化。 */
		uint8_t work[_MAX_SS];                                        /* 按 FatFs 支持的最大扇区大小预留临时栈缓冲区。 */

		res = f_mkfs(&mountp->mnt_point[1],                           /* 把去掉前导斜线的卷名传给格式化接口。 */
				(FM_FAT | FM_SFD), 0, work, sizeof(work));    /* 采用 FAT 与无分区表的 SFD 方式，分配单元由库选择。 */
		if (res == FR_OK) {                                           /* 格式化成功后再重新挂载，失败则直接上报。 */
			res = f_mount((FATFS *)mountp->fs_data,               /* 继续使用原先的 FATFS 卷对象尝试挂载。 */
					&mountp->mnt_point[1], 1);            /* 立即检查刚写入的卷格式。 */
		}
	}
#endif                                                                        /* CONFIG_FS_FATFS_MOUNT_MKFS */

	return translate_error(res);                                          /* 将本次库调用的结果转换为 VFS 状态。 */

}

/**
  * @brief  撤销 FatFs 对指定逻辑卷的挂载。
  * @param  mountp：[输入]非空的挂载描述，提供带前导斜线的卷名。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。
  * @note   由上层在成功后移除其 VFS 挂载节点。
  */
static int fatfs_unmount(struct fs_mount_t *mountp)
{
	FRESULT res;                                      /* 保留 FatFs 库状态，供本方法完成后转换。 */

	res = f_mount(NULL, &mountp->mnt_point[1], 0);    /* 传入空卷对象，撤销 FatFs 对指定卷的绑定。 */

	return translate_error(res);                      /* 将本次库调用的结果转换为 VFS 状态。 */
}
/* 项目扩展通过逻辑卷号调用下层 DISK_HW_DETECT。 */
/**
  * @brief  请求驱动检测指定卷对应介质的当前状态。
  * @param  mountp：[输入]VFS 传入的挂载描述，本回调由路径中的卷名选择目标。
  * @param  path：[输入]非空、以斜线开头的卷路径，卷名部分须符合 FatFs 配置。
  * @param  state：[输出]可写的单字节状态，由底层驱动填写。
  * @retval 直接返回 FatFs 的 FRESULT，FR_OK 为成功，FR_INVALID_DRIVE 或 FR_DISK_ERR 表示失败。
  * @note   此项目扩展直接传回库状态，调用方应判断是否等于 FR_OK。
  */
static int fatfs_disk_detect(struct fs_mount_t *mountp, const char *path, uint8_t *state)
{
	return f_disk_detect(path + 1, state);    /* 跳过前导斜线，原样返回 FRESULT，失败时为非零库状态而非负 errno。 */
}

/**
  * @brief  按目录起始簇与目录项偏移打开一个文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  dir：[输入]带前导斜线及 FatFs 卷名的目录路径。
  * @param  cluster：[输入]FAT 目录的起始簇号。
  * @param  blk_ofs：[输入]目录内的字节偏移，须落在有效目录项边界。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。 池空返回 -ENOMEM。
  * @note   扩展固定传入 FA_READ、FA_WRITE 和 FA_OPEN_ALWAYS。库打开失败时此路径没有归还申请的 FIL。
  */
static int fatfs_open_cluster(struct fs_file_t *zfp, char *dir, uint32_t cluster, uint32_t blk_ofs)
{
	FRESULT res;                                                             /* 保留 FatFs 库状态，供本方法完成后转换。 */
	uint8_t fs_mode;                                                         /* 保存按簇打开使用的固定 FatFs 标志。 */
	void *ptr;                                                               /* 接收从内核固定内存池取得的对象地址。 */

	/* 簇定位路径使用与普通文件打开相同的 FIL 池。 */
	if (k_mem_slab_alloc(&fatfs_filep_pool, &ptr, K_NO_WAIT) == 0) {         /* 从固定文件池获取按簇打开所需的 FIL。 */
		(void)memset(ptr, 0, sizeof(FIL));                               /* 清空分配到的文件对象。 */
		zfp->filep = ptr;                                                /* 保存后端指针以供扩展接口填入。 */
	} else {
		return -ENOMEM;                                                  /* 池中没有空闲对象，立即报告内存不足。 */
	}

	fs_mode = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;                           /* 固定请求读写与 FA_OPEN_ALWAYS，具体按目录项坐标定位。 */

	res = f_open_cluster(zfp->filep, dir + 1, cluster, blk_ofs, fs_mode);    /* 去掉路径前导斜线，把簇号、目录内字节偏移和模式交给项目 FatFs 扩展。 */

	return translate_error(res);                                             /* 此扩展直接返回库状态转换结果，库失败时没有归还刚分配的 FIL。 */
}

/**
  * @brief  建立从目录簇与指定偏移开始的 FatFs 目录游标。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  path：[输入]非空、以斜线开头的卷路径，卷名部分须符合 FatFs 配置。
  * @param  cluster：[输入]FAT 目录的起始簇号。
  * @param  blk_ofs：[输入]目录内的字节偏移，须落在有效目录项边界。
  * @retval 成功返回 0，FatFs 失败时返回 translate_error 转换的负 errno。 池空返回 -ENOMEM。
  * @note   扩展失败时本分支保留已申请的 DIR。
  */
static int fatfs_opendir_cluster(struct fs_dir_t *zdp, const char *path, unsigned int cluster, unsigned int blk_ofs)
{
	FRESULT res;                                                       /* 保留 FatFs 库状态，供本方法完成后转换。 */
	void *ptr;                                                         /* 接收从内核固定内存池取得的对象地址。 */

	/* 从目录池取得游标，由 FatFs 扩展设置起始簇与当前位置。 */
	if (k_mem_slab_alloc(&fatfs_dirp_pool, &ptr, K_NO_WAIT) == 0) {    /* 从固定目录池申请用于按簇遍历的 DIR。 */
		(void)memset(ptr, 0, sizeof(DIR));                         /* 清空申请到的目录游标。 */
		zdp->dirp = ptr;                                           /* 把 DIR 绑定到 VFS 目录对象。 */
	} else {
		return -ENOMEM;                                            /* 池中没有空闲对象，立即报告内存不足。 */
	}

	res = f_opendir_cluster(zdp->dirp, path + 1, cluster, blk_ofs);    /* 去掉路径前导斜线，由扩展接口按目录起始簇和字节偏移设置游标。 */

	return translate_error(res);                                       /* 扩展调用失败时本分支也不归还已申请的 DIR，返回转换后的状态。 */
}

/* VFS 通过挂载项持有这张常量表，打开、读写、枚举、容量查询和项目扩展分别分派到下面的方法。 */
static const struct fs_file_system_t fatfs_fs = {
	.open = fatfs_open,                          /* 申请 FIL 并按路径打开。 */
	.close = fatfs_close,                        /* 关闭文件并归还池块。 */
	.read = fatfs_read,                          /* 从当前游标读取数据。 */
	.write = fatfs_write,                        /* 按写入或追加模式写入。 */
	.lseek = fatfs_seek,                         /* 换算并更新字节游标。 */
	.tell = fatfs_tell,                          /* 取得当前字节位置。 */
	.truncate = fatfs_truncate,                  /* 缩短或扩展文件长度。 */
	.sync = fatfs_sync,                          /* 同步已打开文件。 */
	.opendir = fatfs_opendir,                    /* 建立真实目录游标。 */
	.readdir = fatfs_readdir,                    /* 读取并转换一个目录项。 */
	.closedir = fatfs_closedir,                  /* 结束目录遍历。 */
	.mount = fatfs_mount,                        /* 挂载或按条件建立卷。 */
	.unmount = fatfs_unmount,                    /* 解除卷挂载。 */
	.unlink = fatfs_unlink,                      /* 删除文件或目录。 */
	.rename = fatfs_rename,                      /* 替换名称或移动目录项。 */
	.mkdir = fatfs_mkdir,                        /* 创建子目录。 */
	.stat = fatfs_stat,                          /* 查询单个对象的元数据。 */
	.statvfs = fatfs_statvfs,                    /* 以簇计数查询卷容量。 */
	.disk_detect = fatfs_disk_detect,            /* 按项目扩展检测介质状态。 */
	.open_cluster = fatfs_open_cluster,          /* 按簇号和目录项偏移打开文件。 */
	.opendir_cluster = fatfs_opendir_cluster,    /* 按簇号和偏移建立目录游标。 */
};

/**
  * @brief  在启动时把 FatFs 操作表登记到 VFS。
  * @param  dev：[输入]由系统初始化框架传入，本实现不读取。
  * @retval 返回 fs_register 的结果，0 为成功，-EALREADY 或 -ENOSPC 表示注册失败。
  * @note   操作表具有静态生命周期，供随后挂载的各 FatFs 卷共享。
  */
static int fatfs_init(const struct device *dev)
{
	ARG_UNUSED(dev);                            /* 该注册没有独立的硬件设备实例，启动框架参数无需读取。 */

	return fs_register(FS_FATFS, &fatfs_fs);    /* 以 FS_FATFS 类型号登记此操作表，此时不挂载具体卷。 */
}

SYS_INIT(fatfs_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);    /* 在 APPLICATION 阶段注册 FatFs 后端，随后业务可通过 VFS 挂载卷。 */
