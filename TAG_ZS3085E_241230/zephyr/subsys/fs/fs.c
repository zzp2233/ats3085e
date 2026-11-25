/*
 * Copyright (c) 2018 Intel Corporation.
 * Copyright (c) 2020 Peter Bigot Consulting, LLC
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Zephyr 虚拟文件系统核心。先注册后端类型，再将卷加入挂载表；按绝对路径找卷后，文件与目录操作经卷的函数指针调用适配层。 */
#include <stdio.h>           /* 导入基础输入输出类型。 */
#include <string.h>          /* 用于挂载前缀、目录项名称及字符串比较。 */
#include <zephyr/types.h>    /* 提供 Zephyr 的定宽数据类型。 */
#include <errno.h>           /* 向调用方返回负的 errno 状态。 */
#include <init.h>            /* 提供内核初始化入口注册宏。 */
#include <fs/fs.h>           /* 文件、目录、挂载结构及公共函数声明。 */
#include <fs/fs_sys.h>       /* 定义挂载后端的操作表及各回调签名。 */
#include <sys/check.h>       /* CHECKIF 的行为受构建时的检查配置控制。 */
#include <sys/stat.h>        /* 导入文件状态相关的公共定义。 */


#define LOG_LEVEL CONFIG_FS_LOG_LEVEL    /* 按文件系统配置选择日志级别。 */
#include <logging/log.h>
LOG_MODULE_REGISTER(fs);    /* 将路径分派、挂载和后端错误归入 fs 日志模块。 */

/* 挂载表只包含后端已成功挂载的卷；VFS 虚拟根目录也遍历这张表。 */
static sys_dlist_t fs_mnt_list;

/* 同一互斥锁保护挂载表与类型注册表；一般的文件读写直接使用已绑定的操作表。 */
static struct k_mutex mutex;

/* 注册项把类型编号映射到操作表，卷的实例在另一个挂载链表中管理。 */
struct registry_entry {
	int type;                               /* 调用方注册的唯一类型编号。 */
	const struct fs_file_system_t *fstp;    /* 此指针为空表示空槽，非空时指向该类型的分派表。 */
};
static struct registry_entry registry[CONFIG_FILE_SYSTEM_MAX_TYPES];    /* 元素数限制可同时注册的后端类型数，与挂载卷数独立。 */

/**
  * @brief  清空一个注册项的有效标记。
  * @param  ep：[输入/输出]非空的已占用注册项。
  * @retval 无。
  * @note   调用方持注册表锁。
  */
static inline void registry_clear_entry(struct registry_entry *ep)
{
	ep->fstp = NULL;    /* 只清方法表指针即释放注册槽，旧编号随槽失效。 */
}

/**
  * @brief  把后端及类型写入首个空槽。
  * @param  type：[输入]注册表内唯一的文件系统类型编号。
  * @param  fstp：[输入]非空、存续到注销的后端方法表。
  * @retval 0 为成功，-ENOSPC 为没有空槽。
  * @note   调用方在锁内检查重复类型。
  */
static int registry_add(int type,
			const struct fs_file_system_t *fstp)
{
	int rv = -ENOSPC;                                      /* 默认报告注册表已满，找到空槽才改为成功。 */

	/* 找到一个空的类型槽位并一次填写编号和方法表。 */
	for (size_t i = 0; i < ARRAY_SIZE(registry); ++i) {    /* 从头扫描寻找第一个未占用的槽。 */
		struct registry_entry *ep = &registry[i];      /* 取得当前可读写的注册项。 */

		if (ep->fstp == NULL) {                        /* 空的方法表指针是槽可用的判据。 */
			ep->type = type;                       /* 保存本次申请注册的类型编号。 */
			ep->fstp = fstp;                       /* 绑定在注册期间保持有效的操作表指针。 */
			rv = 0;                                /* 标记登记成功。 */
			break;                                 /* 已得到空槽，停止扫描。 */
		}
	}

	return rv;                                             /* 向注册接口报告成功或容量不足。 */
}

/**
  * @brief  按类型定位有效的注册槽。
  * @param  type：[输入]注册表内唯一的文件系统类型编号。
  * @retval 找到时返回注册项的借用指针，找不到返回 NULL。
  * @note   在持锁期间读取并使用该项。
  */
static struct registry_entry *registry_find(int type)
{
	for (size_t i = 0; i < ARRAY_SIZE(registry); ++i) {        /* 扫描固定容量的注册表。 */
		struct registry_entry *ep = &registry[i];          /* 取出本轮待检查的槽。 */

		if ((ep->fstp != NULL) && (ep->type == type)) {    /* 只有有效槽中的相同编号才构成命中。 */
			return ep;                                 /* 返回注册表中该项的地址。 */
		}
	}
	return NULL;                                               /* 扫描结束仍无该类型。 */
}

/**
  * @brief  取得给定类型的后端操作表。
  * @param  type：[输入]注册表内唯一的文件系统类型编号。
  * @retval 存在时返回已注册的方法表，未登记时返回 NULL。
  * @note   由持有互斥锁的挂载或类型管理路径调用。
  */
static const struct fs_file_system_t *fs_type_get(int type)
{
	struct registry_entry *ep = registry_find(type);    /* 查找有效的类型注册项。 */

	return (ep != NULL) ? ep->fstp : NULL;              /* 取得实际的回调表，类型未注册时返回空指针。 */
}

/**
  * @brief  为绝对路径选择匹配长度最大的挂载点。
  * @param  mnt_pntp：[输出]非空，接收借用的挂载描述。
  * @param  name：[输入]非空且以零终止的绝对路径。
  * @param  match_len：[输出]可为 NULL，非空时接收匹配前缀的字节数。
  * @retval 0 为匹配成功，-ENOENT 为未找到卷。
  * @note   在互斥锁内检索链表，返回的挂载描述仍由创建者持有。
  */
static int fs_get_mnt_point(struct fs_mount_t **mnt_pntp,
			    const char *name, size_t *match_len)
{
	struct fs_mount_t *mnt_p = NULL, *itr;                                   /* 保存当前最佳匹配与本轮候选卷。 */
	size_t longest_match = 0;                                                /* 记录已经匹配的最长路径前缀长度。 */
	size_t len, name_len = strlen(name);                                     /* 预先取得输入路径长度，限制后续比较范围。 */
	sys_dnode_t *node;                                                       /* 遍历由各卷内嵌节点组成的挂载表。 */

	/* 挂载节点随挂载和卸载变化；锁内完成前缀选择。 */
	k_mutex_lock(&mutex, K_FOREVER);                                         /* 等待并持有互斥锁，防止并发更改共享表。 */
	SYS_DLIST_FOR_EACH_NODE(&fs_mnt_list, node) {                            /* 沿挂载双向链表逐卷检查。 */
		itr = CONTAINER_OF(node, struct fs_mount_t, node);               	/* 从链表节点取回所属 fs_mount_t。 */
		len = itr->mountp_len;                                           	/* 使用挂载成功时缓存的长度。 */

		/* 保留最长的有效前缀；还须保证输入足够长，才能读取对应的边界字符。 */
		if ((len < longest_match) || (len > name_len)) {                 	/* 剪掉过短的候选前缀和超出输入路径的候选。 */
			continue;                                                		/* 继续检查后面的挂载项。 */
		}

		/* 在完整路径分量处匹配，例如 /NOR: 不会匹配 /NOR:abc。 */
		if ((len > 1) && (name[len] != '/') && (name[len] != '\0')) {    	/* 要求卷名后面紧接分隔符或字符串结尾。 */
			continue;                                                		/* 跳过只匹配到目录名称一部分的候选项。 */
		}

		/* 通过长度和目录边界后，再核对挂载名内容。 */
		if (strncmp(name, itr->mnt_point, len) == 0) {                      /* 按候选挂载名的实际字节数比较。 */
			mnt_p = itr;                                             		/* 保存当前最具体的匹配卷。 */
			longest_match = len;                                     		/* 之后只接受同样长或更长的有效匹配。 */
		}
	}
	k_mutex_unlock(&mutex);                                                  /* 结束本次受保护的共享表访问。 */

	if (mnt_p == NULL) {                                                     /* 没有任何挂载路径匹配输入。 */
		return -ENOENT;                                                  	/* 通知调用方路径无法分派。 */
	}

	*mnt_pntp = mnt_p;                                                       /* 交付挂载描述的借用指针。 */
	if (match_len)                                                           /* 调用者可选取回匹配的字节数。 */
		*match_len = mnt_p->mountp_len;                                  	/* 源卷前缀长度供重命名等调用者使用。 */

	return 0;                                                                /* 找到可供调用者分派的卷。 */
}

