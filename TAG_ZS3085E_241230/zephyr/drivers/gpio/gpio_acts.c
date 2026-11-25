/*
 * Copyright (c) 2018 Justin Watson
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Actions GPIO 驱动。每个普通端口以 32 位位图寻址，GPIO61 对应 GPIOB[29]；GPIOB 的回调列表由共享 GPIO 中断遍历。 */

#include <errno.h>           /* 引入底层返回码约定。 */
#include <kernel.h>          /* 提供 irq_lock 与忙等时序。 */
#include <device.h>          /* 提供设备实例和句柄。 */
#include <init.h>            /* 提供系统初始化阶段。 */
#include <soc.h>             /* 取得当前芯片的 GPIO、WIO 寄存器与中断号。 */
#include <drivers/gpio.h>    /* 引入公共 GPIO 类型、标志和设备操作表。 */
#include <board_cfg.h>       /* 从当前目标板选择端口名称和 IRQ 优先级。 */
#include "gpio_utils.h"      /* 调用通用回调链表登记、筛选及分发实现。 */

#define  GPIO_MAX_GRP	GPIO_MAX_GROUPS    /* 组号用作设备表索引，额外一项容纳最高索引。 */
#define  WIO_DELAY_TIME (200)              /* WIO 写操作后的稳定等待，单位 µs。 */

static struct device *g_gpio_dev[GPIO_MAX_GRP + 1];    /* 由实例初始化登记指针，供共享 ISR 按组定位回调链。 */

struct gpio_acts_config {
	struct gpio_driver_config common;    /* 首成员提供有效引脚掩码，供 GPIO 公共层校验组内引脚编号。 */
	uint32_t base;                       /* 保存片上 GPIO 寄存器区起始地址。 */
	uint8_t grp;                         /* 组号用于将端口局部位索引换算成绝对管脚编号。 */
};

struct gpio_acts_runtime {
	struct gpio_driver_data common;    /* 首成员保存低有效引脚位图，公共层据此转换逻辑值。 */
	sys_slist_t cb;                    /* 当前端口已注册的回调链表。 */
};

/* 从设备对象的只读配置取寄存器基址和端口组号。 */
#define DEV_CFG(dev) \
	((const struct gpio_acts_config * const)(dev)->config)

/* 从设备对象的可变数据取回调链表和软件反相掩码。 */
#define DEV_DATA(dev) \
	((struct gpio_acts_runtime * const)(dev)->data)


/**
  * @brief  读取端口输入寄存器并按物理位序输出采样。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @param  value：[输出]非空，至少一个可写 32 位字。
  * @retval 0 表示已取得端口位图。
  * @note   普通端口一次读出整组 IDAT。WIO 在本地中断锁内依次读五个控制字，五位采样有先后。
  */
static int gpio_acts_port_get_raw(const struct device *dev, uint32_t *value)
{
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);                                  /* 从设备对象取出组号和寄存器基址。 */
	int pin_num = cfg->grp*32;                                                                 /* 用该组的首引脚定位一组 32 位数据寄存器。 */
	unsigned int key;                                                                          /* 保存进入临界区前的中断状态。 */

	/* 在本地中断锁内采样端口，WIO 的各个控制字需要依次读取。 */
	key = irq_lock();                                                                          /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	if(cfg->grp != 3){                                                                         /* 普通 GPIO 和 WIO 使用不同的寄存器组织。 */
		*value = sys_read32(GPIO_REG_IDAT(cfg->base, pin_num));                            /* 按该组代表引脚选取 IDAT，读取全部引脚当前物理电平。 */
	}else{
		*value = 0;                                                                        /* 合并 WIO 单脚状态前从全零位图开始。 */
		for(pin_num = 0; pin_num < 5; pin_num++){                                          /* 遍历五个 WIO 控制字，各引脚独立保存电平。 */
			*value |= ((sys_read32(WIO_REG_CTL(pin_num)) >> 16) & 0x01) << pin_num;    /* 将当前 WIO 控制字第 16 位搬到结果的同号位。 */
		}
	}
#else
	*value = sys_read32(GPIO_REG_IDAT(cfg->base, pin_num));                                    /* 未开启 WIO 时直接读取普通端口输入数据寄存器。 */
#endif
	irq_unlock(key);                                                                           /* 完成寄存器访问，恢复先前的中断状态。 */
	return 0;                                                                                  /* 本次端口操作已完成，按硬件实现返回成功。 */
}

/**
  * @brief  按掩码替换所选引脚的物理输出位。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @param  mask：[输入]端口位图，第 n 位选择端口内引脚 n。
  * @param  value：[输入]物理电平位图，只有 mask 为 1 的位置有效。
  * @retval 本实现写完寄存器后返回 0。
  * @note   普通端口通过端口寄存器访问；WIO 分支逐脚读改写，退出本地中断临界区后等待 200 µs。
  */
