/*
 * Copyright (c) 2016 Actions Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* NOR 块设备把 512 字节逻辑扇区映射到 UDISK 分区；对不足 4096 字节擦除块的改写使用单块 RAM 缓存。 */
#include <string.h>
#include <zephyr/types.h>
#include <sys/__assert.h>
#include <sys/util.h>
/* 兼容头转入公共磁盘 API；底层 flash、分区和板级设备名决定实际访问哪一颗 NOR。 */
#include <disk/disk_access.h>
#include <errno.h>
#include <init.h>
#include <device.h>
#include <drivers/flash.h>
#include <partition/partition.h>
#include <board_cfg.h>


#define NOR_SECTOR_SIZE 512                                       /* 对外暴露的逻辑扇区大小，单位字节；下方左移 9 位完成扇区到字节的换算。 */
#define NOR_ERASE_SIZE 4096                                       /* 擦除与回写的物理块粒度，单位字节。 */
#define NOR_CACHE_NUM_SECTOR  (NOR_ERASE_SIZE/NOR_SECTOR_SIZE)    /* 每个擦除块可容纳八个逻辑扇区，掩码运算依赖此数量为二的幂。 */

#define STA_DISK_OK		0x08                              /* 介质检测命令写回的状态字节，与 disk_status 的 DISK_STATUS_OK 分属不同接口。 */


/* 指向已绑定的 NOR flash 设备。驱动操作表以此指针判断介质是否可用。 */
const struct device *nor_disk;
/* 串行化缓存内容、所属擦除块、有效标志和相关闪存读写。 */
static struct k_mutex n_mutex;
/* 无 PSRAM 配置把缓存放入专用段，其他配置沿用普通静态存储。 */
#ifdef CONFIG_SOC_NO_PSRAM
__in_section_unique(diskio.cache.pool)
#endif
static u8_t __aligned(4) nor_cache_data[NOR_ERASE_SIZE];    /* 4 字节对齐的单擦除块缓存，保存修改扇区及同一块内仍应保留的其他数据。 */
static bool  b_cache_valid, b_write_valid;                  /* 前者表示当前缓存记录了块，后者表示有扇区被修改、需要回写。 */
static uint32_t nor_cache_start_sec;                        /* 当前缓存对应的物理起始扇区号，按八个扇区对齐。 */


/**
 * @brief  查询 NOR 闪存设备是否已经绑定。
 * @param  disk 操作表统一传入的磁盘描述，本实现查询模块级设备指针。
 * @retval 已绑定返回 DISK_STATUS_OK，否则返回 DISK_STATUS_NOMEDIA。
 */
static int nor_disk_status(struct disk_info *disk)
{
	if (!nor_disk) {                       /* 检查启动阶段是否已经取得 flash 设备指针。 */
		return DISK_STATUS_NOMEDIA;    /* 未绑定时报告无介质状态位。 */
	}
	return DISK_STATUS_OK;                 /* 此后端只根据设备指针报告就绪状态。 */
}

/**
 * @brief  在磁盘访问层初始化阶段确认 NOR 设备已经就绪。
 * @param  disk 磁盘操作表提供的实例，本函数仅检查启动时的设备绑定。
 * @retval 0 表示设备已绑定，否则为 -ENODEV。
 */
static int nor_disk_initialize(struct disk_info *disk)
{
	if (nor_disk) {      /* 启动时已绑定设备，就不用在每次文件系统初始化时重建缓存。 */
		return 0;    /* 设备已存在，块接口可以使用。 */
	}
	return -ENODEV;      /* 绑定缺失按设备不存在返回。 */
}
/**
 * @brief  将脏缓存的整个 4096 字节擦除块写回 NOR。
 * @param  无。
 * @retval 无脏数据时为 0；否则返回 flash_erase 与 flash_write 的按位或结果。
 * @note   调用处负责持有 n_mutex。此实现即使擦除失败也会调用写入，并在两次调用后清除脏标志。
 */
