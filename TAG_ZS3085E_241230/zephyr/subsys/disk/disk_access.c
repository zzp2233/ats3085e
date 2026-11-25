/*
 * Copyright (c) 2018 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 磁盘访问层维护磁盘实例注册表。文件系统以磁盘名提交逻辑扇区，具体设备操作由各后端的回调表实现。 */
#include <string.h>
#include <zephyr/types.h>
#include <sys/__assert.h>
#include <sys/util.h>
#include <init.h>
#include <storage/disk_access.h>
#include <errno.h>
#include <device.h>

/* 使用磁盘模块的配置日志级别，记录注册与注销诊断。 */
#define LOG_LEVEL CONFIG_DISK_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(disk);

/* 所有已注册的磁盘实例按嵌入节点串联；文件系统挂载表另由文件系统层管理。 */
static sys_dlist_t disk_access_list;

/* 互斥锁保护注册表的遍历、查重和增删；慢速介质 I/O 交给后端后不持有此锁。 */
static struct k_mutex mutex;

/**
 * @brief  从互斥保护的注册表查找完全同名的磁盘。
 * @param  name 非空、以 NUL 结束的注册名称。
 * @retval 成功返回注册方拥有的磁盘实例，未找到时为 NULL。
 * @note   返回前已经解锁；调用方应保证实例不会在使用期间注销并销毁。
 */
struct disk_info *disk_access_get_di(const char *name)
{
	struct disk_info *disk = NULL, *itr;                         /* disk 为待返回匹配结果，itr 是当前遍历的实例。 */
	size_t name_len = strlen(name);                              /* 取完整注册名长度，调用者须传有效字符串。 */
	sys_dnode_t *node;                                           /* 临时指向注册实例中嵌入的链表节点。 */

	/* 持有互斥锁查找精确名称，防止遍历过程中节点被移除。 */
	k_mutex_lock(&mutex, K_FOREVER);
	SYS_DLIST_FOR_EACH_NODE(&disk_access_list, node) {           /* 从表头到表尾逐个访问已注册节点。 */
		itr = CONTAINER_OF(node, struct disk_info, node);    /* 根据 node 在结构内的位置还原 disk_info 指针。 */

		/* 名称的长度必须相等，以免一个卷名错误匹配另一个卷名的前缀。 */
		if (strlen(itr->name) != name_len) {
			continue;                                    /* 跳过长度不匹配的实例。 */
		}

		/* 再比较整个名称的每个字节，区分大小写。 */
		if (strncmp(name, itr->name, name_len) == 0) {
			disk = itr;                                  /* 保存注册方提供的原始对象地址。 */
			break;                                       /* 找到唯一同名实例后结束遍历。 */
		}
	}

	/* 遍历完成即释放锁，不把注册表锁带入后续驱动调用。 */
	k_mutex_unlock(&mutex);

	return disk;                                                 /* 未匹配时保留入口预设的 NULL。 */
}

/**
 * @brief  按注册名称查找磁盘，执行后端初始化。
 * @param  pdrv 非空、以 NUL 结尾的磁盘名，本机 NOR 的名称为 NOR。
 * @retval 0 表示成功。找不到磁盘、操作表或初始化入口时返回 -EINVAL，后端的初始化错误原样返回。
 */
int disk_access_init(const char *pdrv)
{
	struct disk_info *disk = disk_access_get_di(pdrv);    /* 根据注册名取得保存实际介质及回调表的实例。 */
	int rc = -EINVAL;                                     /* 预置 -EINVAL，涵盖查无此盘或驱动缺少入口。 */

	/* 核实实例、操作表与 init 回调均有效后，再执行后端初始化。 */
	if ((disk != NULL) && (disk->ops != NULL) &&
				(disk->ops->init != NULL)) {
		rc = disk->ops->init(disk);                   /* 直接调用注册的 init，保留该后端的实际返回状态。 */
	}

	return rc;                                            /* 将本次后端初始化的结果传回磁盘消费者。 */
}

/**
 * @brief  查询指定磁盘的后端状态。
 * @param  pdrv 非空、以 NUL 结尾的注册名称。
 * @retval 就绪为 DISK_STATUS_OK，其他介质状态由 DISK_STATUS_* 表示。名称未注册或缺少状态入口时返回 -EINVAL。
 */
