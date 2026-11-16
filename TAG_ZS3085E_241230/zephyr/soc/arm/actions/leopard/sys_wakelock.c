/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file 系统唤醒锁实现：按类型和使用者累计引用，以阻止待机状态切换。
 */
 #include <kernel.h>
 #include <device.h>
#include <string.h>
#include <sys_wakelock.h>
#include <assert.h>
#include <debug/tracedump.h>

struct wakelock_user_info_t {
	uint16_t ref_cnt;    /* 该对象累计引用次数；在类型结构中为总数，在使用者结构中为该使用者次数。 */
};

struct wakelock_t {
	uint8_t wake_lock_type;                                       /* 此表项对应的唤醒锁类型。 */
	uint16_t ref_cnt;                                             /* 该对象累计引用次数；在类型结构中为总数，在使用者结构中为该使用者次数。 */
	struct wakelock_user_info_t user_info[MAX_WAKE_LOCK_USER];    /* 按调用者类别保存独立计数，帮助定位未释放引用。 */
	uint32_t free_timestamp;                                      /* 最近释放时间基准，单位毫秒；加锁时清零。 */
};

struct wakelocks_manager_context_t {
	uint32_t wakelock_enable;                           /* 是否启用查询机制，禁用不抹除实际计数。 */
	struct wakelock_t wakelocks[MAX_WAKE_LOCK_TYPE];    /* 每个唤醒锁类型对应的状态表。 */
};

struct wakelocks_manager_context_t wakelocks_manager;    /* 本文件定义的全局管理器，初始化时清零。 */

static struct k_sem wakelocks_sem;    /* 所有类型共用的待机通知信号量。 */

/**
  * @brief  按类型在线性表中查找唤醒锁对象。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 匹配对象地址；不存在返回 NULL。
  * @note   调用者负责在需要一致性时持有中断锁。
  */
static struct wakelock_t *_sys_wakelock_lookup(int wake_lock_type)
{
	for (int i = 0; i < MAX_WAKE_LOCK_TYPE; i++) {                                    /* 遍历已支持的类型表项。 */
		if (wakelocks_manager.wakelocks[i].wake_lock_type == wake_lock_type) {    /* 编号匹配时返回该类型状态。 */
			return &wakelocks_manager.wakelocks[i];                           /* 返回管理器中的对象地址，不分配新对象。 */
		}
	}
	return NULL;                                                                      /* 未找到指定类型。 */
}

/**
  * @brief  以 APP_WAKE_LOCK_USER 身份增加指定唤醒锁引用。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 0：成功；-EEXIST：类型未找到。
  * @note   与 sys_wake_unlock 配对；持锁阻止相应待机策略，不是对 SPI 总线的互斥锁。
  */
int sys_wake_lock(int wake_lock_type)
{
	return sys_wake_lock_ext(wake_lock_type,APP_WAKE_LOCK_USER);    /* 默认包装把引用计入应用使用者槽。 */
}

/**
  * @brief  在中断临界区内增加指定类型和使用者的唤醒锁计数。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @param  wake_lock_user：[输入]0～MAX_WAKE_LOCK_USER-1 的使用者索引，本实现不检查越界。
  * @retval 0：成功；-EEXIST：类型未找到。
  * @note   总引用计数须小于 0xFFFF，否则断言；持锁后清零释放时间戳。
  */