static int nor_cache_flush(void)
{
	int ret = 0;                                                                                     /* 无脏块时保留成功，调用者无需触碰 flash。 */

	/* 仅回写已修改块。整块擦除后再写整份缓存，覆盖原始介质上的全部八个扇区。 */
	if(b_write_valid){
		ret =  flash_erase(nor_disk, nor_cache_start_sec<<9, NOR_ERASE_SIZE);                    /* 将缓存物理起始扇区换算为字节地址并擦除 4096 字节。 */
		ret |= flash_write(nor_disk, nor_cache_start_sec<<9, nor_cache_data, NOR_ERASE_SIZE);    /* 即使擦除失败也会尝试写入，把两项操作的状态按位或合并。 */
		b_write_valid = false;                                                                   /* 本实现无论回写是否成功都会清除脏标志。 */
		/* 停用的串口跟踪可显示正在回写的块地址。 */
		//qprintk("nor flush off=0x%x\n", nor_cache_start_sec*512);
	}
	return ret;                                                                                      /* 返回合并的结果；是否检查取决于调用方。 */
}
/**
 * @brief  将一个擦除块载入 RAM 缓存，以便保留未被本次写操作覆盖的相邻扇区。
 * @param  off_sec 必须按八个扇区对齐的物理起始扇区号。
 * @retval 无。
 * @note   调用方持有 n_mutex；本实现未向上报告旧块刷新或新块读取失败。
 */
static void nor_cache_buf_update(uint32_t off_sec)
{
	/* 检查调用方给的是擦除块的起始扇区，防止缓存覆盖相邻块。 */
	if(off_sec & (NOR_CACHE_NUM_SECTOR-1)){
		printk("-----error, off_sec=0x%x not align to erase size-----\n", off_sec);              /* 记录未按八扇区对齐的地址。 */
		return;                                                                                  /* 保留缓存现状，函数没有返回错误码。 */
	}

	/* 已载入缓存时，只有请求落在另一擦除块才切换。 */
	if(b_cache_valid){
		if(off_sec != nor_cache_start_sec){                                                      /* 命中同一块则保留脏数据，不用重新读取。 */
			nor_cache_flush();                                                               /* 尝试把旧块的修改写回；此处未接收该调用的状态。 */
			nor_cache_start_sec = off_sec;                                                   /* 更新为用户将要修改的块号。 */
			flash_read(nor_disk, nor_cache_start_sec<<9, nor_cache_data, NOR_ERASE_SIZE);    /* 读完整新块，供调用方修改其中部分扇区；返回码未检查。 */
		}

	/* 缓存尚未使用时，直接装入整块原始数据。 */
	}else{			
		nor_cache_start_sec = off_sec;                                                           /* 登记新缓存的物理起点。 */
		flash_read(nor_disk, nor_cache_start_sec<<9, nor_cache_data, NOR_ERASE_SIZE);            /* 读取整块，让未覆盖的扇区也有原始副本。 */
		b_cache_valid = true;                                                                    /* 此处直接标记有效，未按 flash_read 的结果分支。 */
	}
}

/**
 * @brief  读取 NOR 磁盘的逻辑扇区，优先从命中起始扇区的缓存中取数。
 * @param  disk 当前磁盘分区描述。
 * @param  buff 可写缓冲，容量按请求扇区数乘以 512 字节预留。
 * @param  start_sector 分区内的起始扇区号。
 * @param  sector_count 请求读取的扇区数。
 * @retval 0 完成；未绑定设备为 -ENODEV，起点越界、数量为零或底层读失败为 -EIO。
 * @note   尾部超出分区时截短；仅当请求起点位于缓存块时先复制缓存，剩余范围直接读 flash。
 */