static int gpio_acts_port_set_masked_raw(const struct device *dev, uint32_t mask,
					uint32_t value)
{
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);                                                      /* 从设备对象取出组号和寄存器基址。 */
	int pin_num = cfg->grp*32;                                                                                     /* 用该组的首引脚定位一组 32 位数据寄存器。 */
	unsigned int val, key;                                                                                         /* 保存端口读改写中间值与进入前的中断状态。 */

	/* 保护本端口的寄存器更新，普通 GPIO 和 WIO 采用各自的访问路径。 */
	key = irq_lock();                                                                                              /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	if(cfg->grp != 3){                                                                                             /* 普通 GPIO 和 WIO 使用不同的寄存器组织。 */
		val = sys_read32(GPIO_REG_ODAT(cfg->base, pin_num));                                                   /* 读取普通端口原输出锁存，保留掩码之外各脚。 */
		val = (val&~mask) | (mask & value);                                                                    /* 保留未选位，用调用值替换所选位。 */
		sys_write32(val, GPIO_REG_ODAT(cfg->base, pin_num));                                                   /* 同一临界区写回更新后的端口锁存。 */
	}else{
		val = 0;                                                                                               /* 从零开始拼合五路 WIO 状态。 */
		for(pin_num = 0; pin_num < 5; pin_num++){                                                              /* 遍历五个 WIO 控制字，各引脚独立保存电平。 */
			val |= ((sys_read32(WIO_REG_CTL(pin_num)) >> 16) & 0x01) << pin_num;                           /* 取单脚第 16 位并合并到对应端口位。 */
		}

		/* 保留未被 mask 选中的 WIO 状态，得到每个引脚的目标值。 */
		val = (val&~mask) | (mask & value);                                                                    /* 在拼合后的位图上只替换 mask 指定的引脚。 */

		/* 按更新后的位图写回五个 WIO 控制字，分别保留各脚的其他配置。 */
		for(pin_num = 0; pin_num < 5; pin_num++){                                                              /* 遍历五个 WIO 控制字，各引脚独立保存电平。 */
			if(val & (1 << pin_num)){                                                                      /* 按合并后的目标位决定每个 WIO 控制寄存器的第 16 位。 */
				sys_write32(sys_read32(WIO_REG_CTL(pin_num)) | (1 << 16), WIO_REG_CTL(pin_num));       /* 保留该脚其他配置，置位输出电平。 */
			}else{
				sys_write32(sys_read32(WIO_REG_CTL(pin_num)) & (~(1 << 16)), WIO_REG_CTL(pin_num));    /* 保留该脚其他配置，清除输出电平。 */
			}
		}
	}
#else
	val = sys_read32(GPIO_REG_ODAT(cfg->base, pin_num));                                                           /* 读出普通输出寄存器，作为掩码更新基准。 */
	val = (val&~mask) | (mask & value);                                                                            /* 让本次调用仅改变掩码指定的输出。 */
	sys_write32(val, GPIO_REG_ODAT(cfg->base, pin_num));                                                           /* 写回一整组的合成值。 */
#endif
	irq_unlock(key);                                                                                               /* 完成寄存器访问，恢复先前的中断状态。 */

	/* 寄存器访问结束后在锁外执行 WIO 的写后延时。 */
	if(cfg->grp == 3){                                                                                             /* WIO 写后按驱动要求保留稳定时间。 */
        k_busy_wait(WIO_DELAY_TIME);                                                                                   /* 在临界区外忙等 200 µs。 */
	}
	return 0;                                                                                                      /* 本次端口操作已完成，按硬件实现返回成功。 */
}

/**
  * @brief  将掩码内的引脚物理输出置高。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @param  mask：[输入]端口位图，第 n 位选择端口内引脚 n。
  * @retval 本实现写完寄存器后返回 0。
  * @note   普通端口通过端口寄存器访问；WIO 分支逐脚读改写，退出本地中断临界区后等待 200 µs。
  */
static int gpio_acts_port_set_bits_raw(const struct device *dev, uint32_t mask)
{

	const struct gpio_acts_config * const cfg = DEV_CFG(dev);                                                   /* 从设备对象取出组号和寄存器基址。 */
	int pin_num = cfg->grp*32;                                                                                  /* 用该组的首引脚定位一组 32 位数据寄存器。 */
	unsigned int key;                                                                                           /* 保存进入临界区前的中断状态。 */

	/* 保护本端口的寄存器更新，普通 GPIO 和 WIO 采用各自的访问路径。 */
	key = irq_lock();                                                                                           /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	if(cfg->grp != 3){                                                                                          /* 普通 GPIO 和 WIO 使用不同的寄存器组织。 */
		sys_write32(mask, GPIO_REG_BSR(cfg->base, pin_num));                                                /* 将掩码写到置位寄存器，掩码为零的位不受影响。 */
	}else{
		for(pin_num = 0; pin_num < 5; pin_num++){                                                           /* 遍历五个 WIO 控制字，各引脚独立保存电平。 */
			if(mask & (1 << pin_num)){                                                                  /* 只需要逐个更新 mask 选中的 WIO 引脚。 */
				sys_write32(sys_read32(WIO_REG_CTL(pin_num)) | (1 << 16), WIO_REG_CTL(pin_num));    /* 置位单脚控制字的输出电平位。 */
			}
		}
	}
#else
	sys_write32(mask, GPIO_REG_BSR(cfg->base, pin_num));                                                        /* 写普通 GPIO 置位寄存器，把选中的输出拉高。 */
#endif
	irq_unlock(key);                                                                                            /* 完成寄存器访问，恢复先前的中断状态。 */

	/* 寄存器访问结束后在锁外执行 WIO 的写后延时。 */
	if(cfg->grp == 3){                                                                                          /* WIO 写后按驱动要求保留稳定时间。 */
        k_busy_wait(WIO_DELAY_TIME);                                                                                /* 在临界区外忙等 200 µs。 */
	}
	return 0;                                                                                                   /* 本次端口操作已完成，按硬件实现返回成功。 */
}