/* 文件操作：先在打开时选卷，后续通过已绑定的方法表分派。 */
/**
  * @brief  按路径选择挂载卷并打开文件，需要时创建。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  file_name：[输入]以 / 开头、含文件名的零终止字符串。
  * @param  flags：[输入]读、写、创建和追加标志的按位组合，也可为 0，以无读写权限尝试打开已有文件。
  * @retval 0 表示成功。-EINVAL 路径无效，-EBUSY 对象已打开，-EROFS 只读卷拒绝修改，-ENOENT 未匹配挂载点，后端不可用或操作失败返回相应负值。
  * @note   首次使用先初始化对象。FS_O_CREATE 保留已有文件；FS_O_APPEND 要求每次写入前定位到末尾。
  */
int fs_open(struct fs_file_t *zfp, const char *file_name, fs_mode_t flags)
{
	struct fs_mount_t *mp;                                                  /* 接收按输入路径选出的挂载描述。 */
	int rc = -EINVAL;                                                       /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 先保存原始标志，以便后端在后续调用中实现追加等语义。 */
	zfp->flags = flags;                                                     /* 先记录访问模式，FatFs 在追加写入时重新检查该标志。 */

	/* 文件名和句柄必须适合一次新的打开操作。 */
	if ((file_name == NULL) ||                                              /* 先排除空指针，再检查输入路径格式。 */
			(strlen(file_name) <= 1) || (file_name[0] != '/')) {    		/* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                                 	/* 记录路径或参数校验失败。 */
		return -EINVAL;                                                 	/* 返回路径或参数无效。 */
	}

	if (zfp->mp != NULL) {                                                  /* 已绑定卷的文件对象不可重复打开。 */
		return -EBUSY;                                                  	/* 用另一个句柄或先成功关闭已有文件。 */
	}

	/* 确定卷并检查本次请求的写权限。 */
	rc = fs_get_mnt_point(&mp, file_name, NULL);                            /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                           /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("mount point not found!!");                             	/* 记录未匹配到挂载路径。 */
		return rc;                                                      	/* 保留路径查找的失败原因。 */
	}

	if (((mp->flags & FS_MOUNT_FLAG_READ_ONLY) != 0) &&                     /* 检查卷是否只读，以及调用方是否请求创建或写入。 */
	    (flags & FS_O_CREATE || flags & FS_O_WRITE)) {                      /* 这两类请求均可能改变介质内容。 */
		return -EROFS;                                                  	/* 以只读文件系统错误拒绝本次修改。 */
	}

	CHECKIF(mp->fs->open == NULL) {                                         /* 启用接口检查时验证后端已提供 open 方法。 */
		return -ENOTSUP;                                                	/* 后端缺少对应方法，返回操作不支持。 */
	}

	/* 先绑定挂载点供后端使用，失败时撤销普通路径的临时绑定。 */
	zfp->mp = mp;                                                           /* 后端打开期间即可通过对象访问所属卷。 */
	rc = mp->fs->open(zfp, file_name, flags);                               /* 由绑定的后端按绝对路径打开或创建文件。 */
	if (rc < 0) {                                                           /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file open error (%d)", rc);                            /* 记录本次操作的错误，便于结合调用状态诊断。 */
		zfp->mp = NULL;                                                 /* 普通打开失败后清回挂载指针。 */
		return rc;                                                      /* 句柄已解除临时绑定，将后端打开错误返回。 */
	}

	return rc;                                                              /* 后端打开成功，调用者可使用文件句柄。 */
}

/**
  * @brief  按目录簇号和目录项偏移打开文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  dir：[输入]非空、以斜线开头的卷或目录路径，用于选择挂载点。
  * @param  cluster：[输入]FAT 目录的起始簇号。
  * @param  blk_ofs：[输入]目录内的字节偏移，须落在有效目录项边界。
  * @retval -EINVAL 为无效路径，-ENOENT 为无匹配卷；已安装扩展且调用成功返回 0，失败返回后端错误码。
  * @note   匹配卷后先填 mp；缺少扩展时会沿用匹配成功的 0 而未打开后端对象；后端失败时也保留 mp。
  */
int fs_open_cluster(struct fs_file_t *zfp, char *dir, uint32_t cluster, uint32_t blk_ofs)
{
	struct fs_mount_t *mp;                                                 /* 接收按输入路径选出的挂载描述。 */
	int rc = -EINVAL;                                                      /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	if ((dir == NULL) ||                                                   /* 先排除空指针，再检查输入路径格式。 */
			(strlen(dir) <= 1) || (dir[0] != '/')) {               /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                                /* 记录路径或参数校验失败。 */
		return -EINVAL;                                                /* 返回路径或参数无效。 */
	}

	/* 用路径选卷，簇位置留给该卷后端解释。 */
	rc = fs_get_mnt_point(&mp, dir, NULL);                                 /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                          /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("%s:mount point not found!!", __func__);               /* 记录未匹配到挂载路径。 */
		return rc;                                                     /* 没有匹配卷，无法解释传入的目录项坐标。 */
	}

	zfp->mp = mp;                                                          /* 簇定位扩展先写入所属卷，失败时本分支不回滚。 */

	if (zfp->mp->fs->open_cluster != NULL) {                               /* 方法存在才按簇定位；没有该方法时沿用查卷结果。 */
		rc = zfp->mp->fs->open_cluster(zfp, dir, cluster, blk_ofs);    /* 将原目录路径、起始簇和字节偏移交给项目扩展。 */
		if (rc < 0) {                                                  /* 负值表示调用失败，进入相应错误路径。 */
			LOG_ERR("file open error (%d)", rc);                   /* 记录本次操作的错误，便于结合调用状态诊断。 */
			return rc;                                             /* 按簇打开失败；前端保留了已设置的挂载指针。 */
		}
	}

	return rc;                                                             /* 返回最后一次实际执行的查询或后端调用结果。 */

}

/**
  * @brief  经已绑定的卷关闭文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 0 表示已关闭或本来未打开；未提供关闭方法时可返回 -ENOTSUP，其他负值来自后端。
  * @note   成功后可重新使用同一文件对象。若关闭失败，前端仍保留挂载点。
  */