static int nor_disk_read(struct disk_info *disk, uint8_t *buff, uint32_t start_sector,
		     uint32_t sector_count)
{
	int ret = 0;                                                                   /* 记录直接 flash 读取结果；完全命中缓存时保持零。 */
	uint32_t nsec, off_sec, sec_index;                                             /* 分别表示本次复制扇区数、对齐块起点及块内下标。 */

	/* 检查设备和逻辑区间，再把分区内扇区号转换为物理扇区号。 */
	if (!nor_disk ) {
		return -ENODEV;                                                        /* 设备不存在，无法提供数据。 */
	}

	if ((start_sector >= disk->sector_cnt) || (sector_count == 0)){                /* 拒绝从分区之外开始读取，也拒绝零扇区请求。 */
		return -EIO;                                                           /* 以 I/O 错误报告不可执行的范围。 */
	}
	if (start_sector + sector_count > disk->sector_cnt) {                          /* 起点合法但结尾超出分区时，缩短本次请求。 */
		sector_count = disk->sector_cnt - start_sector;                        /* 限制为从起点到分区末端尚有的扇区数。 */
	}
	start_sector += disk->sector_offset;                                           /* 加入分区的物理起点后才可与缓存块号比较。 */

	/* 停用的诊断记录物理起点及请求扇区数。 */
	//printk("-nor disk read off=0x%x , sec=0x%x\n", start_sector , sector_count);

	/* 在锁中优先读取覆盖请求起点的缓存，使尚未刷新的改写可见。 */
	k_mutex_lock(&n_mutex, K_FOREVER);

	if(b_cache_valid){                                                             /* 只有缓存已装载才比较块号。 */
		off_sec = start_sector & (~(NOR_CACHE_NUM_SECTOR-1));                  /* 屏蔽低三位得到请求起点所在的物理块。 */
		if(off_sec == nor_cache_start_sec){                                    /* 仅在请求起点命中缓存块时进入此复制路径。 */
			sec_index = (start_sector & (NOR_CACHE_NUM_SECTOR-1));         /* 得到请求起点在缓存中的扇区下标。 */
			nsec = NOR_CACHE_NUM_SECTOR - sec_index;                       /* 最多可以从此下标复制到所在块结尾。 */
			if(nsec > sector_count)                                        /* 实际请求较短时按剩余请求量读取。 */
				nsec = sector_count;                                   /* 将复制范围限制在调用者所请求的扇区数内。 */
			memcpy(buff, nor_cache_data+(sec_index<<9), nsec<<9);          /* 按 512 字节换算缓存偏移和复制字节数。 */
			buff += nsec<<9;                                               /* 把输出指针移至已复制内容之后。 */
			start_sector += nsec;                                          /* 后续物理读取从首段之后开始。 */
			sector_count -= nsec;                                          /* 扣除已从缓存取得的扇区。 */
		}
	}

	/* 若尚有剩余范围，直接从 flash 连续读取。 */
	if(sector_count){
		/* 此处保留了停用的刷新调用，当前直接读取不会先写回缓存。 */
		//nor_cache_flush();
		ret = flash_read(nor_disk, start_sector<<9, buff, sector_count<<9);    /* 将物理起点及剩余长度换算为字节后调用 flash_read。 */
	}
	k_mutex_unlock(&n_mutex);                                                      /* 释放互斥锁后才将设备状态转换为公共错误。 */
	if(ret)                                                                        /* 底层读取返回非零表示失败。 */
		return -EIO;                                                           /* 将 flash 访问失败归一为 -EIO。 */
	return 0;                                                                      /* 已完成当前有效范围的读取。 */
}

/**
 * @brief  处理 NOR 的扇区写入，用读改写缓存保留未覆盖的块首或块尾。
 * @param  disk 当前磁盘分区描述。
 * @param  buff 至少含请求扇区对应字节数的只读来源。
 * @param  start_sector 分区内的起始扇区。
 * @param  sector_count 请求写入的扇区数。
 * @retval 0 表示操作完成或部分数据已进入缓存，-ENODEV 为设备缺失，-EIO 为起点、零长度或整块擦写错误。
 * @note   本函数会把超过分区末端的请求截短；内部缓存切换不传回刷新错误。
 */
