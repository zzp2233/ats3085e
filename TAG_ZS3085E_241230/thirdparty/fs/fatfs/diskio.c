/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2016        */
/*-----------------------------------------------------------------------*/
/* 本项目通过卷名表把 FatFs 扇区号转换为 Zephyr 磁盘读写请求。缓存与直接访问由构建配置选择。 */

#include "diskio.h"    /* 引入 FatFs 物理磁盘接口及 Zephyr 磁盘服务。 */
#include <ffconf.h>    /* 引入卷数和卷名映射配置。 */

#define DISK_MAX_PHY_DRV	_VOLUMES    /* 用编译期的卷数限制 pdrv 下标。 */

/* 索引是物理卷号，值是磁盘层注册时采用的设备名称。 */
const char* const disk_volume_strs[] = {
	_VOLUME_STRS    /* 展开配置中的卷名表。 */
};




/**
  * @brief  把磁盘返回码转换为 FatFs 使用的值。
  * @param  error：[输入]底层磁盘操作返回的原始整数。
  * @retval 0 转 RES_OK，正值 EINVAL、ENODEV 分别转 RES_PARERR、RES_NOTRDY，其他值转 RES_ERROR。
  * @note   代码按原始符号匹配，负 errno 会落入默认分支。
  */
static DSTATUS translate_error(int error)
{
	switch (error) {
	case 0:
		return RES_OK;        /* 正常完成映射为 FatFs 成功码。 */
	case EINVAL:
		return RES_PARERR;    /* 仅匹配正值 EINVAL。 */
	case ENODEV:
		return RES_NOTRDY;    /* 仅匹配正值 ENODEV。 */
	default:
		return RES_ERROR;     /* 对其余结果一律报告通用错误。 */
	}
}

/**
  * @brief  读取指定卷的磁盘状态。
  * @param  pdrv：[输入]从 0 开始的物理卷号，必须小于 _VOLUMES。
  * @retval 成功为 RES_OK；底层结果经 translate_error 转换，驱动编号越界时原样返回 STA_NODISK。
  * @note   卷号先映射为名称，再由磁盘层分派。
  */
DSTATUS disk_status (
	BYTE pdrv    /* 待访问的物理卷索引。 */
)
{
	int result;                                             /* 保存磁盘层返回状态，退出前转换为 FatFs 结果。 */

	if (pdrv >= DISK_MAX_PHY_DRV)                           /* 索引越界时不能读取卷名表。 */
		return STA_NODISK;                              /* 返回该实现用于未提供卷号的状态值。 */

	result = disk_access_status(disk_volume_strs[pdrv]);    /* 把卷号映射成设备名再查询状态。 */
	return translate_error(result);                         /* 将刚才的磁盘层结果映射回 FatFs。 */
}





/**
  * @brief  初始化指定卷对应的磁盘设备。
  * @param  pdrv：[输入]从 0 开始的物理卷号，必须小于 _VOLUMES。
  * @retval 成功为 RES_OK；底层结果经 translate_error 转换，驱动编号越界时原样返回 STA_NODISK。
  * @note   若启用扇区缓存，先使旧条目失效。
  */
DSTATUS disk_initialize (
	BYTE pdrv    /* 待访问的物理卷索引。 */
)
{
	int result;                                           /* 保存磁盘层返回状态，退出前转换为 FatFs 结果。 */

	if (pdrv >= DISK_MAX_PHY_DRV)                         /* 索引越界时不能读取卷名表。 */
		return STA_NODISK;                            /* 返回该实现用于未提供卷号的状态值。 */

#ifdef CONFIG_DISKIO_CACHE
	diskio_cache_invalid(disk_volume_strs[pdrv]);         /* 缓存模式下使该卷旧缓存失效。 */
#endif

	result = disk_access_init(disk_volume_strs[pdrv]);    /* 按名称初始化选定的块设备。 */
	return translate_error(result);                       /* 将刚才的磁盘层结果映射回 FatFs。 */

}



/**
  * @brief  从连续扇区读取数据。
  * @param  pdrv：[输入]从 0 开始的物理卷号，必须小于 _VOLUMES。
  * @param  buff：[输出]非空且可写，至少 count × 扇区大小字节。
  * @param  sector：[输入]起始逻辑扇区号 LBA。
  * @param  count：[输入]连续扇区数。
  * @retval 成功为 RES_OK；底层结果经 translate_error 转换，驱动编号越界时原样返回 STA_NODISK。
  * @note   缓存构建可走扇区缓存，否则直接交由 disk_access_read。
  */
DRESULT disk_read (
	BYTE pdrv,       /* 待访问的物理卷索引。 */
	BYTE *buff,      /* 可写的读扇区目标缓冲。 */
	DWORD sector,    /* 第一个扇区的逻辑块地址。 */
	UINT count       /* 要搬运的连续扇区数。 */
)
{
	int result;                                                                 /* 保存磁盘层返回状态，退出前转换为 FatFs 结果。 */

	if (pdrv >= DISK_MAX_PHY_DRV)                                               /* 索引越界时不能读取卷名表。 */
		return STA_NODISK;                                                  /* 返回该实现用于未提供卷号的状态值。 */

#ifdef CONFIG_DISKIO_CACHE
	result = diskio_cache_read(disk_volume_strs[pdrv], buff, sector, count);    /* 缓存开启时由缓存层提供扇区数据。 */
#else
	result = disk_access_read(disk_volume_strs[pdrv], buff, sector, count);     /* 无缓存分支直接发出读扇区请求。 */
#endif
	return translate_error(result);                                             /* 将刚才的磁盘层结果映射回 FatFs。 */
}



