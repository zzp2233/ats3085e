/* 内核自旋锁：先屏蔽本CPU可屏蔽中断，SMP时再用原子变量排他。临界区应短且不能调用会挂起的等待；SPI等待许可由信号量与调度器交接，不能持本锁无限等待外设。 */
/*
 * Copyright (c) 2018 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_SPINLOCK_H_
#define ZEPHYR_INCLUDE_SPINLOCK_H_    /* 保护结构体、类型和内联实现不被重复定义。 */

#include <sys/atomic.h>
#include <sys/__assert.h>
#include <stdbool.h>
#include <arch/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct z_spinlock_key {
	int key;    /* 架构中断恢复值，应用不得自行解释。 */
};

/**
 * @brief 内核自旋锁
 *
 * 该结构记录锁状态；CPU通过k_spin_lock等待取得所有权，应用可按受保护对象定义多把锁。
 */
struct k_spinlock {
#ifdef CONFIG_SMP
	atomic_t locked;    /* SMP的原子所有权标记，0为空闲、1为已持有。 */
#endif

#ifdef CONFIG_SPIN_VALIDATE
	/* 保存持锁线程，低两位编码持锁CPU编号；仅锁验证配置使用。 */
	uintptr_t thread_cpu;    /* 验证配置的线程/CPU所有者记录。 */
#endif

#if !defined(CONFIG_SMP) && !defined(CONFIG_SPIN_VALIDATE)
	/* 单核且关闭验证时没有实际锁状态，仍保留1字节占位。
	 * 原说明提到空结构在C/C++中的大小差异会改变外层结构成员偏移；
	 * 当前条件下无论是否启用C++都存在此成员，以保持布局一致。 */
	char dummy;    /* 无运行时锁字段时保证结构布局非空。 */
#endif
};

/* 启用锁验证可检查递归和所有权等错误，但原说明估算增加约3KB代码；
 * 具体开销由构建决定，不适合只为普通互斥而默认开启。 */
#ifdef CONFIG_SPIN_VALIDATE

/**
  * @brief  检查当前上下文是否允许取得此锁。
  * @param  l：[输入输出] 待验证或登记的有效锁，不可为空。
  * @retval true：允许；false：发现递归等非法取得条件。
  * @note   仅CONFIG_SPIN_VALIDATE启用；一致性检查另需CONFIG_KERNEL_COHERENCE。
  */
bool z_spin_lock_valid(struct k_spinlock *l);

/**
  * @brief  检查当前上下文是否有权释放此锁。
  * @param  l：[输入输出] 待验证或登记的有效锁，不可为空。
  * @retval true：允许；false：当前上下文不是合法所有者。
  * @note   仅CONFIG_SPIN_VALIDATE启用；一致性检查另需CONFIG_KERNEL_COHERENCE。
  */
bool z_spin_unlock_valid(struct k_spinlock *l);

/**
  * @brief  记录锁的线程/CPU所有者用于后续验证。
  * @param  l：[输入输出] 待验证或登记的有效锁，不可为空。
  * @retval 无。
  * @note   仅CONFIG_SPIN_VALIDATE启用；一致性检查另需CONFIG_KERNEL_COHERENCE。
  */
void z_spin_lock_set_owner(struct k_spinlock *l);
BUILD_ASSERT(CONFIG_MP_NUM_CPUS <= 4, "Too many CPUs for mask");    /* 验证记录只用低两位编码CPU，最多支持4个CPU。 */

# ifdef CONFIG_KERNEL_COHERENCE

/**
  * @brief  检查锁对象是否满足架构缓存一致性要求。
  * @param  l：[输入输出] 待验证或登记的有效锁，不可为空。
  * @retval true：内存位置满足一致性要求；false：不满足。
  * @note   仅CONFIG_SPIN_VALIDATE启用；一致性检查另需CONFIG_KERNEL_COHERENCE。
  */
bool z_spin_lock_mem_coherent(struct k_spinlock *l);
# endif    /* CONFIG_KERNEL_COHERENCE */

#endif    /* CONFIG_SPIN_VALIDATE */

/**
 * @brief 自旋锁恢复钥匙类型
 *
 * 记录k_spin_lock调用前的中断状态，必须原样传给匹配的k_spin_unlock。
 * 应用把它当作不透明值，不自行修改或猜测内部编码。
 */
typedef struct z_spinlock_key k_spinlock_key_t;