static int nor_disk_write(struct disk_info *disk, const uint8_t *buff, uint32_t start_sector,
		      uint32_t sector_count)
{
	int ret = 0;                                                                                                          /* 初值为零，仅整块直写的擦除或写入结果会合并进此值。 */
	uint32_t nsec, off_sec, sec_index;                                                                                    /* 保存首尾分段所需的扇区数、对齐起点和块内下标。 */

	/* 核实设备和分区范围，截短越过分区末端的尾部。 */
	if (!nor_disk ) {
		return -ENODEV;                                                                                               /* 无设备直接报 -ENODEV。 */
	}

	if ((start_sector >= disk->sector_cnt) || (sector_count == 0)){                                                       /* 起始扇区超界或请求数量为零时拒绝写入。 */
		return -EIO;                                                                                                  /* 错误发生在逻辑范围检查阶段。 */
	}
	if (start_sector + sector_count > disk->sector_cnt) {                                                                 /* 合法起点加请求长度超过分区时调整写入数。 */
		sector_count = disk->sector_cnt - start_sector;                                                               /* 仅保留该分区内还能容纳的扇区数。 */
	}
	start_sector += disk->sector_offset;                                                                                  /* 从分区内编号换到物理扇区地址。 */

	/* 停用的写入跟踪会打印换算后的字节偏移和扇区数。 */
	//printk("-nor disk write off=0x%x , sec=0x%x\n", start_sector*512 , sector_count);

	/* 在缓存锁内分三段写入，先将未对齐的块首合并到整块镜像。 */
	k_mutex_lock(&n_mutex, K_FOREVER);
	sec_index = (start_sector & (NOR_CACHE_NUM_SECTOR-1));                                                                /* 低三位给出本次写入在擦除块内的起始扇区。 */
	if(sec_index) {                                                                                                       /* 块内起点非零时需要保留它前面的扇区。 */
		off_sec = start_sector & (~(NOR_CACHE_NUM_SECTOR-1)) ;                                                        /* 向下对齐到首个受影响的块起点。 */
		nor_cache_buf_update(off_sec);                                                                                /* 取得该块的完整镜像，必要时先切换并刷新旧块。 */
		nsec = NOR_CACHE_NUM_SECTOR - sec_index;                                                                      /* 计算到块末尾所能写入的最大扇区数。 */
		if(nsec > sector_count)                                                                                       /* 若请求未延伸到块尾则按实际剩余数处理。 */
			nsec = sector_count;                                                                                  /* 截短本次首段。 */
		memcpy(nor_cache_data+(sec_index<<9), buff, nsec<<9);                                                         /* 只改写对应扇区范围，保留同一物理块内其他字节。 */
		buff += nsec<<9;                                                                                              /* 输入指针越过已经合并到缓存的字节。 */
		start_sector += nsec;                                                                                         /* 推进物理扇区游标。 */
		sector_count -= nsec;                                                                                         /* 从待处理长度中扣除首段。 */
		b_write_valid = true;                                                                                         /* 记录缓存已有尚未持久化的修改。 */
		/* 保留原有停用的首段块地址跟踪。 */
		//printk("head off=0x%x\n", nor_cache_start_sec*512);
	}

	/* 中段含完整擦除块时直接擦写 flash，避免额外缓存搬运。 */
	if(sector_count >= NOR_CACHE_NUM_SECTOR){
		nsec = sector_count & (~(NOR_CACHE_NUM_SECTOR-1));                                                            /* 将剩余扇区数向下对齐到八扇区的整倍数。 */
		ret = flash_erase(nor_disk,  start_sector<<9, nsec<<9);                                                       /* 一次擦除将被完整覆盖的物理范围。 */
		ret |= flash_write(nor_disk, start_sector<<9, buff, nsec<<9);                                                 /* 随后写入同样长度的数据，累计前后两次调用的状态。 */
		if(b_cache_valid && (start_sector <= nor_cache_start_sec) && (nor_cache_start_sec <= start_sector +nsec)){    /* 按实际包含两个端点的条件判断缓存块起点是否位于直写区间或其紧邻末端。 */
			b_cache_valid = false;                                                                                /* 判为重叠时旧块内容不能继续作为有效缓存。 */
			b_write_valid = false;                                                                                /* 同时清除脏标志，此分支不会另行刷新原缓存。 */
			printk("nor cache lost off=0x%x\n", nor_cache_start_sec*512);                                         /* 记录失效缓存的字节地址。 */
		}
		buff += nsec<<9;                                                                                              /* 输入游标推进到整块段之后。 */
		start_sector += nsec;                                                                                         /* 更新下一段的物理起始扇区。 */
		sector_count -= nsec;                                                                                         /* 扣除已经直写的扇区数。 */
	}

	/* 尾段不足一个完整块时，加载结尾块原内容，再替换前部扇区。 */
	if(sector_count){
		nor_cache_buf_update(start_sector);                                                                           /* 整块中段结束后的起点也是尾块的对齐起点。 */
		memcpy(nor_cache_data, buff, sector_count<<9);                                                                /* 从缓存首字节开始覆盖请求中的尾部数据。 */
		/* 停用的尾段跟踪记录本次缓存块物理地址。 */
		//printk("tail off=0x%x\n", nor_cache_start_sec*512);
		b_write_valid = true;                                                                                         /* 尾部修改留待后续刷新。 */
	}

	/* 释放缓存锁，再将直写阶段已经记录的结果返回给访问层。 */
	k_mutex_unlock(&n_mutex);
	if(ret)                                                                                                               /* 这里只检查本函数保留的整块直写状态。 */
		return -EIO;                                                                                                  /* 底层擦写任一返回非零时映射为 -EIO。 */
	return 0;                                                                                                             /* 已写入介质或已合并入缓存的请求按成功返回。 */
}