/**
  * @brief  向连续扇区写入数据。
  * @param  pdrv：[输入]从 0 开始的物理卷号，必须小于 _VOLUMES。
  * @param  buff：[输入]非空且可读，至少 count × 扇区大小字节。
  * @param  sector：[输入]起始逻辑扇区号 LBA。
  * @param  count：[输入]连续扇区数。
  * @retval 成功为 RES_OK；底层结果经 translate_error 转换，驱动编号越界时原样返回 STA_NODISK。
  * @note   缓存构建允许通过缓存写入，CTRL_SYNC 可用于同步。
  */
DRESULT disk_write (
	BYTE pdrv,           /* 待访问的物理卷索引。 */
	const BYTE *buff,    /* 保持有效至写入返回的源缓冲。 */
	DWORD sector,        /* 第一个扇区的逻辑块地址。 */
	UINT count           /* 要搬运的连续扇区数。 */
)
{
	int result;                                                                  /* 保存磁盘层返回状态，退出前转换为 FatFs 结果。 */

	if (pdrv >= DISK_MAX_PHY_DRV)                                                /* 索引越界时不能读取卷名表。 */
		return STA_NODISK;                                                   /* 返回该实现用于未提供卷号的状态值。 */

#ifdef CONFIG_DISKIO_CACHE
	result = diskio_cache_write(disk_volume_strs[pdrv], buff, sector, count);    /* 缓存开启时把扇区写交给缓存层。 */
#else
	result = disk_access_write(disk_volume_strs[pdrv], buff, sector, count);     /* 无缓存分支直接发出写扇区请求。 */
#endif
	return translate_error(result);                                              /* 将刚才的磁盘层结果映射回 FatFs。 */
}





/**
  * @brief  把 FatFs 控制命令翻译为磁盘接口操作。
  * @param  pdrv：[输入]从 0 开始的物理卷号，必须小于 _VOLUMES。
  * @param  cmd：[输入]同步、几何信息或介质检测命令。
  * @param  buff：[输入/输出]命令相关的数据地址，容量由磁盘接口决定。
  * @retval 成功为 RES_OK，命令无效为 RES_PARERR，底层失败为 RES_ERROR；卷号越界返回 STA_NODISK。
  * @note   缓存构建同步时先请求刷缓存，然后检查底层同步结果。
  */
DRESULT disk_ioctl (
	BYTE pdrv,    /* 待访问的物理卷索引。 */
	BYTE cmd,     /* 外层控制码决定传给底层的命令和参数格式。 */
	void *buff    /* 可选输入或输出地址，由具体命令解释。 */
)
{
	int result = RES_OK;                                                                                  /* 默认成功，仅在底层失败或无法识别命令时改写。 */

	if (pdrv >= DISK_MAX_PHY_DRV)                                                                         /* 索引越界时不能读取卷名表。 */
		return STA_NODISK;                                                                            /* 返回该实现用于未提供卷号的状态值。 */

	switch (cmd) {
	case CTRL_SYNC:
#ifdef CONFIG_DISKIO_CACHE
		diskio_cache_flush(disk_volume_strs[pdrv]);                                                   /* 先要求缓存写回，实际返回值在此未单独检查。 */
#endif
		if(disk_access_ioctl(disk_volume_strs[pdrv], DISK_IOCTL_CTRL_SYNC, buff) != 0) {              /* 请求磁盘完成所有挂起写入。 */
			result = RES_ERROR;                                                                   /* 底层磁盘控制失败，对 FatFs 统一返回 I/O 错误。 */
		}
		break;                                                                                        /* 完成本命令分派后返回累积状态。 */

	case GET_SECTOR_SIZE:
		if(disk_access_ioctl(disk_volume_strs[pdrv], DISK_IOCTL_GET_SECTOR_SIZE, buff) != 0) {        /* 将单个逻辑扇区的字节数写入调用者缓冲。 */
			result = RES_ERROR;                                                                   /* 底层磁盘控制失败，对 FatFs 统一返回 I/O 错误。 */
		}
		break;                                                                                        /* 完成本命令分派后返回累积状态。 */

	case GET_SECTOR_COUNT:
		if(disk_access_ioctl(disk_volume_strs[pdrv], DISK_IOCTL_GET_SECTOR_COUNT, buff) != 0) {       /* 将介质的扇区总数写入调用者缓冲。 */
			result = RES_ERROR;                                                                   /* 底层磁盘控制失败，对 FatFs 统一返回 I/O 错误。 */
		}
		break;                                                                                        /* 完成本命令分派后返回累积状态。 */

	case GET_BLOCK_SIZE:
		if (disk_access_ioctl(disk_volume_strs[pdrv], DISK_IOCTL_GET_ERASE_BLOCK_SZ, buff) != 0) {    /* 请求底层报告擦除块大小。 */
			result = RES_ERROR;                                                                   /* 底层磁盘控制失败，对 FatFs 统一返回 I/O 错误。 */
		}
		break;                                                                                        /* 完成本命令分派后返回累积状态。 */

	case DISK_HW_DETECT:
		if (disk_access_ioctl(disk_volume_strs[pdrv], DISK_IOCTL_HW_DETECT, buff) != 0) {             /* 获取磁盘介质检测结果。 */
			result = RES_ERROR;                                                                   /* 底层磁盘控制失败，对 FatFs 统一返回 I/O 错误。 */
		}
		break;                                                                                        /* 完成本命令分派后返回累积状态。 */

	default:
		result = RES_PARERR;                                                                          /* 此适配器没有实现请求的控制命令。 */
		break;                                                                                        /* 完成本命令分派后返回累积状态。 */
	}
	return result;                                                                                        /* 返回本次控制操作的 FatFs 状态。 */

}