int sys_wake_lock_ext(int wake_lock_type, int wake_lock_user)
{
	int res = 0;                                                                              /* 默认处理状态为成功，参数错误时改为负错误码。 */
	struct wakelock_t *wakelock = NULL;                                                       /* 查找前清空对象指针，错误出口可据此避免解引用。 */

	uint32_t key = irq_lock();                                                                /* 保护共享引用计数及时间戳，保存进入前中断状态。 */

	wakelock = _sys_wakelock_lookup(wake_lock_type);                                          /* 查找本次要操作的类型对象。 */
	if (!wakelock) {                                                                          /* 未找到类型时报告错误并走统一恢复中断出口。 */
		printk("err, holder: %d", wake_lock_type);                                        /* 打印无法识别的类型编号。 */
		res = -EEXIST;                                                                    /* 加锁路径对无效类型使用的原有错误码。 */
		goto exit;                                                                        /* 跳转到统一出口，确保恢复中断状态。 */
	}

	assert(wakelock->ref_cnt < 0xFFFF);                                                       /* 防止总引用计数 16 位溢出。 */

	wakelock->ref_cnt++;                                                                      /* 增加该类型总引用数。 */
	wakelock->user_info[wake_lock_user].ref_cnt++;                                            /* 增加调用者所属槽的引用。 */
	wakelock->free_timestamp = 0;                                                             /* 持锁期间不累计空闲释放时间。 */

	tracedump_save(TRACE_WAKELOCK, (uint32_t)wake_lock_type, (uint32_t)wakelock->ref_cnt);    /* 记录本次引用变化供跟踪诊断。 */

exit:
	irq_unlock(key);                                                                          /* 恢复进入临界区前的中断状态。 */
	return res;                                                                               /* 返回本轮计数操作状态。 */
}

/**
  * @brief  以 APP_WAKE_LOCK_USER 身份减少指定唤醒锁引用。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 0：成功；-ESRCH：类型未找到。
  * @note   须与本使用者的加锁匹配；不是强制把所有使用者引用清零。
  */
int sys_wake_unlock(int wake_lock_type)
{
	return sys_wake_unlock_ext(wake_lock_type, APP_WAKE_LOCK_USER);    /* 默认包装从应用使用者槽扣除引用。 */
}
extern void system_clear_fast_standby(void);    /* 待机模块提供的外部入口，仅配置启用时调用。 */

/**
  * @brief  减少使用者及总引用计数，更新时间戳并在全部释放后唤醒等待者。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @param  wake_lock_user：[输入]0～MAX_WAKE_LOCK_USER-1 的使用者索引，本实现不检查越界。
  * @retval 0：成功；-ESRCH：类型未找到。
  * @note   总引用必须大于零；本实现未单独验证使用者计数。FULL 类型还按配置清除快速待机状态，内部用中断锁保护计数。
  */
int sys_wake_unlock_ext(int wake_lock_type, int wake_lock_user)
{
	int res = 0;                                                                                  /* 默认处理状态为成功，参数错误时改为负错误码。 */
	struct wakelock_t *wakelock = NULL;                                                           /* 查找前清空对象指针，错误出口可据此避免解引用。 */

	uint32_t key = irq_lock();                                                                    /* 保护共享引用计数及时间戳，保存进入前中断状态。 */

	wakelock = _sys_wakelock_lookup(wake_lock_type);                                              /* 查找本次要操作的类型对象。 */
	if (!wakelock) {                                                                              /* 未找到类型时报告错误并走统一恢复中断出口。 */
		printk("err, holder: %d", wake_lock_type);                                            /* 打印无法识别的类型编号。 */
		res = -ESRCH;                                                                         /* 解锁路径报告目标类型不存在。 */
		goto exit;                                                                            /* 跳转到统一出口，确保恢复中断状态。 */
	}

	assert(wakelock->ref_cnt > 0);                                                                /* 拒绝在总引用已为零时继续解锁。 */

	wakelock->user_info[wake_lock_user].ref_cnt--;                                                /* 扣除指定调用者槽的引用，调用者必须保证配对。 */

	if (wakelock->ref_cnt--) {                                                                    /* 用递减前的总数决定更新时间戳；断言正常时进入，随后总数减一。 */
		/* FULL 锁释放时重置所有类型的释放时间基准，其他类型仅更新自身。 */
		if (wake_lock_type == FULL_WAKE_LOCK) {                                               /* FULL 锁需联动全部类型时间基准或快速待机状态。 */
			for (int i = 0; i < MAX_WAKE_LOCK_TYPE; i++) {                                /* 遍历已支持的类型表项。 */
				wakelocks_manager.wakelocks[i].free_timestamp = k_uptime_get_32();    /* 把该类型的释放时间基准设为当前毫秒时间。 */
			}
		} else {
			wakelock->free_timestamp = k_uptime_get_32();                                 /* 仅重置当前非 FULL 类型的释放时间基准。 */
		}
	}

	if (wake_lock_type == FULL_WAKE_LOCK) {                                                       /* FULL 锁需联动全部类型时间基准或快速待机状态。 */
#ifdef CONFIG_SYS_STANDBY
		system_clear_fast_standby();                                                          /* 启用系统待机管理时清除快速待机标志。 */
#endif
	}

	tracedump_save(TRACE_WAKELOCK, (uint32_t)wake_lock_type, (uint32_t)wakelock->ref_cnt);        /* 记录本次引用变化供跟踪诊断。 */

exit:

	if (wakelock && wakelock->ref_cnt == 0) {                                                     /* 全部引用已释放时给等待者一次通知。 */
		sys_wakelocks_wake(wake_lock_type);                                                   /* 唤醒共享信号量上的等待者。 */
	}	
	irq_unlock(key);                                                                              /* 恢复进入临界区前的中断状态。 */
	return res;                                                                                   /* 返回本轮计数操作状态。 */
}