/**
 * @brief  执行 NOR 盘的同步、扇区查询或介质检测。
 * @param  disk 当前 NOR 分区描述。
 * @param  cmd DISK_IOCTL_* 操作编号。
 * @param  buff 数量、大小查询需可写 uint32_t，介质检测需可写 uint8_t，同步命令不使用它。
 * @retval 识别的命令返回 0，未绑定设备返回 -ENODEV，未支持的命令返回 -EINVAL。
 * @note   同步调用未检查刷新结果；擦除块大小查询在此实现实际返回 512，和物理擦除粒度 4096 字节有区别。
 */
static int nor_disk_ioctl(struct disk_info *disk, uint8_t cmd, void *buff)
{
	int ret = 0;                                     /* 当前支持的命令均沿此变量返回零。 */
	if (!nor_disk )                                  /* 控制命令也必须先有有效 NOR 设备。 */
		return -ENODEV;                          /* 无设备时不访问输入输出缓冲。 */

	/* 按命令决定缓冲区布局与返回值；同步命令持锁操作共享缓存。 */
	switch (cmd) {
	case DISK_IOCTL_CTRL_SYNC:                       /* 请求把脏块提交到物理介质。 */
		printk("-nor sync\n");                   /* 输出同步动作诊断。 */
		k_mutex_lock(&n_mutex, K_FOREVER);       /* 等待缓存锁，避免同步时正在合并其他扇区。 */
		nor_cache_flush();                       /* 尝试刷新，但本分支没有把其返回码存入 ret。 */
		k_mutex_unlock(&n_mutex);                /* 释放缓存锁。 */
		break;                                   /* 回到统一返回位置。 */
	case DISK_IOCTL_GET_SECTOR_COUNT:                /* 请求逻辑分区的扇区总数。 */
		*(uint32_t *)buff = disk->sector_cnt;    /* 通过 uint32_t 输出本实例注册的扇区数量。 */
		break;                                   /* 此命令已完成。 */
    case DISK_IOCTL_GET_SECTOR_SIZE:                     /* 请求单个逻辑扇区的字节数。 */
        *(uint32_t *)buff = NOR_SECTOR_SIZE;             /* 写回逻辑扇区的 512 字节大小。 */
        break;                                           /* 返回给公共控制入口。 */
    case DISK_IOCTL_GET_ERASE_BLOCK_SZ:                  /* 请求擦除块尺寸；需要以此后端实际填入的值解释。 */
        *(uint32_t *)buff  = NOR_SECTOR_SIZE;            /* 此处写回 NOR_SECTOR_SIZE 即 512；物理擦除块实际为 4096 字节。 */
        break;                                           /* 该分支直接报告成功。 */
	case DISK_IOCTL_HW_DETECT:                       /* 查询是否检测到可用磁盘。 */
		*(uint8_t *)buff = STA_DISK_OK;          /* 以一个字节返回介质检测状态 0x08。 */
		break;                                   /* 结束介质检测分支。 */
	default:                                         /* 其余命令没有在此 NOR 后端实现。 */
		return -EINVAL;                          /* 按参数不受支持返回 -EINVAL。 */
	}

	return ret;                                      /* 对已识别的命令返回零。 */
}

/* NOR 的磁盘操作表。磁盘访问层经 disk_info.ops 把请求绑定到以下实际函数。 */
struct disk_operations disk_nor_operation = {
	.init		= nor_disk_initialize,    /* 初始化调用检查启动阶段的 NOR 设备绑定。 */
	.status		= nor_disk_status,        /* 状态查询由 NOR 设备指针给出有无介质。 */
	.read		= nor_disk_read,          /* 使用缓存命中或直接 flash_read 完成读。 */
	.write		= nor_disk_write,         /* 写入按块首、整块中段和尾块拆分。 */
	.ioctl		= nor_disk_ioctl,         /* 同步、几何信息和检测请求走控制分发。 */
};