/**
  * @brief  将掩码内的引脚物理输出置低。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @param  mask：[输入]端口位图，第 n 位选择端口内引脚 n。
  * @retval 本实现写完寄存器后返回 0。
  * @note   普通端口通过端口寄存器访问；WIO 分支逐脚读改写，退出本地中断临界区后等待 200 µs。
  */
static int gpio_acts_port_clear_bits_raw(const struct device *dev, uint32_t mask)
{
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);                                                      /* 从设备对象取出组号和寄存器基址。 */
	int pin_num = cfg->grp*32;                                                                                     /* 用该组的首引脚定位一组 32 位数据寄存器。 */
	unsigned int key;                                                                                              /* 保存进入临界区前的中断状态。 */

	/* 保护本端口的寄存器更新，普通 GPIO 和 WIO 采用各自的访问路径。 */
	key = irq_lock();                                                                                              /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	if(cfg->grp != 3){                                                                                             /* 普通 GPIO 和 WIO 使用不同的寄存器组织。 */
		sys_write32(mask, GPIO_REG_BRR(cfg->base, pin_num));                                                   /* 将掩码写到清零寄存器，其他引脚不受影响。 */
	}else{
		for(pin_num = 0; pin_num < 5; pin_num++){                                                              /* 遍历五个 WIO 控制字，各引脚独立保存电平。 */
			if(mask & (1 << pin_num)){                                                                     /* 跳过未选中的 WIO 引脚。 */
				sys_write32(sys_read32(WIO_REG_CTL(pin_num)) & (~(1 << 16)), WIO_REG_CTL(pin_num));    /* 清除单脚控制字的输出电平位。 */
			}
		}
	}
#else
	sys_write32(mask, GPIO_REG_BRR(cfg->base, pin_num));                                                           /* 写普通 GPIO 清零寄存器，把选中的输出拉低。 */
#endif
	irq_unlock(key);                                                                                               /* 完成寄存器访问，恢复先前的中断状态。 */

	/* 寄存器访问结束后在锁外执行 WIO 的写后延时。 */
	if(cfg->grp == 3){                                                                                             /* WIO 写后按驱动要求保留稳定时间。 */
        k_busy_wait(WIO_DELAY_TIME);                                                                                   /* 在临界区外忙等 200 µs。 */
	}
	return 0;                                                                                                      /* 本次端口操作已完成，按硬件实现返回成功。 */
}

/**
  * @brief  按调用标志配置方向、初始输出、上下拉和施密特输入。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @param  pin：[输入]端口内引脚索引，普通端口为 0～31，WIO 须为实现支持的引脚。
  * @param  flags：[输入]公共接口转换后的方向、电平及电气属性位。
  * @retval 本实现写完寄存器后返回 0。
  * @note   同时请求输入与输出时先走输出分支；预置输出锁存值后才使能输出。READY 在 GPIOB 的 29 号位请求输入和下拉。
  */