/**
  * @brief  打印全部已支持类型及各使用者引用，供诊断未释放的唤醒锁。
  * @param  无。
  * @retval 无。
  * @note   诊断打印没有整体快照锁，不能保证多字段同时刻一致。
  */
void sys_wakelocks_dump(void)
{
	struct wakelock_t *wakelock = NULL;                                                                        /* 查找前清空对象指针，错误出口可据此避免解引用。 */

	printk("wlock info: \n");                                                                                  /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
	wakelock = _sys_wakelock_lookup(PARTIAL_WAKE_LOCK);                                                        /* 查找要打印的类型对象，后续先检查是否存在。 */
	if (wakelock) {                                                                                            /* 只有查找成功才读取并打印该对象计数。 */
		printk("type : PARTIAL_WAKE_LOCK	(%d)\n",wakelock->ref_cnt);                                /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	BT_WAKE_LOCK_USER    :%d\n",wakelock->user_info[BT_WAKE_LOCK_USER].ref_cnt);       /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	MEDIA_WAKE_LOCK_USER :%d\n",wakelock->user_info[MEDIA_WAKE_LOCK_USER].ref_cnt);    /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	USB_WAKE_LOCK_USER   :%d\n",wakelock->user_info[USB_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	POWER_WAKE_LOCK_USER :%d\n",wakelock->user_info[POWER_WAKE_LOCK_USER].ref_cnt);    /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	SYS_WAKE_LOCK_USER   :%d\n",wakelock->user_info[SYS_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	APP_WAKE_LOCK_USER   :%d\n",wakelock->user_info[APP_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("DISPLAY_WAKE_LOCK_USER   :%d\n",wakelock->user_info[DISPLAY_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
	}
	wakelock = _sys_wakelock_lookup(FULL_WAKE_LOCK);                                                           /* 查找要打印的类型对象，后续先检查是否存在。 */
	if (wakelock) {                                                                                            /* 只有查找成功才读取并打印该对象计数。 */
		printk("type : FULL_WAKE_LOCK total	(%d)\n",wakelock->ref_cnt);                                /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	BT_WAKE_LOCK_USER   : %d\n",wakelock->user_info[BT_WAKE_LOCK_USER].ref_cnt);       /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	MEDIA_WAKE_LOCK_USER: %d\n",wakelock->user_info[MEDIA_WAKE_LOCK_USER].ref_cnt);    /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	USB_WAKE_LOCK_USER  : %d\n",wakelock->user_info[USB_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	POWER_WAKE_LOCK_USER: %d\n",wakelock->user_info[POWER_WAKE_LOCK_USER].ref_cnt);    /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	SYS_WAKE_LOCK_USER  : %d\n",wakelock->user_info[SYS_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("	APP_WAKE_LOCK_USER  : %d\n",wakelock->user_info[APP_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
		printk("DISPLAY_WAKE_LOCK_USER  : %d\n",wakelock->user_info[DISPLAY_WAKE_LOCK_USER].ref_cnt);      /* 打印对应类型或使用者的当前引用快照，帮助定位未释放锁。 */
	}
}