/* 系统注册的 NOR 逻辑盘实例，扇区偏移和数量在启动时从 UDISK 分区表填入。 */
struct disk_info disk_nor_mass = {
	.name		= "NOR",                  /* 文件系统和访问层据此名称查找实例。 */
	.sector_size = NOR_SECTOR_SIZE,           /* 逻辑扇区固定为 512 字节。 */
	.sector_offset	= 0,                      /* 启动前缺省偏移为零。 */
	.sector_cnt	= 0,                      /* 初值尚无可寻址扇区。 */
	.ops		= &disk_nor_operation,    /* 把公共块设备回调绑定到本文件提供的实现。 */
};

/**
 * @brief  根据 UDISK 分区所在存储器绑定 NOR 设备，并登记逻辑磁盘实例。
 * @param  dev 启动框架保留参数，此处从分区表与板级设备名寻找后端。
 * @retval 未绑定到 NOR 设备返回 -ENODEV；其余路径返回 0，注册调用的错误未向上返回。
 */
static int nor_disk_init(const struct device *dev)
{
	const struct partition_entry *parti;                                                /* 指向分区表中选出的 UDISK 记录，用于换算物理范围。 */

	ARG_UNUSED(dev);                                                                    /* 设备实际由存储 ID 和板级名称选择。 */

	/* 先建立缓存锁并清掉缓存与绑定状态，再按分区所属设备选择后端。 */
	k_mutex_init(&n_mutex);
	b_write_valid = b_cache_valid = false;                                              /* 清除脏标志和缓存有效位。 */
	nor_disk = NULL;                                                                    /* 在确定设备来源前按未绑定处理。 */
	parti = partition_get_stf_part(STORAGE_ID_DATA_NOR, PARTITION_FILE_ID_UDISK);       /* 优先查询数据 NOR 设备上的 UDISK 分区。 */
	if(parti == NULL){                                                                  /* 找不到数据 NOR 分区时尝试常规 NOR。 */
		printk("nor disk:ext nor\n");                                               /* 记录进入后备设备选择路径。 */
		parti = partition_get_stf_part(STORAGE_ID_NOR, PARTITION_FILE_ID_UDISK);    /* 到常规 NOR 的分区表继续查找 UDISK。 */
		if(parti)                                                                   /* 仅当该设备确有相应分区时绑定。 */
			nor_disk = device_get_binding(CONFIG_SPI_FLASH_NAME);               /* 用板级主 SPI flash 的设备名查找实例。 */
	}else{                                                                              /* 数据 NOR 分区存在时使用第二个 flash 设备名。 */
		printk("nor disk:data nor\n");                                              /* 记录选择了数据 NOR。 */
		nor_disk = device_get_binding(CONFIG_SPI_FLASH_2_NAME);                     /* 取得板级第二 SPI flash 实例。 */
	}

	/* 检查绑定结果，再把分区的字节偏移及容量换成对外的逻辑扇区。 */
	if (!nor_disk) {
		printk("nor disk: not nor dev\n");                                          /* 记录启动找不到 NOR 后端。 */
		return -ENODEV;                                                             /* 无法向上层注册可用磁盘。 */
	}

	if(parti != NULL){                                                                  /* 应用所选分区的边界信息。 */
		disk_nor_mass.sector_offset = parti->offset >> 9;                           /* 分区字节偏移除以 512 转为物理起始扇区。 */
		disk_nor_mass.sector_cnt = parti->size >> 9;                                /* 分区字节长度除以 512 转为逻辑磁盘扇区数。 */
	}

	/* 输出识别的扇区总数与起点，并发布 NOR 磁盘实例。 */
	printk("nor_disk_init,size=0x%x, off=0x%x secotr\n", disk_nor_mass.sector_cnt, disk_nor_mass.sector_offset);
	disk_access_register(&disk_nor_mass);                                               /* 注册后公共磁盘层可以通过 NOR 名称找到该操作表，此处未检查注册返回值。 */
	return 0;                                                                           /* 当前启动入口固定以成功结束此路径。 */
}

/* 由 Zephyr 在 POST_KERNEL 的默认初始化优先级调用 NOR 后端启动入口。 */
SYS_INIT(nor_disk_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