static int gpio_acts_config(const struct device *dev, gpio_pin_t pin,
			   gpio_flags_t flags)
{
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);        /* 从设备对象取出组号和寄存器基址。 */
	int pin_num = cfg->grp*32 + pin;                                 /* 以组号乘 32 加组内编号得到芯片引脚号。 */
	unsigned int val, key, set_val, set_mask;                        /* 保留旧硬件值、中断状态、新字段及待替换位。 */
	int reg_addr;                                                    /* 保存普通 GPIO 或 WIO 的引脚控制寄存器地址。 */

	set_val = 0;                                                     /* 从未使能输入和输出的状态合成配置。 */
	set_mask = 0xfff;                                                /* 低十二位由本次配置覆盖，保留控制字的其他位。 */

	/* 先确定方向和输出初值，再组合输入端的上下拉与滤波设置。 */
	if (flags & GPIO_OUTPUT){                                        /* 输出分支优先，可在打开驱动前先预装锁存值。 */
		if (flags & GPIO_OUTPUT_INIT_HIGH) {                     /* 仅请求高初值时执行端口置位。 */
			/* 在打开输出缓冲前先设置输出锁存，避免切换瞬间产生相反的电平。 */
			gpio_acts_port_set_bits_raw(dev, BIT(pin));      /* 打开输出缓冲前先将该脚锁存置高。 */
		}
		if (flags & GPIO_OUTPUT_INIT_LOW) {                      /* 仅请求低初值时执行端口清零。 */
			/* 需要低初值时先清除输出锁存。 */
			gpio_acts_port_clear_bits_raw(dev, BIT(pin));    /* 打开输出缓冲前先将该脚锁存置低。 */
		}
		set_val = GPIO_CTL_GPIO_OUTEN;                           /* 生成输出使能字段。 */

	}else if (flags & GPIO_INPUT){                                   /* 未要求输出时才检查输入方向。 */
		set_val = GPIO_CTL_GPIO_INEN;                            /* 为输入模式打开数字输入通道。 */

	}

	/* 将上下拉请求和施密特输入位合入方向设置，稍后一次写入引脚控制寄存器。 */
	if (flags & GPIO_PULL_UP)                                        /* 按请求引入内部上拉。 */
		set_val |= GPIO_CTL_PULLUP;                              /* 把上拉使能加到目标控制字。 */
	if (flags & GPIO_PULL_DOWN)                                      /* 按请求引入内部下拉。 */
		set_val |= GPIO_CTL_PULLDOWN;                            /* 把下拉使能加到目标控制字。 */
	if (flags & GPIO_INT_DEBOUNCE)                                   /* 以施密特输入响应此兼容的消抖标志。 */
		set_val |= GPIO_CTL_SMIT;                                /* 在引脚控制字打开施密特输入字段。 */

	/* 短暂屏蔽中断，保留引脚控制字中无需改变的字段。 */
	key = irq_lock();                                                /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	if(cfg->grp != 3){                                               /* 普通 GPIO 和 WIO 使用不同的寄存器组织。 */
		reg_addr = GPIO_REG_CTL(cfg->base, pin_num);             /* 普通 GPIO 使用芯片全局引脚号选取寄存器。 */
	}else{
		reg_addr = WIO_REG_CTL(pin);                             /* WIO 使用组内 pin 选取独立寄存器。 */
	}
#else
	reg_addr = GPIO_REG_CTL(cfg->base, pin_num);                     /* 无 WIO 构建走普通 GPIO 寄存器映射。 */
#endif

	val = sys_read32(reg_addr);                                      /* 保存控制字中不归本次方向、电气配置管理的字段。 */
	val &= ~set_mask;                                                /* 清空即将替换的低十二位。 */
	val |= set_val;                                                  /* 把已组合方向和电气属性并入保留值。 */
	sys_write32(val, reg_addr);                                      /* 将新的引脚属性提交给寄存器。 */
	irq_unlock(key);                                                 /* 完成寄存器访问，恢复先前的中断状态。 */

	/* 寄存器访问结束后在锁外执行 WIO 的写后延时。 */
	if(cfg->grp == 3){                                               /* WIO 写后按驱动要求保留稳定时间。 */
        k_busy_wait(WIO_DELAY_TIME);                                     /* 在临界区外忙等 200 µs。 */
	}
	return 0;;                                                       /* 本次端口操作已完成，按硬件实现返回成功。 */
}


/**
  * @brief  翻转指定引脚的物理输出锁存值。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @param  mask：[输入]端口位图，第 n 位选择端口内引脚 n。
  * @retval 本实现写完寄存器后返回 0。
  * @note   普通端口通过端口寄存器访问；WIO 分支逐脚读改写，退出本地中断临界区后等待 200 µs。
  */
static int gpio_acts_port_toggle_bits(const struct device *dev, uint32_t mask)
{
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);                                                   /* 从设备对象取出组号和寄存器基址。 */
	int pin_num = cfg->grp*32;                                                                                  /* 用该组的首引脚定位一组 32 位数据寄存器。 */
	unsigned int val, key;                                                                                      /* 保存端口读改写中间值与进入前的中断状态。 */

	/* 保护本端口的寄存器更新，普通 GPIO 和 WIO 采用各自的访问路径。 */
	key = irq_lock();                                                                                           /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	if(cfg->grp != 3){                                                                                          /* 普通 GPIO 和 WIO 使用不同的寄存器组织。 */
		val = sys_read32(GPIO_REG_ODAT(cfg->base, pin_num));                                                /* 取得当前锁存以翻转选定输出。 */
		val ^= mask;                                                                                        /* 与掩码异或，使所选位取反。 */
		sys_write32(val, GPIO_REG_ODAT(cfg->base, pin_num));                                                /* 写回新锁存，维持未选位原值。 */
	}else{
		for(pin_num = 0; pin_num < 5; pin_num++){                                                           /* 遍历五个 WIO 控制字，各引脚独立保存电平。 */
			if(mask & (1 << pin_num)){                                                                  /* 只翻转掩码指定的 WIO 引脚。 */
				sys_write32(sys_read32(WIO_REG_CTL(pin_num)) ^ (1 << 16), WIO_REG_CTL(pin_num));    /* 在该 WIO 控制字中只翻转输出电平位。 */
			}
		}
	}