/**
  * @brief  清零管理器、初始化通知信号量并建立各类型对象。
  * @param  arg：[输入]系统初始化框架传入的设备参数，当前未使用。
  * @retval 固定返回 0。
  * @note   POST_KERNEL 优先级 50 执行；末尾原有调试加锁保留，初始化后 FULL 引用并非零。
  */
static int sys_wakelocks_init(const struct device *arg)
{
	uint32_t current_timestamp = k_uptime_get_32();                                /* 初始化时间基准使用系统启动后的毫秒数。 */

	memset(&wakelocks_manager, 0 , sizeof(struct wakelocks_manager_context_t));    /* 清零全部引用、使用者计数及管理标志。 */
	k_sem_init(&wakelocks_sem, 0, 1);                                              /* 共享通知初始无许可，最大仅保留一个通知。 */

	for (int i = 0; i < MAX_WAKE_LOCK_TYPE; i++) {                                 /* 遍历已支持的类型表项。 */
		wakelocks_manager.wakelocks[i].wake_lock_type = i;                     /* 建立数组索引到类型编号的对应关系。 */
		wakelocks_manager.wakelocks[i].free_timestamp = current_timestamp;     /* 各类型初始释放时刻使用同一个时间快照。 */
		wakelocks_manager.wakelocks[i].ref_cnt = 0;                            /* 初始化时该类型总引用为零。 */
	}
	wakelocks_manager.wakelock_enable = 1;                                         /* 启用计数查询和释放时长查询。 */
	/* 保留原有调试 FULL 加锁；其引用需由对应启动流程安排释放。 */
	sys_wake_lock(FULL_WAKE_LOCK);                                                 /* 保留初始化末尾的调试 FULL 引用。 */
	return 0;                                                                      /* 本次初始化或通知包装结束。 */
}
SYS_INIT(sys_wakelocks_init, POST_KERNEL, 50);    /* 内核启动后优先级 50 注册管理器初始化。 */


/**
  * @brief  读取指定类型当前总引用计数。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 非负引用计数；管理禁用或类型未找到时为 0。
  * @note   使用中断临界区保护读取；0 可能是禁用，不必然证明所有对象原始引用都为零。
  */
int sys_wakelocks_check(int wake_lock_type)
{
	int ref_cnt = 0;                                              /* 未启用或未找到时默认返回零。 */
	struct wakelock_t *wakelock = NULL;                           /* 查找前清空对象指针，错误出口可据此避免解引用。 */
	uint32_t key = irq_lock();                                    /* 保护共享引用计数及时间戳，保存进入前中断状态。 */
	if (wakelocks_manager.wakelock_enable) {                      /* 管理机制启用时才读取实际计数或计算空闲时间。 */
		wakelock = _sys_wakelock_lookup(wake_lock_type);      /* 查找本次要操作的类型对象。 */
		if (!wakelock) {                                      /* 未找到类型时报告错误并走统一恢复中断出口。 */
			printk("err, holder: %d", wake_lock_type);    /* 打印无法识别的类型编号。 */
			goto exit;                                    /* 跳转到统一出口，确保恢复中断状态。 */
		}
		ref_cnt =  wakelock->ref_cnt;                         /* 在临界区中取得该类型总引用快照。 */
	}
exit:
	irq_unlock(key);                                              /* 恢复进入临界区前的中断状态。 */
	return ref_cnt;                                               /* 返回实际快照或默认零。 */
}