int fs_close(struct fs_file_t *zfp)
{
	int rc = -EINVAL;                                /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 关闭空句柄无需访问后端。 */
	if (zfp->mp == NULL) {                           /* 检查文件对象是否已与一个卷绑定。 */
		return 0;                                /* 当前句柄无需释放卷资源，按成功处理。 */
	}

	CHECKIF(zfp->mp->fs->close == NULL) {            /* 启用接口检查时验证后端已提供 close 方法。 */
		return -ENOTSUP;                         /* 后端缺少对应方法，返回操作不支持。 */
	}

	/* 只有后端成功关闭后，前端才清除所属卷。 */
	rc = zfp->mp->fs->close(zfp);                    /* 由绑定的后端结束后端文件访问并回收句柄。 */
	if (rc < 0) {                                    /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file close error (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
		return rc;                               /* 关闭已经失败，文件对象仍保有挂载指针。 */
	}

	zfp->mp = NULL;                                  /* 成功关闭后解除文件与卷的绑定。 */

	return rc;                                       /* 卷绑定已清除，返回后端的关闭结果。 */
}

/**
  * @brief  从打开的文件当前位置读取数据。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  ptr：[输出]至少可写 size 字节的目标区。
  * @param  size：[输入]本次最多读取的字节数。
  * @retval 成功返回实际读取字节数，短读可能到达文件尾，0 表示未取得数据。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   上传端需要固定长度时须校验返回值是否等于请求长度。
  */
ssize_t fs_read(struct fs_file_t *zfp, void *ptr, size_t size)
{
	int rc = -EINVAL;                               /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 先验证句柄和读取方法，再把实际传输交给后端。 */
	if (zfp->mp == NULL) {                          /* 检查文件对象是否已与一个卷绑定。 */
		return -EBADF;                          /* 未打开的文件没有可用的后端句柄。 */
	}

	CHECKIF(zfp->mp->fs->read == NULL) {            /* 启用接口检查时验证后端已提供 read 方法。 */
		return -ENOTSUP;                        /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = zfp->mp->fs->read(zfp, ptr, size);         /* 由绑定的后端将请求的数据读入调用者的缓冲区。 */
	if (rc < 0) {                                   /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file read error (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                      /* 将实际传输字节数或负错误码原样交付调用者。 */
}

/**
  * @brief  把连续缓冲写入已打开的文件。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  ptr：[输入]至少可读 size 字节的源数据。
  * @param  size：[输入]计划写入的字节数。
  * @retval 成功返回实际写入字节数，可以短写。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   调用者负责识别短写并按需同步，返回的长度并不代表已经写回介质。
  */
ssize_t fs_write(struct fs_file_t *zfp, const void *ptr, size_t size)
{
	int rc = -EINVAL;                                /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 已打开的文件通过所属卷写入，写入数量由后端报告。 */
	if (zfp->mp == NULL) {                           /* 检查文件对象是否已与一个卷绑定。 */
		return -EBADF;                           /* 未打开的文件没有可用的后端句柄。 */
	}

	CHECKIF(zfp->mp->fs->write == NULL) {            /* 启用接口检查时验证后端已提供 write 方法。 */
		return -ENOTSUP;                         /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = zfp->mp->fs->write(zfp, ptr, size);         /* 由绑定的后端写入指定字节数，结果可能为短写。 */
	if (rc < 0) {                                    /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file write error (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                       /* 将实际传输字节数或负错误码原样交付调用者。 */
}

/**
  * @brief  按所选基点调整文件游标。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  offset：[输入]相对所选基点的有符号字节偏移。
  * @param  whence：[输入]FS_SEEK_SET、FS_SEEK_CUR 或 FS_SEEK_END。
  * @retval 0 表示成功。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。 本项目 FatFs 对越界位置或无效基点返回 -EINVAL。
  * @note   移动游标不修改文件内容。
  */
int fs_seek(struct fs_file_t *zfp, off_t offset, int whence)
{
	int rc = -ENOTSUP;                               /* 保存后端状态，预设为不支持操作。 */

	/* 验证已有卷和定位方法后再移动游标。 */
	if (zfp->mp == NULL) {                           /* 检查文件对象是否已与一个卷绑定。 */
		return -EBADF;                           /* 未打开的文件没有可用的后端句柄。 */
	}

	CHECKIF(zfp->mp->fs->lseek == NULL) {            /* 启用接口检查时验证后端已提供 lseek 方法。 */
		return -ENOTSUP;                         /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = zfp->mp->fs->lseek(zfp, offset, whence);    /* 由绑定的后端按指定基点及字节偏移移动游标。 */
	if (rc < 0) {                                    /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file seek error (%d)", rc);     /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                       /* 返回后端定位结果或偏移超界等错误。 */
}

/**
  * @brief  读取打开文件的当前字节位置。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 成功返回从文件开头计算的非负字节偏移。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   调用者须提供已初始化的有效对象。
  */
off_t fs_tell(struct fs_file_t *zfp)
{
	int rc = -ENOTSUP;                              /* 保存后端状态，预设为不支持操作。 */

	/* 通过文件对象当前所属卷取得游标位置。 */
	if (zfp->mp == NULL) {                          /* 检查文件对象是否已与一个卷绑定。 */
		return -EBADF;                          /* 未打开的文件没有可用的后端句柄。 */
	}

	CHECKIF(zfp->mp->fs->tell == NULL) {            /* 启用接口检查时验证后端已提供 tell 方法。 */
		return -ENOTSUP;                        /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = zfp->mp->fs->tell(zfp);                    /* 由绑定的后端读取当前位置相对文件起始处的偏移。 */
	if (rc < 0) {                                   /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file tell error (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                      /* 将当前位置或负错误码原样交付调用者。 */
}

/**
  * @brief  将打开文件缩短或延长到给定长度。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @param  length：[输入]目标文件长度，单位字节，应为非负值。
  * @retval 0 表示成功。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   当前 FatFs 在扩展定位后仅同步，不填零。空间不足时实际长度可能小于请求值。
  */
int fs_truncate(struct fs_file_t *zfp, off_t length)
{
	int rc = -EINVAL;                                   /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 仅对已经绑定的文件调用后端长度调整操作。 */
	if (zfp->mp == NULL) {                              /* 检查文件对象是否已与一个卷绑定。 */
		return -EBADF;                              /* 未打开的文件没有可用的后端句柄。 */
	}

	CHECKIF(zfp->mp->fs->truncate == NULL) {            /* 启用接口检查时验证后端已提供 truncate 方法。 */
		return -ENOTSUP;                            /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = zfp->mp->fs->truncate(zfp, length);            /* 由绑定的后端调整文件长度。 */
	if (rc < 0) {                                       /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file truncate error (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                          /* 将本次长度调整的后端状态交还调用方。 */
}

/**
  * @brief  请求后端把打开文件的缓存写回介质。
  * @param  zfp：[输入/输出]非空文件对象，打开后在关闭前由调用者保持有效。
  * @retval 0 表示成功。未打开返回 -EBADF，缺少后端操作时可返回 -ENOTSUP，其他负值由后端给出。
  * @note   需要保留句柄继续访问时可在关闭前同步。
  */
int fs_sync(struct fs_file_t *zfp)
{
	int rc = -EINVAL;                               /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 通过文件持有的卷将缓存写回。 */
	if (zfp->mp == NULL) {                          /* 检查文件对象是否已与一个卷绑定。 */
		return -EBADF;                          /* 未打开的文件没有可用的后端句柄。 */
	}

	CHECKIF(zfp->mp->fs->sync == NULL) {            /* 启用接口检查时验证后端已提供 sync 方法。 */
		return -ENOTSUP;                        /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = zfp->mp->fs->sync(zfp);                    /* 由绑定的后端把文件缓存同步到介质。 */
	if (rc < 0) {                                   /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("file sync error (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                      /* 把缓存同步状态交还调用方，失败可据此识别。 */
}

/* 目录操作：区分实际卷游标与 VFS 虚拟根目录的挂载表游标。 */
/**
  * @brief  打开挂载卷中的目录或枚举虚拟根目录。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  abs_path：[输入]非空、以斜线开头的路径字符串。
  * @retval 0 表示成功。-EINVAL 路径无效，-EBUSY 对象已有游标，-ENOENT 无匹配卷，其他负值来自后端或操作检查。
  * @note   根路径 / 枚举挂载表，其他路径按卷分派。
  */
int fs_opendir(struct fs_dir_t *zdp, const char *abs_path)
{
	struct fs_mount_t *mp;                                               /* 接收按输入路径选出的挂载描述。 */
	int rc = -EINVAL;                                                    /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 验证绝对路径以及目录句柄空闲状态。 */
	if ((abs_path == NULL) ||                                            /* 先排除空指针，再检查输入路径格式。 */
			(strlen(abs_path) < 1) || (abs_path[0] != '/')) {    /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                              /* 记录路径或参数校验失败。 */
		return -EINVAL;                                              /* 返回路径或参数无效。 */
	}

	if (zdp->mp != NULL || zdp->dirp != NULL) {                          /* 同时检查真实卷与虚拟根目录游标是否已被占用。 */
		return -EBUSY;                                               /* 拒绝覆盖已有的目录遍历状态。 */
	}


	if (strcmp(abs_path, "/") == 0) {                                    /* 单独处理 VFS 的根路径。 */
		/* 根目录没有后端，改为在锁内获取第一个挂载节点。 */
		k_mutex_lock(&mutex, K_FOREVER);                             /* 等待并持有互斥锁，防止并发更改共享表。 */

		zdp->mp = NULL;                                              /* 根遍历以空挂载指针作标记。 */
		zdp->dirp = sys_dlist_peek_head(&fs_mnt_list);               /* 首个待返回目录项是挂载表表头，空表取得 NULL。 */

		k_mutex_unlock(&mutex);                                      /* 结束本次受保护的共享表访问。 */

		return 0;                                                    /* 根目录游标已建好，无需调用具体后端。 */
	}

	/* 非根路径进入挂载卷的目录实现。 */
	rc = fs_get_mnt_point(&mp, abs_path, NULL);                          /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                        /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("mount point not found!!");                          /* 记录未匹配到挂载路径。 */
		return rc;                                                   /* 返回路径查找错误，目录句柄未绑定。 */
	}

	CHECKIF(mp->fs->opendir == NULL) {                                   /* 启用接口检查时验证后端已提供 opendir 方法。 */
		return -ENOTSUP;                                             /* 后端缺少对应方法，返回操作不支持。 */
	}

	zdp->mp = mp;                                                        /* 让后端在打开目录时访问该卷上下文。 */
	rc = zdp->mp->fs->opendir(zdp, abs_path);                            /* 由绑定的后端打开实际卷中的目录。 */
	if (rc < 0) {                                                        /* 负值表示调用失败，进入相应错误路径。 */
		zdp->mp = NULL;                                              /* 打开失败时清除临时绑定。 */
		zdp->dirp = NULL;                                            /* 同时清空无效的后端目录对象指针。 */
		LOG_ERR("directory open error (%d)", rc);                    /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                                           /* 返回后端状态；失败时本分支已清除两个指针。 */
}

/**
  * @brief  以簇号和目录偏移定位遍历起点。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @param  cluster：[输入]FAT 目录的起始簇号。
  * @param  blk_ofs：[输入]目录内的字节偏移，须落在有效目录项边界。
  * @retval -EINVAL 为无效路径，-ENOENT 为无匹配卷；已安装扩展且调用成功返回 0，失败返回后端错误码。
  * @note   根目录 / 总是从挂载表枚举。匹配卷后先填 mp；缺少扩展时会沿用匹配成功的 0 而未打开后端对象；后端失败时也保留 mp。
  */
int fs_opendir_cluster(struct fs_dir_t *zdp, const char *path, unsigned int cluster, unsigned int blk_ofs)
{
	struct fs_mount_t *mp;                                                     /* 接收按输入路径选出的挂载描述。 */
	int rc = -EINVAL;                                                          /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 路径用于选择卷；虚拟根目录有独立遍历逻辑。 */
	if ((path == NULL) ||                                                      /* 先排除空指针，再检查输入路径格式。 */
			(strlen(path) < 1) || (path[0] != '/')) {                  /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid path!!");                                         /* 记录路径或参数校验失败。 */
		return -EINVAL;                                                    /* 返回路径或参数无效。 */
	}

	if (strcmp(path, "/") == 0) {                                              /* 根目录由 VFS 合成，不按簇定位。 */
		/* 根目录与簇号无关，直接从当前挂载表开始。 */
		k_mutex_lock(&mutex, K_FOREVER);                                   /* 等待并持有互斥锁，防止并发更改共享表。 */

		zdp->mp = NULL;                                                    /* 以空挂载指针标识虚拟根目录。 */
		zdp->dirp = sys_dlist_peek_head(&fs_mnt_list);                     /* 首次遍历从当前挂载表的第一个节点开始。 */

		k_mutex_unlock(&mutex);                                            /* 结束本次受保护的共享表访问。 */

		return 0;                                                          /* 根目录游标准备完成。 */
	}

	/* 选择实际卷，再交给项目的按簇打开方法。 */
	rc = fs_get_mnt_point(&mp, path, NULL);                                    /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                              /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("%s:mount point not found!!", __func__);                   /* 记录未匹配到挂载路径。 */
		return rc;                                                         /* 返回目录路径的卷查找错误。 */
	}

	zdp->mp = mp;                                                              /* 保存已匹配卷，扩展回调失败时此处不回滚。 */

	if (zdp->mp->fs->opendir_cluster != NULL) {                                /* 只在后端提供按簇定位方法时分派。 */
		rc = zdp->mp->fs->opendir_cluster(zdp, path, cluster, blk_ofs);    /* 传入原始路径和目录项坐标，由后端解释。 */
		if (rc < 0) {                                                      /* 负值表示调用失败，进入相应错误路径。 */
			LOG_ERR("opendir error (%d)", rc);                         /* 记录本次操作的错误，便于结合调用状态诊断。 */
		}
	}

	return rc;                                                                 /* 返回最后一次实际执行的查询或后端调用结果。 */

}

/**
  * @brief  从卷目录或虚拟根目录读取下一个可见目录项。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @param  entry：[输出]非空、可写的目录项，用于接收名称、类型和长度。
  * @retval 0 表示取得一项或遍历结束，结束时 name[0] 为 0。根目录游标对应的挂载项已移除时返回 -ENOENT，其他负值来自操作检查或后端。
  * @note   过滤真实目录中的 . 和 ..；虚拟根目录以挂载点名称构造目录项。
  */
int fs_readdir(struct fs_dir_t *zdp, struct fs_dirent *entry)
{
	if (zdp->mp) {                                                        /* 有挂载对象时遍历真实后端目录。 */
		/* 真实卷使用其后端目录游标。 */
		int rc = -EINVAL;                                             /* 保存路径校验或后端调用的状态，初值为参数无效。 */

		CHECKIF(zdp->mp->fs->readdir == NULL) {                       /* 启用接口检查时验证后端已提供 readdir 方法。 */
			return  -ENOTSUP;                                     /* 后端缺少对应方法，返回操作不支持。 */
		}

		/* 跳过目录 . 和 ..，保持普通文件、其他目录及结束标记。 */
		while (true) {                                                /* 直到取得一项可见目录项、到达末尾或遇错。 */
			rc = zdp->mp->fs->readdir(zdp, entry);                /* 由绑定的后端将下一条后端目录项放入输出缓冲区。 */
			if (rc < 0) {                                         /* 负值表示调用失败，进入相应错误路径。 */
				break;                                        /* 后端失败，停止本次目录遍历并报告错误。 */
			}
			if (entry->name[0] == 0) {                            /* 空名称是后端的目录结束标志。 */
				break;                                        /* 保留空名称让调用者判断遍历已结束。 */
			}
			if (entry->type != FS_DIR_ENTRY_DIR) {                /* 普通文件无需作点目录过滤。 */
				break;                                        /* 将该文件作为本次结果。 */
			}
			if ((strcmp(entry->name, ".") != 0)                   /* 判断目录名是否为当前或上级目录标记。 */
			    && (strcmp(entry->name, "..") != 0)) {            /* 两个特殊目录之外的目录对调用者可见。 */
				break;                                        /* 已取得可返回的目录项。 */
			}
		}
		if (rc < 0) {                                                 /* 负值表示调用失败，进入相应错误路径。 */
			LOG_ERR("directory read error (%d)", rc);             /* 记录本次操作的错误，便于结合调用状态诊断。 */
		}

		return rc;                                                    /* 传回本次遍历状态，结束标记保存在 entry 的名称内。 */
	}

	/* 根目录由挂载表临时合成目录项。 */
	if (zdp->dirp == NULL) {                                              /* 无下一个挂载节点，虚拟根目录遍历结束。 */
		/* 没有后继时沿用真实目录的空名称约定。 */
		entry->name[0] = 0;                                           /* 用空名称标记遍历结束。 */
		return 0;                                                     /* 到达末尾也返回成功。 */
	}

	/* 重新查表验证上次保留的节点，再从该节点取得后继。 */
	sys_dnode_t *node, *next = NULL;                                      /* next 默认为空，表示当前节点之后没有更多卷。 */
	bool found = false;                                                   /* 区分找到最后一项与当前项已被卸载两种情况。 */

	k_mutex_lock(&mutex, K_FOREVER);                                      /* 等待并持有互斥锁，防止并发更改共享表。 */

	SYS_DLIST_FOR_EACH_NODE(&fs_mnt_list, node) {                         /* 沿挂载双向链表逐卷检查。 */
		if (node == zdp->dirp) {                                      /* 比较保存的节点是否仍在挂载表内。 */
			found = true;                                         /* 当前项仍存在，可以安全读取。 */

			/* 从现存挂载对象构造只含一个路径分量的可见目录项。 */
			struct fs_mount_t *mnt;                               /* 保存当前节点所属挂载描述。 */

			mnt = CONTAINER_OF(node, struct fs_mount_t, node);    /* 由链表成员地址还原其挂载描述。 */

			entry->type = FS_DIR_ENTRY_DIR;                       /* 向调用者提供可遍历的挂载目录项。 */
			strncpy(entry->name, mnt->mnt_point + 1,              /* 跳过挂载名开头的斜线，将它作为根下的目录名。 */
				sizeof(entry->name) - 1);                     /* 留出一个字节给终止符。 */
			entry->name[sizeof(entry->name) - 1] = 0;             /* 即使挂载名被截断也保证缓冲区内有终止符。 */
			entry->size = 0;                                      /* 虚拟目录项的长度记为零。 */

			/* 当前位置即将交付，提前保留下一次要检查的节点。 */
			next = sys_dlist_peek_next(&fs_mnt_list, node);       /* 在持锁期间取后继，供下一次调用核对。 */
			break;                                                /* 已处理当前项，离开链表扫描。 */
		}
	}

	k_mutex_unlock(&mutex);                                               /* 结束本次受保护的共享表访问。 */

	if (!found) {                                                         /* 记录的当前项在两次目录读取之间消失。 */
		/* 上一次保存的节点已脱离挂载表，不能继续解引用。 */
		return -ENOENT;                                               /* 通过错误通知调用者原游标已失效。 */
	}

	zdp->dirp = next;                                                     /* 保存下一位置；读到末项后它为 NULL。 */
	return 0;                                                             /* 根目录项已填入输出缓冲区。 */
}

/**
  * @brief  结束一次目录遍历并释放游标。
  * @param  zdp：[输入/输出]非空目录对象，遍历期间持有后端游标。
  * @retval 0 为成功，缺少方法时可返回 -ENOTSUP，其他负值来自后端。
  * @note   虚拟根目录只需丢弃链表游标，真实目录由挂载卷关闭。
  */
int fs_closedir(struct fs_dir_t *zdp)
{
	int rc = -EINVAL;                                     /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	/* 根目录和卷上的目录使用不同的关闭方式。 */
	if (zdp->mp == NULL) {                                /* 空挂载指针对应虚拟根目录或未绑定的句柄。 */
		/* 虚拟目录的游标借自挂载表，不占后端目录对象。 */
		zdp->dirp = NULL;                             /* 清掉链表游标，结束根目录遍历。 */
		return 0;                                     /* 没有需要后端释放的对象。 */
	}

	CHECKIF(zdp->mp->fs->closedir == NULL) {              /* 启用接口检查时验证后端已提供 closedir 方法。 */
		return -ENOTSUP;                              /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = zdp->mp->fs->closedir(zdp);                      /* 由绑定的后端释放实际目录对象。 */
	if (rc < 0) {                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("directory close error (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
		return rc;                                    /* 后端关闭失败，前端保留原有字段。 */
	}

	zdp->mp = NULL;                                       /* 成功后解除目录与卷的绑定。 */
	zdp->dirp = NULL;                                     /* 同时清除后端游标，句柄可重新初始化或打开。 */
	return rc;                                            /* 已解除游标绑定，返回后端的关闭状态。 */
}

/* 按路径操作卷上的对象，涉及写入时先检查挂载标志。 */
/**
  * @brief  在路径对应的挂载卷创建目录。
  * @param  abs_path：[输入]非空、以斜线开头的路径字符串。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 只读卷返回 -EROFS，已有同名项或介质错误由后端返回。
  * @note   挂载表查找会等待互斥锁，应在线程上下文调用。
  */
int fs_mkdir(const char *abs_path)
{
	struct fs_mount_t *mp;                                                /* 接收按输入路径选出的挂载描述。 */
	int rc = -EINVAL;                                                     /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	if ((abs_path == NULL) ||                                             /* 先排除空指针，再检查输入路径格式。 */
			(strlen(abs_path) <= 1) || (abs_path[0] != '/')) {    /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                               /* 记录路径或参数校验失败。 */
		return -EINVAL;                                               /* 返回路径或参数无效。 */
	}

	/* 目录创建会修改介质，先定位卷并检查只读标志。 */
	rc = fs_get_mnt_point(&mp, abs_path, NULL);                           /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("mount point not found!!");                           /* 记录未匹配到挂载路径。 */
		return rc;                                                    /* 找不到负责此路径的卷，直接交付查找结果。 */
	}

	if (mp->flags & FS_MOUNT_FLAG_READ_ONLY) {                            /* 先按挂载属性拒绝会修改介质的操作。 */
		return -EROFS;                                                /* 以只读文件系统错误拒绝本次修改。 */
	}

	CHECKIF(mp->fs->mkdir == NULL) {                                      /* 启用接口检查时验证后端已提供 mkdir 方法。 */
		return -ENOTSUP;                                              /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = mp->fs->mkdir(mp, abs_path);                                     /* 由绑定的后端在给定路径创建目录。 */
	if (rc < 0) {                                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("failed to create directory (%d)", rc);               /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                                            /* 创建结果可能包含介质或同名目录错误。 */
}

/**
  * @brief  删除指定文件或可删除的目录项。
  * @param  abs_path：[输入]非空、以斜线开头的路径字符串。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 只读卷返回 -EROFS，其他删除错误由后端决定。
  * @note   挂载表查找会等待互斥锁，应在线程上下文调用。
  */
int fs_unlink(const char *abs_path)
{
	struct fs_mount_t *mp;                                                /* 接收按输入路径选出的挂载描述。 */
	int rc = -EINVAL;                                                     /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	if ((abs_path == NULL) ||                                             /* 先排除空指针，再检查输入路径格式。 */
			(strlen(abs_path) <= 1) || (abs_path[0] != '/')) {    /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                               /* 记录路径或参数校验失败。 */
		return -EINVAL;                                               /* 返回路径或参数无效。 */
	}

	/* 定位所属卷，删除前检查挂载权限。 */
	rc = fs_get_mnt_point(&mp, abs_path, NULL);                           /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("mount point not found!!");                           /* 记录未匹配到挂载路径。 */
		return rc;                                                    /* 返回挂载查找错误，尚未删除任何目录项。 */
	}

	if (mp->flags & FS_MOUNT_FLAG_READ_ONLY) {                            /* 先按挂载属性拒绝会修改介质的操作。 */
		return -EROFS;                                                /* 以只读文件系统错误拒绝本次修改。 */
	}

	CHECKIF(mp->fs->unlink == NULL) {                                     /* 启用接口检查时验证后端已提供 unlink 方法。 */
		return -ENOTSUP;                                              /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = mp->fs->unlink(mp, abs_path);                                    /* 由绑定的后端移除指定目录项。 */
	if (rc < 0) {                                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("failed to unlink path (%d)", rc);                    /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                                            /* 原样传回后端删除成功或失败的状态。 */
}

/**
  * @brief  将同卷目录项移动或重命名。
  * @param  from：[输入]非空的源绝对路径。
  * @param  to：[输入]非空的目标绝对路径。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 卷前缀不符亦返回 -EINVAL，源卷只读返回 -EROFS。
  * @note   前端只比较源挂载前缀长度。FatFs 后端会先删除已存在的目标。
  */
int fs_rename(const char *from, const char *to)
{
	struct fs_mount_t *mp;                                                    /* 接收按输入路径选出的挂载描述。 */
	size_t match_len;                                                         /* 由源卷查找填入的匹配前缀字节数。 */
	int rc = -EINVAL;                                                         /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	if ((from == NULL) || (strlen(from) <= 1) || (from[0] != '/') ||          /* 同时检查源路径和目标路径。 */
			(to == NULL) || (strlen(to) <= 1) || (to[0] != '/')) {    /* 两者均须是含名称的绝对路径。 */
		LOG_ERR("invalid file name!!");                                   /* 记录路径或参数校验失败。 */
		return -EINVAL;                                                   /* 返回路径或参数无效。 */
	}

	/* 确定源卷和匹配长度，再校验目标路径前缀。 */
	rc = fs_get_mnt_point(&mp, from, &match_len);                             /* 从源路径选卷，同时保留匹配长度以检查目标前缀。 */
	if (rc < 0) {                                                             /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("mount point not found!!");                               /* 记录未匹配到挂载路径。 */
		return rc;                                                        /* 无法选定源卷，按查找错误退出。 */
	}

	if (mp->flags & FS_MOUNT_FLAG_READ_ONLY) {                                /* 先按挂载属性拒绝会修改介质的操作。 */
		return -EROFS;                                                    /* 以只读文件系统错误拒绝本次修改。 */
	}

	/* 本实现以源卷匹配的前缀长度检查目标。 */
	if (strncmp(from, to, match_len) != 0) {                                  /* 按源卷的前缀长度比较两个路径，未再次解析目标卷。 */
		LOG_ERR("mount point not same!!");                                /* 记录本次操作的错误，便于结合调用状态诊断。 */
		return -EINVAL;                                                   /* 路径前缀不同，不能交给同一个后端改名。 */
	}

	CHECKIF(mp->fs->rename == NULL) {                                         /* 启用接口检查时验证后端已提供 rename 方法。 */
		return -ENOTSUP;                                                  /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = mp->fs->rename(mp, from, to);                                        /* 由绑定的后端在该后端重命名或移动目录项。 */
	if (rc < 0) {                                                             /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("failed to rename file or dir (%d)", rc);                 /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}

	return rc;                                                                /* 将后端替换或改名的最终状态交给调用方。 */
}

/**
  * @brief  按路径读取单个文件或目录的元数据。
  * @param  abs_path：[输入]非空、以斜线开头的路径字符串。
  * @param  entry：[输出]非空、可写的目录项，用于接收名称、类型和长度。
  * @retval 0 表示成功。-EINVAL 表示路径格式不合法，-ENOENT 表示无匹配卷，缺少对应后端方法时可返回 -ENOTSUP。 不存在的对象也可返回 -ENOENT。
  * @note   对象不存在是可预期的查询结果，前端原样返回该状态。
  */
int fs_stat(const char *abs_path, struct fs_dirent *entry)
{
	struct fs_mount_t *mp;                                                /* 接收按输入路径选出的挂载描述。 */
	int rc = -EINVAL;                                                     /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	if ((abs_path == NULL) ||                                             /* 先排除空指针，再检查输入路径格式。 */
			(strlen(abs_path) <= 1) || (abs_path[0] != '/')) {    /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                               /* 记录路径或参数校验失败。 */
		return -EINVAL;                                               /* 返回路径或参数无效。 */
	}

	/* 先找卷，再由后端填入名称、类型及大小。 */
	rc = fs_get_mnt_point(&mp, abs_path, NULL);                           /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("mount point not found!!");                           /* 记录未匹配到挂载路径。 */
		return rc;                                                    /* 没有匹配卷，尚未取得对象信息。 */
	}

	CHECKIF(mp->fs->stat == NULL) {                                       /* 启用接口检查时验证后端已提供 stat 方法。 */
		return -ENOTSUP;                                              /* 后端缺少对应方法，返回操作不支持。 */
	}

	rc = mp->fs->stat(mp, abs_path, entry);                               /* 由绑定的后端读取单个对象的元数据。 */
	if (rc == -ENOENT) {                                                  /* 对象不存在是正常的查询结果。 */
		/* 直接返回对象不存在，避免将常规探测记成介质错误。 */
	} else if (rc < 0) {                                                  /* 其他负值表示需记录的查询失败。 */
		LOG_ERR("failed get file or dir stat (%d)", rc);              /* 记录本次操作的错误，便于结合调用状态诊断。 */
	}
	return rc;                                                            /* 返回对象是否可查及其他读取状态。 */
}

/**
  * @brief  取得指定卷的分配单位、容量及剩余空间。
  * @param  abs_path：[输入]非空、以斜线开头的路径字符串。
  * @param  stat：[输出]非空、可写的容量结构，各字段单位见 struct fs_statvfs。
  * @retval 0 通常表示成功，非法路径返回 -EINVAL，未匹配到卷返回 -ENOENT，已实现的后端返回自身状态。
  * @note   后端未安装 statvfs 时本实现沿用挂载点查询的 0，调用者不能据此认定输出已填充。
  */
int fs_statvfs(const char *abs_path, struct fs_statvfs *stat)
{
	struct fs_mount_t *mp;                                                /* 接收按输入路径选出的挂载描述。 */
	int rc;                                                               /* 依次保存挂载查找及可选后端调用的结果。 */

	if ((abs_path == NULL) ||                                             /* 先排除空指针，再检查输入路径格式。 */
			(strlen(abs_path) <= 1) || (abs_path[0] != '/')) {    /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                               /* 记录路径或参数校验失败。 */
		return -EINVAL;                                               /* 返回路径或参数无效。 */
	}

	/* 查询挂载卷，只有已实现的后端方法会更新统计输出。 */
	rc = fs_get_mnt_point(&mp, abs_path, NULL);                           /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("mount point not found!!");                           /* 记录未匹配到挂载路径。 */
		return rc;                                                    /* 无法选卷时，输出的容量结构未填写。 */
	}

	if (mp->fs->statvfs != NULL) {                                        /* statvfs 是可选方法；缺失时不会填写容量结构。 */
		rc = mp->fs->statvfs(mp, abs_path, stat);                     /* 由绑定的后端填入卷的分配单位和容量数据。 */
		if (rc < 0) {                                                 /* 负值表示调用失败，进入相应错误路径。 */
			LOG_ERR("failed get file or dir stat (%d)", rc);      /* 记录本次操作的错误，便于结合调用状态诊断。 */
		}
	}

	return rc;                                                            /* 返回最后一次实际执行的查询或后端调用结果。 */
}

/**
  * @brief  按注册类型挂载一个卷，成功后加入路径匹配表。
  * @param  mp：[输入/输出]非空的挂载描述，路径、操作表及卷对象在整个挂载期间须有效。
  * @retval 0 为成功，-EINVAL 为描述或路径无效，-EBUSY 为同名挂载点，-ENOENT 为类型未注册，-ENOTSUP 为缺少挂载方法，其他负值来自后端。
  * @note   挂载名须以 / 开头且长于单个斜线。对象及其字符串由调用者保持到成功卸载。FatFs 后端使用配置的卷名或数字卷号。
  */
int fs_mount(struct fs_mount_t *mp)
{
	struct fs_mount_t *itr;                                               /* 保存逐项核对的已挂载卷。 */
	const struct fs_file_system_t *fs;                                    /* 由注册类型查得的后端分派表。 */
	sys_dnode_t *node;                                                    /* 遍历挂载表需要的双向链表节点。 */
	int rc = -EINVAL;                                                     /* 保存路径校验或后端调用的状态，初值为参数无效。 */
	size_t len = 0;                                                       /* 保存挂载路径字节长度，成功后写入挂载描述。 */

	/* 先检查描述和路径，再加锁查询注册表和挂载表。 */
	if ((mp == NULL) || (mp->mnt_point == NULL)) {                        /* 检查挂载对象及其名称可用后才读取路径。 */
		LOG_ERR("mount point not initialized!!");                     /* 记录路径或参数校验失败。 */
		return -EINVAL;                                               /* 返回路径或参数无效。 */
	}

	len = strlen(mp->mnt_point);                                          /* 取得挂载名长度用于格式检查和去重。 */

	if ((len <= 1) || (mp->mnt_point[0] != '/')) {                        /* 禁止空根路径或没有前导斜线的路径。 */
		LOG_ERR("invalid mount point!!");                             /* 记录路径或参数校验失败。 */
		return -EINVAL;                                               /* 返回路径或参数无效。 */
	}

	/* 挂载名称去重与发布在同一个锁区间完成。 */
	k_mutex_lock(&mutex, K_FOREVER);                                      /* 等待并持有互斥锁，防止并发更改共享表。 */

	/* 同一挂载名称在整个 VFS 中只能出现一次。 */
	SYS_DLIST_FOR_EACH_NODE(&fs_mnt_list, node) {                         /* 沿挂载双向链表逐卷检查。 */
		itr = CONTAINER_OF(node, struct fs_mount_t, node);            /* 从挂载链表节点取得已有卷。 */
		/* 先排除长度不等的挂载名。 */
		if (len != itr->mountp_len) {                                 /* 名称长度不同，不可能重复。 */
			continue;                                             /* 继续核对下一个挂载名。 */
		}

		if (strncmp(mp->mnt_point, itr->mnt_point, len) == 0) {       /* 长度相同且字节完全一致表示同名挂载。 */
			LOG_ERR("mount point already exists!!");              /* 记录本次操作的错误，便于结合调用状态诊断。 */
			rc = -EBUSY;                                          /* 记录挂载点被占用。 */
			goto mount_err;                                       /* 转到公共解锁路径。 */
		}
	}

	/* 从类型表取出后端，进入它的卷挂载回调。 */
	fs = fs_type_get(mp->type);                                           /* 用调用方提供的类型号查注册表。 */
	if (fs == NULL) {                                                     /* 未注册的类型没有可调用的后端。 */
		LOG_ERR("requested file system type not registered!!");       /* 记录本次操作的错误，便于结合调用状态诊断。 */
		rc = -ENOENT;                                                 /* 报告未找到指定文件系统类型。 */
		goto mount_err;                                               /* 跳转至统一解锁出口。 */
	}

	CHECKIF(fs->mount == NULL) {                                          /* 若启用接口检查，确认该类型支持挂载。 */
		LOG_ERR("fs type %d does not support mounting", mp->type);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
		rc = -ENOTSUP;                                                /* 缺少挂载方法，卷无法生效。 */
		goto mount_err;                                               /* 从公共出口解锁。 */
	}

	if (fs->unmount == NULL) {                                            /* 缺少卸载方法仍允许本次挂载。 */
		LOG_WRN("mount path %s is not unmountable",                   /* 记录卷将无法通过卸载方法释放。 */
			log_strdup(mp->mnt_point));                           /* 为可能延迟写出的日志提供挂载名称。 */
	}

	rc = fs->mount(mp);                                                   /* 要求底层挂载实际介质，持锁期间不会并发登记同名卷。 */
	if (rc < 0) {                                                         /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("fs mount error (%d)", rc);                           /* 记录本次操作的错误，便于结合调用状态诊断。 */
		goto mount_err;                                               /* 后端挂载失败，跳过向挂载表发布新卷。 */
	}

	/* 只有后端确认卷已就绪后，才填写缓存并向路径查找公开。 */
	mp->mountp_len = len;                                                 /* 缓存后续路径比较要用的前缀长度。 */
	mp->fs = fs;                                                          /* 发布该卷绑定的后端方法表。 */

	sys_dlist_append(&fs_mnt_list, &mp->node);                            /* 链入可匹配列表，此后文件操作可通过路径找到该卷。 */
	LOG_DBG("fs mounted at %s", log_strdup(mp->mnt_point));               /* 输出操作结果供启用调试日志时查看。 */

mount_err:                                                                    /* 挂载成功和持锁后的失败路径汇合于此。 */
	k_mutex_unlock(&mutex);                                               /* 结束本次受保护的共享表访问。 */
	return rc;                                                            /* 返回登记冲突、后端挂载失败或成功的最终状态。 */
}

/**
  * @brief  卸载指定卷并移除其挂载点。
  * @param  mp：[输入/输出]非空的挂载描述，路径、操作表及卷对象在整个挂载期间须有效。
  * @retval 0 为成功，-EINVAL 为描述为空或尚未挂载，-ENOTSUP 为不支持卸载，其他负值来自后端。
  * @note   先结束使用该卷的文件和目录对象；后端卸载成功后才清理挂载表。
  */
int fs_unmount(struct fs_mount_t *mp)
{
	int rc = -EINVAL;                                              /* 保存路径校验或后端调用的状态，初值为参数无效。 */

	if (mp == NULL) {                                              /* 空描述没有可供注销的卷。 */
		return rc;                                             /* 返回初始化的无效参数错误。 */
	}

	/* 挂载状态检查、后端卸载及摘链共享同一个锁。 */
	k_mutex_lock(&mutex, K_FOREVER);                               /* 等待并持有互斥锁，防止并发更改共享表。 */

	if (mp->fs == NULL) {                                          /* 无方法表说明尚未成功挂载或已被卸载。 */
		LOG_ERR("fs not mounted (mp == %p)", mp);              /* 记录本次操作的错误，便于结合调用状态诊断。 */
		goto unmount_err;                                      /* 跳过后端和链表操作，到公共出口释放锁。 */
	}

	CHECKIF(mp->fs->unmount == NULL) {                             /* 启用接口检查时验证后端已提供 unmount 方法。 */
		LOG_ERR("fs unmount not supported!!");                 /* 记录本次操作的错误，便于结合调用状态诊断。 */
		rc = -ENOTSUP;                                         /* 当前后端无法执行卸载。 */
		goto unmount_err;                                      /* 从统一出口释放锁。 */
	}

	rc = mp->fs->unmount(mp);                                      /* 由绑定的后端请求后端卸载持有的卷。 */
	if (rc < 0) {                                                  /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("fs unmount error (%d)", rc);                  /* 记录本次操作的错误，便于结合调用状态诊断。 */
		goto unmount_err;                                      /* 后端未完成卸载，保留现有挂载表项。 */
	}

	/* 底层卸载完成后撤销方法表绑定。 */
	mp->fs = NULL;                                                 /* 成功后清空操作表，使挂载描述恢复未挂载状态。 */

	/* 同步移除挂载节点，使后续打开不再选中这个卷。 */
	sys_dlist_remove(&mp->node);                                   /* 从路径查找表移除此卷。 */
	LOG_DBG("fs unmounted from %s", log_strdup(mp->mnt_point));    /* 输出操作结果供启用调试日志时查看。 */

unmount_err:                                                           /* 所有持锁退出路径共用此出口。 */
	k_mutex_unlock(&mutex);                                        /* 结束本次受保护的共享表访问。 */
	return rc;                                                     /* 返回卸载结果，只有成功路径撤销了卷绑定。 */
}

/**
  * @brief  按索引依次读取当前挂载点名称。
  * @param  index：[输入/输出]非空的可写索引，首项为 0，成功后自增。
  * @param  name：[输出]非空的指针变量，接收借用的挂载名或 NULL。
  * @retval 0 为取得名称，-ENOENT 为没有相应项。
  * @note   名称仍属于挂载对象；挂载表在两次调用之间发生变化时，索引可对应不同对象。
  */
int fs_readmount(int *index, const char **name)
{
	sys_dnode_t *node;                                                    /* 保存遍历挂载链表的当前位置。 */
	int rc = -ENOENT;                                                     /* 索引无对应项时报告不存在。 */
	int cnt = 0;                                                          /* 从 0 开始计算当前节点在表内的位置。 */
	struct fs_mount_t *itr = NULL;                                        /* 保存索引对应卷，直到找到前保持空指针。 */

	*name = NULL;                                                         /* 提前置空返回名称，覆盖索引越界的情况。 */

	/* 按本次调用时的链表顺序寻找索引。 */
	k_mutex_lock(&mutex, K_FOREVER);                                      /* 等待并持有互斥锁，防止并发更改共享表。 */

	SYS_DLIST_FOR_EACH_NODE(&fs_mnt_list, node) {                         /* 沿挂载双向链表逐卷检查。 */
		if (*index == cnt) {                                          /* 与调用者请求的索引比较。 */
			itr = CONTAINER_OF(node, struct fs_mount_t, node);    /* 保存对应于该节点的挂载描述。 */
			break;                                                /* 已取得所需挂载项。 */
		}

		++cnt;                                                        /* 继续计算下一节点的索引。 */
	}

	k_mutex_unlock(&mutex);                                               /* 结束本次受保护的共享表访问。 */

	if (itr != NULL) {                                                    /* 只有找到指定项才推进调用方的索引。 */
		rc = 0;                                                       /* 标记查找成功。 */
		*name = itr->mnt_point;                                       /* 借出挂载描述中的原始路径字符串。 */
		++(*index);                                                   /* 下次调用查询后续位置。 */
	}

	return rc;                                                            /* 返回枚举成功或索引未命中。 */

}

/* 介质检测请求按路径转交卷后端。 */
/**
  * @brief  由已挂载的后端查询介质是否在位。
  * @param  path：[输入]非空、以斜线开头的路径字符串。
  * @param  state：[输出]非空的单字节状态，实际位值由底层驱动定义。
  * @retval 无效路径返回 -EINVAL，未匹配卷返回 -ENOENT；成功分派时返回底层检测结果。
  * @note   本项目 FatFs 检测失败会直接返回非零 FRESULT；未安装检测方法时返回 0 而不填写 state。
  */
int fs_disk_detect(const char *path, uint8_t *state)
{
	struct fs_mount_t *mp;                                        /* 接收按输入路径选出的挂载描述。 */
	int rc;                                                       /* 依次保存挂载查找及可选后端调用的结果。 */

	if ((path == NULL) ||                                         /* 先排除空指针，再检查输入路径格式。 */
			(strlen(path) <= 1) || (path[0] != '/')) {    /* 要求以斜线开头且长度符合此操作的要求。 */
		LOG_ERR("invalid file name!!");                       /* 记录路径或参数校验失败。 */
		return -EINVAL;                                       /* 返回路径或参数无效。 */
	}

	/* 从路径定位负责介质检测的卷。 */
	rc = fs_get_mnt_point(&mp, path, NULL);                       /* 根据绝对路径查找最具体的挂载点，取得实际后端。 */
	if (rc < 0) {                                                 /* 负值表示调用失败，进入相应错误路径。 */
		LOG_ERR("%s:mount point not found!!", __func__);      /* 记录未匹配到挂载路径。 */
		return rc;                                            /* 没有对应的卷可供介质检测。 */
	}

	if (mp->fs->disk_detect != NULL) {                            /* 没安装检测方法时保留查卷状态，state 未填写。 */
		rc = mp->fs->disk_detect(mp, path, state);            /* 由绑定的后端将介质检测结果写入状态字节。 */
		if (rc < 0) {                                         /* 这里只记录负值；FatFs 检测直接返回的正 FRESULT 将由调用者收到。 */
			LOG_ERR("failed get disk state (%d)", rc);    /* 记录本次操作的错误，便于结合调用状态诊断。 */
		}
	}

	return rc;                                                    /* 返回最后一次实际执行的查询或后端调用结果。 */

}

/* 登记后端类型，独立于卷的挂载。 */
/**
  * @brief  将文件系统类型与其操作表加入固定容量的注册表。
  * @param  type：[输入]注册表内唯一的文件系统类型编号。
  * @param  fs：[输入]非空的只读操作表，须在注册和卷使用期间持续有效。
  * @retval 0 为成功，-EALREADY 为类型已注册，-ENOSPC 为注册表已满。
  * @note   注册只登记类型，挂载后路径才能被 fs_open 等接口匹配。
  */
int fs_register(int type, const struct fs_file_system_t *fs)
{
	int rc = 0;                                 /* 默认登记操作成功，错误路径会改写此状态。 */

	/* 将重复检查与空槽写入放在同一次加锁中。 */
	k_mutex_lock(&mutex, K_FOREVER);            /* 等待并持有互斥锁，防止并发更改共享表。 */

	if (fs_type_get(type) != NULL) {            /* 同一个类型号只能绑定一个操作表。 */
		rc = -EALREADY;                     /* 类型号已登记，报告重复注册。 */
	} else {
		rc = registry_add(type, fs);        /* 向固定容量的类型表加入绑定。 */
	}

	k_mutex_unlock(&mutex);                     /* 结束本次受保护的共享表访问。 */

	LOG_DBG("fs register %d: %d", type, rc);    /* 输出操作结果供启用调试日志时查看。 */

	return rc;                                  /* 返回登记成功、类型重复或注册表已满。 */
}

/* 移除特定实现的类型登记。 */
/**
  * @brief  注销类型与操作表均匹配的注册项。
  * @param  type：[输入]注册表内唯一的文件系统类型编号。
  * @param  fs：[输入]注册时传入的同一操作表地址。
  * @retval 0 为已移除，-EINVAL 为类型未注册或表地址不匹配。
  * @note   先卸载依赖该表的卷，此处仅清注册项，不遍历已挂载卷。
  */
int fs_unregister(int type, const struct fs_file_system_t *fs)
{
	int rc = 0;                                   /* 默认登记操作成功，错误路径会改写此状态。 */
	struct registry_entry *ep;                    /* 当前注销请求对应的注册槽。 */

	/* 锁内核对方法表身份再释放类型槽。 */
	k_mutex_lock(&mutex, K_FOREVER);              /* 等待并持有互斥锁，防止并发更改共享表。 */

	ep = registry_find(type);                     /* 先按编号查找注册项。 */
	if ((ep == NULL) || (ep->fstp != fs)) {       /* 同时核对类型是否存在以及操作表地址是否相同。 */
		rc = -EINVAL;                         /* 拒绝移除不同实现的注册项。 */
	} else {
		registry_clear_entry(ep);             /* 令槽位空闲，供后续注册复用。 */
	}

	k_mutex_unlock(&mutex);                       /* 结束本次受保护的共享表访问。 */

	LOG_DBG("fs unregister %d: %d", type, rc);    /* 输出操作结果供启用调试日志时查看。 */
	return rc;                                    /* 返回注销成功或注册信息不匹配。 */
}

/**
  * @brief  在启动阶段初始化 VFS 共享状态。
  * @param  dev：[输入]由系统初始化框架传入，本实现不读取。
  * @retval 固定返回 0。
  * @note   通过 SYS_INIT 在后端注册和业务线程访问卷之前执行。
  */
static int fs_init(const struct device *dev)
{
	k_mutex_init(&mutex);            /* 在其他线程访问文件系统前初始化互斥锁。 */
	sys_dlist_init(&fs_mnt_list);    /* 建立空挂载链表；后端注册及各卷挂载在后续阶段完成。 */
	return 0;                        /* 公共文件系统已经准备好。 */
}

SYS_INIT(fs_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);    /* 在内核可用后的 POST_KERNEL 阶段建立 VFS 公共状态。 */
