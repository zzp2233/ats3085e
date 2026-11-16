/* ARM 线程主动切换入口：SPI 等待信号量或测试线程休眠时，调度器经 arch_swap 请求 PendSV；寄存器保存和恢复主体位于 swap_helper.S。 */
/*
 * Copyright (c) 2018 Linaro, Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>             /* 线程对象、当前线程及中断控制接口。 */
#include <kernel_internal.h>    /* 内核内部架构声明和调度约定。 */

extern const int _k_neg_eagain;    /* kernel/errno.c 定义的 -EAGAIN 常量；未被唤醒方改写时作为默认切换返回值。 */

/**
  * @brief  保存当前线程的中断屏蔽状态并请求架构线程切换。
  * @param  key：[输入]进入调度临界区前的中断屏蔽键；当前 Cortex-M33 使用 BASEPRI，ARMv6-M 使用 PRIMASK。
  * @retval 返回当前线程重新获得执行权时保存的 swap_return_value；默认是 -EAGAIN，正常唤醒路径可改写为 0 或其他状态。
  * @note   调用时应已选择下一线程并锁定可屏蔽内核中断。当前 Cortex-M 路径借 PendSV 完成上下文切换，硬件先保存异常栈帧，汇编再保存被调用者保存寄存器；BASEPRI 不屏蔽所有异常。Cortex-R 分支改用 SVC，不属于当前构建。
  */
int arch_swap(unsigned int key)
{
	/* 保存旧屏蔽阈值及默认状态，待当前线程重新调度时恢复。 */
	_current->arch.basepri = key;                        /* 为本线程保存进入临界区前的屏蔽键，供切回时恢复。 */
	_current->arch.swap_return_value = _k_neg_eagain;    /* 预置超时/未成功唤醒状态，唤醒方可通过线程上下文改写。 */

#if defined(CONFIG_CPU_CORTEX_M)
	/* 将 PendSV 置为挂起；它在中断优先级和屏蔽条件允许时进入。 */
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;                 /* 向系统控制块 ICSR 写 PendSV 请求位。 */

	/* 把内核中断屏蔽键恢复为 0，使挂起的 PendSV 可以执行。 */
	irq_unlock(0);                                       /* 解除内核所用中断屏蔽；不等于允许所有不可屏蔽异常。 */
#elif defined(CONFIG_CPU_CORTEX_R)
	/* Cortex-R 的异常入口和恢复机制；当前 Cortex-M33 构建不编译此分支。 */
	z_arm_cortex_r_svc();                                /* Cortex-R 无 PendSV，使用软件异常进入切换入口。 */
	irq_unlock(key);                                     /* Cortex-R 分支恢复调用前的中断状态。 */
#endif

	/* 切换后本线程可能长时间不运行；执行到此处说明它已重新成为当前线程。 */
	return _current->arch.swap_return_value;             /* 从重新调度后的当前线程上下文取回唤醒结果。 */
}