/**
  * @brief  计算指定类型已无引用的持续时间。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 无引用时返回毫秒差加 1；仍持锁或类型不存在返回 0；管理禁用返回 0xFFFFFFFF。
  * @note   使用 k_uptime_get_32 的毫秒时间；无符号减法按 32 位回绕。不是 OSA 的 5 ms tick。
  */
uint32_t sys_wakelocks_get_free_time(int wake_lock_type)
{
	uint32_t free_time = 0;                                                          /* 默认零表示当前仍持锁或没有可用类型。 */
	struct wakelock_t *wakelock = NULL;                                              /* 查找前清空对象指针，错误出口可据此避免解引用。 */
	uint32_t key = irq_lock();                                                       /* 保护共享引用计数及时间戳，保存进入前中断状态。 */

	if (wakelocks_manager.wakelock_enable) {                                         /* 管理机制启用时才读取实际计数或计算空闲时间。 */
		wakelock = _sys_wakelock_lookup(wake_lock_type);                         /* 查找本次要操作的类型对象。 */
		if (!wakelock) {                                                         /* 未找到类型时报告错误并走统一恢复中断出口。 */
			printk("err, holder: %d", wake_lock_type);                       /* 打印无法识别的类型编号。 */
			goto exit;                                                       /* 跳转到统一出口，确保恢复中断状态。 */
		}

		if (!wakelock->ref_cnt) {                                                /* 仅全部引用为零时累计释放持续时间。 */
			free_time = k_uptime_get_32() - wakelock->free_timestamp + 1;    /* 按毫秒无符号差计算时长，加一以区分仍持锁的零值。 */
		}
	} else {
		free_time = 0xffffffff;                                                  /* 禁用管理时返回全 1 哨兵。 */
	}

exit:
	irq_unlock(key);                                                                 /* 恢复进入临界区前的中断状态。 */
	return free_time;                                                                /* 返回释放时长或约定的状态哨兵。 */
}


/**
  * @brief  等待全局唤醒锁通知信号量。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 取得信号量后固定返回 0。
  * @note   参数当前未参与选择；所有类型共享同一信号量，K_FOREVER 可永久阻塞，不可在中断中等待。
  */
int sys_wakelocks_wait(int wake_lock_type)
{
	k_sem_take(&wakelocks_sem, K_FOREVER);    /* 阻塞等待共享通知，不按类型分别等待。 */
	return 0;                                 /* 本次初始化或通知包装结束。 */
}

/**
  * @brief  给出一次全局唤醒锁通知许可。
  * @param  wake_lock_type：[输入]PARTIAL_WAKE_LOCK 或 FULL_WAKE_LOCK。
  * @retval 固定返回 0。
  * @note   参数当前未参与选择；共享容量 1 的信号量，不是每个类型独立通知队列。
  */
int sys_wakelocks_wake(int wake_lock_type)
{
	k_sem_give(&wakelocks_sem);    /* 给共享通知信号量一个许可，重复通知可能合并。 */
	return 0;                      /* 本次初始化或通知包装结束。 */
}

/**
  * @brief  设置唤醒锁查询机制的使能状态。
  * @param  enable：[输入]0 禁用，非零启用。
  * @retval 无。
  * @note   只更新管理使能标志，不清除现存引用计数。
  */
void sys_wakelocks_enable(int enable)
{
	uint32_t key = irq_lock();                     /* 保护共享引用计数及时间戳，保存进入前中断状态。 */
	wakelocks_manager.wakelock_enable = enable;    /* 更新查询使能，不重置引用。 */
	irq_unlock(key);                               /* 恢复进入临界区前的中断状态。 */
}