#else
	val = sys_read32(GPIO_REG_ODAT(cfg->base, pin_num));                                                        /* 在无 WIO 路径读取普通端口原锁存。 */
	val ^= mask;                                                                                                /* 对选中的普通 GPIO 输出取反。 */
	sys_write32(val, GPIO_REG_ODAT(cfg->base, pin_num));                                                        /* 将翻转后的端口值写回硬件。 */
#endif
	irq_unlock(key);                                                                                            /* 完成寄存器访问，恢复先前的中断状态。 */

	/* 寄存器访问结束后在锁外执行 WIO 的写后延时。 */
	if(cfg->grp == 3){                                                                                          /* WIO 写后按驱动要求保留稳定时间。 */
        k_busy_wait(WIO_DELAY_TIME);                                                                                /* 在临界区外忙等 200 µs。 */
	}
	return 0;                                                                                                   /* 本次端口操作已完成，按硬件实现返回成功。 */
}


/**
  * @brief  将端口内触发类型编码为单脚 GPIO 中断控制字。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @param  pin：[输入]端口内引脚索引，普通端口为 0～31，WIO 须为实现支持的引脚。
  * @param  mode：[输入]禁用、电平或边沿检测。
  * @param  trig：[输入]物理低、高或双边沿触发。
  * @retval 本实现写完寄存器后返回 0。
  * @note   改写中断字段时清除 MFP 选择，使引脚切回 GPIO。READY 选择双边沿，在后续公共 IRQ 中分发。
  */
static int gpio_acts_pin_interrupt_configure(const struct device *dev,
		gpio_pin_t pin, enum gpio_int_mode mode,
		enum gpio_int_trig trig)
{
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);                                            /* 从设备对象取出组号和寄存器基址。 */
	int pin_num, reg_addr;                                                                               /* 分别保存对应控制器使用的引脚索引和寄存器地址。 */
	unsigned int val, key, set_val, set_mask;                                                            /* 保留旧硬件值、中断状态、新字段及待替换位。 */

#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	if(cfg->grp != 3){                                                                                   /* 普通 GPIO 和 WIO 使用不同的寄存器组织。 */
		pin_num = cfg->grp*32 + pin;                                                                 /* 以端口基准换算出普通 GPIO 的全局编号。 */
		reg_addr = GPIO_REG_CTL(cfg->base, pin_num);                                                 /* 选中普通 GPIO 的引脚控制字。 */
	}else{
		pin_num = pin;                                                                               /* WIO 编号直接使用端口内序号。 */
		reg_addr = WIO_REG_CTL(pin_num);                                                             /* 选中该 WIO 的独立控制字。 */
	}
#else
	pin_num = cfg->grp*32 + pin;                                                                         /* 无 WIO 构建直接转换普通引脚编号。 */
	reg_addr = GPIO_REG_CTL(cfg->base, pin_num);                                                         /* 根据全局引脚编号定位中断控制字段。 */