int disk_access_status(const char *pdrv)
{
	struct disk_info *disk = disk_access_get_di(pdrv);    /* 根据注册名取得保存实际介质及回调表的实例。 */
	int rc = -EINVAL;                                     /* 预置 -EINVAL，涵盖查无此盘或驱动缺少入口。 */

	/* 核实实例、操作表与 status 回调均有效后，再执行介质状态查询。 */
	if ((disk != NULL) && (disk->ops != NULL) &&
				(disk->ops->status != NULL)) {
		rc = disk->ops->status(disk);                 /* 直接调用注册的 status，保留该后端的实际返回状态。 */
	}

	return rc;                                            /* 将本次介质状态查询的结果传回磁盘消费者。 */
}

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
		     uint32_t start_sector, uint32_t num_sector)
{
	struct disk_info *disk = disk_access_get_di(pdrv);                         /* 根据注册名取得保存实际介质及回调表的实例。 */
	int rc = -EINVAL;                                                          /* 预置 -EINVAL，涵盖查无此盘或驱动缺少入口。 */

	/* 核实实例、操作表与 read 回调均有效后，再执行逻辑扇区读取。 */
	if ((disk != NULL) && (disk->ops != NULL) &&
				(disk->ops->read != NULL)) {
		rc = disk->ops->read(disk, data_buf, start_sector, num_sector);    /* 直接调用注册的 read，保留该后端的实际返回状态。 */
	}

	return rc;                                                                 /* 将本次逻辑扇区读取的结果传回磁盘消费者。 */
}

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
		      uint32_t start_sector, uint32_t num_sector)
{
	struct disk_info *disk = disk_access_get_di(pdrv);                          /* 根据注册名取得保存实际介质及回调表的实例。 */
	int rc = -EINVAL;                                                           /* 预置 -EINVAL，涵盖查无此盘或驱动缺少入口。 */

	/* 核实实例、操作表与 write 回调均有效后，再执行逻辑扇区写入。 */
	if ((disk != NULL) && (disk->ops != NULL) &&
				(disk->ops->write != NULL)) {
		rc = disk->ops->write(disk, data_buf, start_sector, num_sector);    /* 直接调用注册的 write，保留该后端的实际返回状态。 */
	}

	return rc;                                                                  /* 将本次逻辑扇区写入的结果传回磁盘消费者。 */
}

/**
 * @brief  向指定磁盘发出容量查询、缓存同步或其他设备控制请求。
 * @param  pdrv 非空的注册磁盘名。
 * @param  cmd DISK_IOCTL_* 命令编号。
 * @param  buf 与命令约定匹配的参数或输出缓冲，无参数命令可传 NULL。
 * @retval 0 表示后端报告成功，找不到磁盘或回调时返回 -EINVAL，后端错误原样返回。
 */
int disk_access_ioctl(const char *pdrv, uint8_t cmd, void *buf)
{
	struct disk_info *disk = disk_access_get_di(pdrv);    /* 根据注册名取得保存实际介质及回调表的实例。 */
	int rc = -EINVAL;                                     /* 预置 -EINVAL，涵盖查无此盘或驱动缺少入口。 */

	/* 核实实例、操作表与 ioctl 回调均有效后，再执行设备控制请求。 */
	if ((disk != NULL) && (disk->ops != NULL) &&
				(disk->ops->ioctl != NULL)) {
		rc = disk->ops->ioctl(disk, cmd, buf);        /* 直接调用注册的 ioctl，保留该后端的实际返回状态。 */
	}

	return rc;                                            /* 将本次设备控制请求的结果传回磁盘消费者。 */
}

/**
 * @brief  将磁盘实例加入系统注册表，以便消费者通过名称访问。
 * @param  disk 待注册的持久实例，名称非空且唯一，节点和操作表要在使用期间保持有效。
 * @retval 0 成功，空实例、空名称或同名实例已注册时为 -EINVAL。
 */