/**
  * @brief  取得自旋锁并保存本CPU此前的中断状态。
  * @param  l：[输入输出] 非空、长期有效且零初始化的锁对象，不能递归取得同一把锁。
  * @retval 供匹配k_spin_unlock恢复中断状态的钥匙，须原样保存。
  * @note   单核主要靠屏蔽中断；SMP在原子比较交换中忙等其他CPU。可嵌套不同锁但须反序释放；持锁期间不可阻塞或挂起。
  */
static ALWAYS_INLINE k_spinlock_key_t k_spin_lock(struct k_spinlock *l)
{
	ARG_UNUSED(l);                                                 /* 单核且无验证时不需访问锁对象，保留接口一致。 */
	k_spinlock_key_t k;                                            /* 保存进入临界区前的本CPU中断状态。 */

	/* 直接调用架构中断屏蔽操作；SMP的irq_lock本身可能再包装全局自旋锁，
	 * 此处调用它会引入不必要的嵌套/递归依赖。 */
	k.key = arch_irq_lock();                                       /* 先屏蔽本CPU中断，避免锁被同CPU中断处理器递归争用。 */

#ifdef CONFIG_SPIN_VALIDATE
	__ASSERT(z_spin_lock_valid(l), "Recursive spinlock %p", l);    /* 验证取得锁不会递归，错误时按断言配置处理。 */
# ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(z_spin_lock_mem_coherent(l));                  /* 要求锁位于满足架构一致性约束的内存。 */
# endif
#endif

#ifdef CONFIG_SMP
	while (!atomic_cas(&l->locked, 0, 1)) {                        /* SMP下反复尝试从空闲改为持有，成功前持续忙等，无超时。 */
	}
#endif

#ifdef CONFIG_SPIN_VALIDATE
	z_spin_lock_set_owner(l);                                      /* 取得锁后记录当前所有者，便于检测错误释放。 */
#endif
	return k;                                                      /* 返回恢复钥匙，不能把另一把锁的钥匙用于本锁。 */
}

/**
  * @brief  交出自旋锁并恢复取得锁之前的本CPU中断状态。
  * @param  l：[输入输出] 当前CPU持有的有效锁对象。
  * @param  key：[输入] 最近一次匹配k_spin_lock返回的原始钥匙。
  * @retval 无。
  * @note   严格按嵌套逆序释放；SMP先清原子锁再恢复中断，其他CPU随后可取得该锁。
  */
static ALWAYS_INLINE void k_spin_unlock(struct k_spinlock *l,
					k_spinlock_key_t key)
{
	ARG_UNUSED(l);                                                /* 单核且无验证时不需访问锁对象，保留接口一致。 */
#ifdef CONFIG_SPIN_VALIDATE
	__ASSERT(z_spin_unlock_valid(l), "Not my spinlock %p", l);    /* 验证当前CPU/线程是此锁所有者。 */
#endif

#ifdef CONFIG_SMP
	/* 当前CPU持锁，只需把状态清零，但仍使用原子交换而非普通赋值：
	 * 某些架构需要其提供的内存排序保证，使受保护写入先于其他CPU取得锁可见。 */
	atomic_clear(&l->locked);                                     /* 原子交出SMP锁所有权，并使用架构原子操作的排序语义。 */
#endif
	arch_irq_unlock(key.key);                                     /* 恢复匹配取得锁之前的中断屏蔽状态。 */
}

/* 内部辅助：只交出锁的所有权，保留本CPU中断屏蔽状态；后续必须由调用方完成状态恢复。 */

/**
  * @brief  仅释放锁的所有权，保留本CPU中断屏蔽状态。
  * @param  l：[输入输出] 当前CPU持有的非空有效锁对象。
  * @retval 无。
  * @note   内部交接使用；不会恢复中断，调用方须在后续切换或退出路径正确恢复。
  */
static ALWAYS_INLINE void k_spin_release(struct k_spinlock *l)
{
	ARG_UNUSED(l);                                                /* 单核且无验证时不需访问锁对象，保留接口一致。 */
#ifdef CONFIG_SPIN_VALIDATE
	__ASSERT(z_spin_unlock_valid(l), "Not my spinlock %p", l);    /* 验证当前CPU/线程是此锁所有者。 */
#endif
#ifdef CONFIG_SMP
	atomic_clear(&l->locked);                                     /* 原子交出SMP锁所有权，并使用架构原子操作的排序语义。 */
#endif
}

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_SPINLOCK_H_ */