#endif

	/* 中断检测使用 GPIO 功能，更新触发字段时也要将原复用选择清零。 */
	set_mask = GPIO_CTL_INTC_MASK | GPIO_CTL_INTC_EN | GPIO_CTL_INC_TRIGGER_MASK | GPIO_CTL_MFP_MASK;    /* 屏蔽并替换触发、使能和复用字段，其他位保留。 */
	set_val = GPIO_CTL_INTC_MASK | GPIO_CTL_INTC_EN;                                                     /* 初始值选择 GPIO 中断路径并开启检测。 */

	/* 为启用中断的 WIO 另加输入和上拉。 */
	if(cfg->grp == 3){                                                                                   /* WIO 另有输入使能要求。 */
		if(mode != GPIO_INT_MODE_DISABLED){                                                          /* 仅在启用检测时叠加 WIO 输入属性。 */
			/* WIO 开启中断时驱动同时打开输入和上拉。 */
			set_val |= 	GPIO_CTL_GPIO_INEN | GPIO_CTL_PULLUP;                                /* 打开 WIO 数字输入和上拉，供中断检测。 */
		}
	}

	/* 根据中断模式选择实际需要写入的触发编码。 */
	if(mode == GPIO_INT_MODE_DISABLED){                                                                  /* 禁用时覆盖之前合成的中断设置。 */
		set_val = 0;                                                                                 /* 清除目标中断字段。 */
	}else if(mode == GPIO_INT_MODE_LEVEL){                                                               /* 电平模式在输入持续满足条件时触发。 */
		if(trig == GPIO_INT_TRIG_LOW)                                                                /* 判断物理触发条件是否为低。 */
			set_val |= GPIO_CTL_INC_TRIGGER_LOW_LEVEL;                                           /* 选择低电平检测编码。 */
		else
			set_val |= GPIO_CTL_INC_TRIGGER_HIGH_LEVEL;                                          /* 其他电平条件按高电平编码。 */
	}else{
		if(trig == GPIO_INT_TRIG_LOW)                                                                /* 边沿模式按目标电平区分下降或上升。 */
			set_val |= GPIO_CTL_INC_TRIGGER_FALLING_EDGE;                                        /* 选择下降沿检测。 */
		else  if(trig == GPIO_INT_TRIG_HIGH)                                                         /* 检查是否只需要上升沿。 */
			set_val |= GPIO_CTL_INC_TRIGGER_RISING_EDGE;                                         /* 选择上升沿检测。 */
		else
			set_val |= GPIO_CTL_INC_TRIGGER_DUAL_EDGE;                                           /* 其余边沿组合对应双边沿，READY 使用此配置。 */
	}

	/* 屏蔽本地中断完成控制寄存器读改写。 */
	key = irq_lock();                                                                                    /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
	val = sys_read32(reg_addr);                                                                          /* 先读回引脚原控制值。 */
	val &= ~set_mask;                                                                                    /* 清除本次涉及的旧中断和复用字段。 */
	val |= set_val;                                                                                      /* 合入已选中断模式及必要的输入位。 */
	sys_write32(val, reg_addr);                                                                          /* 把新配置提交到管脚控制寄存器。 */

	irq_unlock(key);                                                                                     /* 完成寄存器访问，恢复先前的中断状态。 */

	/* 寄存器访问结束后在锁外执行 WIO 的写后延时。 */
	if(cfg->grp == 3){                                                                                   /* WIO 写后按驱动要求保留稳定时间。 */
        k_busy_wait(WIO_DELAY_TIME);                                                                         /* 在临界区外忙等 200 µs。 */
	}
	return 0;                                                                                            /* 本次端口操作已完成，按硬件实现返回成功。 */
}

/**
  * @brief  扫描各组待决位，再将匹配引脚分派给已登记的回调。
  * @param  arg：[输入]IRQ_CONNECT 固定传入 port_0_acts 的设备地址，非空。
  * @retval 无。
  * @note   中断上下文。按触发组选择回调表，但回调的设备参数仍为注册 IRQ 时传入的端口 0；处理函数需以 pins 和所属回调对象识别事件。
  */
static void gpio_acts_isr(void *arg)
{
	struct device *dev = (struct device *)arg;                                         /* 取共享 IRQ 注册时保存的设备对象。 */
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);                          /* 从设备对象取出组号和寄存器基址。 */
	struct gpio_acts_runtime *context;                                                 /* 稍后指向实际发生中断端口的运行数据。 */
	uint32_t int_stat, i;                                                              /* 当前组的待决位快照及遍历索引。 */

	/* 一个中断号供所有普通端口共用，逐组读取本次锁存的待决位。 */
	for(i = 0; i < GPIO_MAX_IRQ_GRP; i++ ){
		int_stat = sys_read32(GPIO_REG_IRQ_PD(cfg->base, i*32));                   /* 读取本组锁存的待决位，一位对应组内一脚。 */
		if(int_stat){                                                              /* 该组有事件才遍历回调。 */
			if(g_gpio_dev[i] != NULL){                                         /* 仅已注册端口才存在可使用的运行态链表。 */
				context = g_gpio_dev[i]->data;                             /* 取触发组自己的订阅列表。 */
				gpio_fire_callbacks(&context->cb, dev, int_stat);          /* 按引脚交集调用处理程序；传入设备仍为共享 IRQ 对应的 port_0。 */
			}else{
				printk("gpio-irq:err,grp=%d,stat=0x%x\n", i, int_stat);    /* 记录尚无端口实例的异常待决位。 */
			}

			/* 各订阅者处理完这批事件后，再向本组确认刚才读出的待决位。 */
			sys_write32(int_stat, GPIO_REG_IRQ_PD(cfg->base, i*32));           /* 对本轮快照写一清除，只确认刚处理过的引脚。 */
		}
	}

#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)

	/* WIO 逐脚提供待决位，转换为与普通端口相同的位图交付回调。 */
	for(i = 0; i < GPIO_WIO_MAX_PIN_NUM; i++ ){
		int_stat = sys_read32(WIO_REG_CTL(i)) & WIO_CTL_INT_PD_MASK;               /* WIO 的待决状态独立存在每脚控制字内。 */
		if(int_stat){                                                              /* 只在该引脚有事件时调用 WIO 回调。 */
			if(g_gpio_dev[GPIO_MAX_IRQ_GRP] != NULL){                          /* 先确认 WIO 端口实例已经登记。 */
				context = g_gpio_dev[GPIO_MAX_IRQ_GRP]->data;              /* 取得 WIO 组的运行数据。 */
				gpio_fire_callbacks(&context->cb, dev, (1 << i));          /* 将 WIO 序号换成单比特掩码交给通用分发器。 */
			}else{
				printk("WIO-irq:err,grp=%d,stat=0x%x\n", i, int_stat);     /* 记录有事件但没有 WIO 实例的情况。 */
			}
			sys_write32(sys_read32(WIO_REG_CTL(i)), WIO_REG_CTL(i));           /* 按驱动的确认序列读回并写回 WIO 控制字。 */
		}
	}