int disk_access_register(struct disk_info *disk)
{
	int rc = 0;                                                /* 假设可注册，校验失败时改为负错误码。 */

	/* 在同一互斥区内检查参数、查重并追加，防止同名实例并发进入。 */
	k_mutex_lock(&mutex, K_FOREVER);
	if ((disk == NULL) || (disk->name == NULL)) {              /* 拒绝没有实例或没有可查询名称的注册请求。 */
		LOG_ERR("invalid disk interface!!");               /* 记录无效接口，避免继续解引用。 */
		rc = -EINVAL;                                      /* 将本次注册结果设为参数错误。 */
		goto reg_err;                                      /* 统一通过 reg_err 解锁。 */
	}

	/* 检查当前名称是否已存在。Zephyr 互斥锁允许持锁线程在查找帮助函数内再次取得同一把锁。 */
	if (disk_access_get_di(disk->name) != NULL) {
		LOG_ERR("disk interface already registered!!");    /* 记录重复名称。 */
		rc = -EINVAL;                                      /* 同名占用按参数错误返回。 */
		goto reg_err;                                      /* 已存在同名实例时跳过插入。 */
	}

	/* 查重通过，将持久实例自己的链表节点挂到注册表尾部。 */
	sys_dlist_append(&disk_access_list, &disk->node);
	LOG_DBG("disk interface(%s) registred", disk->name);       /* 记录新注册的磁盘名。 */
reg_err:
	k_mutex_unlock(&mutex);                                    /* 无论是否成功都只在完成表操作后释放外层锁。 */
	return rc;                                                 /* 将确定的注册结果返回启动代码或驱动。 */
}

/**
 * @brief  从系统注册表中摘除给定实例。
 * @param  disk 最初传给注册函数的同一实例
 * @param  调用方须与正在使用该实例的线程协调其生命周期。
 * @retval 0 成功，空实例、空名称或查询不到该名称时为 -EINVAL。
 * @note   实现按名称查存在性，但实际移除的是传入实例自身的链表节点。
 */
int disk_access_unregister(struct disk_info *disk)
{
	int rc = 0;                                               /* 先按成功处理，参数或查找出错时写入 -EINVAL。 */

	/* 锁中检查注册名并摘除节点，调用方须提供当初注册的同一对象。 */
	k_mutex_lock(&mutex, K_FOREVER);
	if ((disk == NULL) || (disk->name == NULL)) {             /* 空实例或名称为空时无法核对注册状态。 */
		LOG_ERR("invalid disk interface!!");              /* 记录注销参数异常。 */
		rc = -EINVAL;                                     /* 给调用方准备参数错误。 */
		goto unreg_err;                                   /* 从统一出口解锁。 */
	}

	/* 若表中找不到该名，则不允许对传入节点执行删除。 */
	if (disk_access_get_di(disk->name) == NULL) {
		LOG_ERR("disk interface not registered!!");       /* 输出找不到注册实例的诊断。 */
		rc = -EINVAL;                                     /* 将结果置为参数错误。 */
		goto unreg_err;                                   /* 保留链表现状并转清理。 */
	}
	/* 按传入实例的 node 摘链，所以不能用另一个同名对象代替已注册对象。 */
	sys_dlist_remove(&disk->node);
	LOG_DBG("disk interface(%s) unregistred", disk->name);    /* 记录已注销的磁盘名。 */
unreg_err:
	k_mutex_unlock(&mutex);                                   /* 在返回前释放注册表锁。 */
	return rc;                                                /* 返回操作结果。 */
}

/**
 * @brief  启动时建立磁盘注册表和保护它的互斥锁。
 * @param  dev Zephyr 系统初始化统一入口参数，此处不依赖设备实例。
 * @retval 固定返回 0。
 */
static int disk_init(const struct device *dev)
{
	ARG_UNUSED(dev);                      /* 此初始化面向整个注册表，框架传入的设备对象在此处不使用。 */

	/* 在存储驱动登记前准备互斥锁及空的双向注册链表。 */
	k_mutex_init(&mutex);
	sys_dlist_init(&disk_access_list);    /* 初始化链表哨兵，使首次查找可安全返回未匹配。 */
	return 0;                             /* 两项初始化均完成后成功返回。 */
}

/* 内核可用之后，在 POST_KERNEL 的默认初始化优先级建立磁盘访问层。 */
SYS_INIT(disk_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