#endif
}

/**
  * @brief  更新给定端口自己的中断回调链表。
  * @param  port：[输入]非空的目标端口。
  * @param  callback：[输入/输出]非空回调对象，登记期间保持有效。
  * @param  set：[输入]真为登记，假为撤销。
  * @retval 返回 gpio_manage_callback 的结果：0 成功，非空表内找不到待删除项时为 -EINVAL。
  * @note   只更新订阅关系，配置引脚触发方式另由中断配置入口完成。
  */
static int gpio_acts_manage_callback(const struct device *port,
				    struct gpio_callback *callback,
				    bool set)
{
	struct gpio_acts_runtime *context = port->data;              /* 选择调用方所属端口的回调链。 */

	return gpio_manage_callback(&context->cb, callback, set);    /* 把插入或移除请求委派给通用链表实现。 */
}

#if 0    /* 历史的普通引脚中断开关接口，未参与当前构建。 */
/**
  * @brief  通过两个中断控制位启停普通 GPIO 引脚的中断。
  * @param  port：[输入]非空的普通端口。
  * @param  pin：[输入]普通端口的组内引脚号，范围为 0～31。
  * @param  eable：[输入]非零开启，零关闭。
  * @retval 本实现写完寄存器后返回 0。
  * @note   此函数位于 #if 0，保留为历史实现。
  */
static int gpio_acts_irq_set(const struct device *port,
					 gpio_pin_t pin, int eable)
{
	const struct gpio_acts_config * const cfg = DEV_CFG(port);    /* 从设备对象取出组号和寄存器基址。 */
	int pin_num = cfg->grp*32 + pin;                              /* 以组号乘 32 加组内编号得到芯片引脚号。 */
	unsigned int val, key;                                        /* 保存端口读改写中间值与进入前的中断状态。 */
	key = irq_lock();                                             /* 屏蔽本地中断，避免对同一共享寄存器的更新交错。 */
	val = sys_read32(GPIO_REG_CTL(cfg->base, pin_num));           /* 保存单脚寄存器中的其他电气属性。 */
	if(eable)                                                     /* 非零参数请求开启中断。 */
		val |= (GPIO_CTL_INTC_MASK | GPIO_CTL_INTC_EN);       /* 同时置位中断控制和使能位。 */
	else
		val &= ~(GPIO_CTL_INTC_MASK | GPIO_CTL_INTC_EN);      /* 清零对应位关闭此脚中断。 */
	sys_write32(val, GPIO_REG_CTL(cfg->base, pin_num));           /* 仅提交上述中断位的改变。 */
	irq_unlock(key);                                              /* 完成寄存器访问，恢复先前的中断状态。 */
	return 0;                                                     /* 本次端口操作已完成，按硬件实现返回成功。 */
}
#endif

/* 设备注册时绑定这张函数表，公共 GPIO API 经 port->api 到达以下实现。 */
static const struct gpio_driver_api gpio_acts_api = {
	.pin_configure = gpio_acts_config,                               /* 配置引脚输入输出初值及电气属性。 */
	.port_get_raw = gpio_acts_port_get_raw,                          /* 读取普通输入寄存器或拼合 WIO 位图。 */
	.port_set_masked_raw = gpio_acts_port_set_masked_raw,            /* 以掩码更新物理输出。 */
	.port_set_bits_raw = gpio_acts_port_set_bits_raw,                /* 只将选中输出置高。 */
	.port_clear_bits_raw = gpio_acts_port_clear_bits_raw,            /* 只将选中输出置低。 */
	.port_toggle_bits = gpio_acts_port_toggle_bits,                  /* 对选中的锁存位取反。 */
	.pin_interrupt_configure = gpio_acts_pin_interrupt_configure,    /* 将触发模式写入单脚中断控制字。 */
	.manage_callback = gpio_acts_manage_callback,                    /* 更新该端口自己的回调链表。 */
};

/**
  * @brief  连接 SoC 的 GPIO 共享中断并打开 IRQ。
  * @param  dev：[输入]触发首次初始化的端口参数，本实现未使用。
  * @retval 无。
  * @note   注册给 ISR 的参数始终为 port_0_acts，在公共向量中再按组分发。
  */
static void port_acts_config_func(const struct device *dev);

/**
  * @brief  保存端口实例以供 ISR 查表，并在首次注册时连接共享中断。
  * @param  dev：[输入]非空，已注册的 Actions GPIO 端口设备。
  * @retval 固定返回 0。越界组会提前返回，未登记该设备。
  * @note   由 PRE_KERNEL_1 的设备初始化调用，全部端口共用一次 IRQ 配置。
  */
int gpio_acts_init(const struct device *dev)
{
	static uint8_t b_init=0;                                        /* 跨实例保存共享中断是否已经安装。 */
	const struct gpio_acts_config * const cfg = DEV_CFG(dev);       /* 从设备对象取出组号和寄存器基址。 */

	/* 先登记端口对象，使共享 ISR 能取到该组自己的回调链。 */
	if(cfg->grp <= GPIO_MAX_GRP) {                                  /* 拒绝把组号用作设备表之外的索引。 */
		if(g_gpio_dev[cfg->grp] != NULL){                       /* 此组已有设备时保留原实例。 */
			//printk("gpio:grp=%d is register\n", cfg->grp);
		}else{
			g_gpio_dev[cfg->grp] = (struct device *)dev;    /* 把设备对象保存在组表，供共享 ISR 取其运行数据。 */
			//printk("gpio:grp=%d ok\n", cfg->grp);
		}
	}else{
		//printk("gpio:grp=%d err\n", cfg->grp);
		return 0;                                               /* 越界组直接返回 0，未登记该实例。 */
	}

	/* 首个有效实例负责启用共享 IRQ，后续实例沿用同一向量。 */
	if(b_init == 0){                                                /* 只有首个有效实例需要安装 IRQ。 */
		port_acts_config_func(dev);                             /* 连接并使能 SoC 的共享 GPIO 向量。 */
		b_init = 1;                                             /* 记住已安装状态，其他端口不重复配置。 */
		printk("gpio irq init\n");                              /* 启动日志指明共享 IRQ 初始化已经完成。 */
	}
	return 0;                                                       /* 按设备初始化接口报告成功。 */
}

/* n 是组号，devname 是板级设备名。各组共享寄存器基址，普通端口靠 n 定位一组 32 位数据寄存器。
 * 公共有效掩码固定为 0xffffffff；该掩码本身不反映部分组实际引出的引脚数。
 * 每组另有一份静态运行数据，起始为空回调链和全零极性位图。
 * DEVICE_DEFINE 以 PRE_KERNEL_1 和 CONFIG_KERNEL_INIT_PRIORITY_OBJECTS 注册实例，绑定初始化函数及 gpio_acts_api。
 */
#define GPIO_ACTS_INIT(n, devname)						\
 	static const struct gpio_acts_config port_##n##_acts_config = {	\
		.common = {						\
			.port_pin_mask = 0xffffffff,\
		},							\
		.base = GPIO_REG_BASE,			\
		.grp = n,		\
	};								\
									\
	static struct gpio_acts_runtime port_##n##_acts_runtime;		\
									\
	DEVICE_DEFINE(port_##n##_acts, devname,		\
			    gpio_acts_init, NULL, &port_##n##_acts_runtime,	\
			    &port_##n##_acts_config, PRE_KERNEL_1,	\
			    CONFIG_KERNEL_INIT_PRIORITY_OBJECTS,		\
			    &gpio_acts_api);


/* 下方保留按设备树批量实例化的旧写法。当前实现由板级组选项逐个注册。 */
/*DT_INST_FOREACH_STATUS_OKAY(GPIO_ACTS_INIT)*/

#if defined(CONFIG_GPIO_A) && (CONFIG_GPIO_A == 1)
	GPIO_ACTS_INIT(0, CONFIG_GPIO_A_NAME)    /* 根据板级名称注册 A 组，普通引脚从 0 开始。 */
#endif

#if defined(CONFIG_GPIO_B) && (CONFIG_GPIO_B == 1)
	GPIO_ACTS_INIT(1, CONFIG_GPIO_B_NAME)    /* 注册 B 组，SPI 握手线 READY 所用的 GPIO61 位于此组。 */
#endif

#if defined(CONFIG_GPIO_C) && (CONFIG_GPIO_C == 1)
	GPIO_ACTS_INIT(2, CONFIG_GPIO_C_NAME)    /* 注册 C 组，普通引脚从 64 开始。 */
#endif

#if defined(CONFIG_WIO) && (CONFIG_WIO == 1)
	GPIO_ACTS_INIT(3, CONFIG_WIO_NAME)    /* 启用 WIO 时另外注册第 3 组。 */
#endif

/**
  * @brief  连接 SoC 的 GPIO 共享中断并打开 IRQ。
  * @param  dev：[输入]触发首次初始化的端口参数，本实现未使用。
  * @retval 无。
  * @note   注册给 ISR 的参数始终为 port_0_acts，在公共向量中再按组分发。
  */
static void port_acts_config_func(const struct device *dev)
{
	IRQ_CONNECT(IRQ_ID_GPIO, CONFIG_GPIO_IRQ_PRI,
			gpio_acts_isr,
			DEVICE_GET(port_0_acts), 0);    /* 共用一个 ISR，传入固定端口 0 的设备对象，优先级来自板级配置。 */

	/* 向量、参数和优先级已就绪，最后开放中断线。 */
	irq_enable(IRQ_ID_GPIO);                        /* 连接完成后向中断控制器开放该 IRQ。 */
}

