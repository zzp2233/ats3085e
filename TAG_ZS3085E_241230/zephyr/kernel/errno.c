/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 线程局部 errno 地址访问，以及供线程切换使用的 -EAGAIN 常量。
 * errno 地址查询只返回当前线程已有存储，不会主动触发线程切换。
 */

#include <kernel.h>             /* 当前线程对象、EAGAIN 错误值和内核类型。 */
#include <syscall_handler.h>    /* 用户态系统调用的实现/校验入口约定。 */

/* errno.h 不能直接安全地由汇编包含，因此用 C 定义一个可供汇编装载的 -EAGAIN。
 * 原实现指出该常量额外占用 4 字节；当前 arch_swap 也用它预置线程阻塞返回值。
 */
const int _k_neg_eagain = -EAGAIN;    /* 真正的常量定义，声明和汇编引用均指向此对象。 */

/* 只有开启 errno 支持才选择线程局部变量或访问函数；上面的切换常量不受此配置限制。 */
#ifdef CONFIG_ERRNO

#ifdef CONFIG_ERRNO_IN_TLS
__thread int z_errno_var;    /* ERRNO_IN_TLS 分支为每线程各保留一个 errno 变量，初始清零。 */
#else

#ifdef CONFIG_USERSPACE

/**
  * @brief  返回当前线程的 errno 存储地址。
  * @param  无。
  * @retval 可读写 int 地址，归当前线程所有，不转移内存所有权。
  * @note   仅启用 ERRNO 且不把 errno 放在线程局部存储时编译；USERSPACE 分支使用栈内局部数据，其他分支使用控制块成员。
  */
int *z_impl_z_errno(void)
{
	/* 用户态局部数据在建栈时放到用户可访问栈区的低地址一端，
	 * 使线程可以直接读写自己的 errno，而不用为每次访问切换上下文。
	 */
	return &_current->userspace_local_data->errno_var;    /* USERSPACE 分支的可访问 errno 区，线程可直接读写。 */
}

/**
  * @brief  提供用户系统调用访问当前线程 errno 的包装。
  * @param  无。
  * @retval 与 z_impl_z_errno 相同的当前线程 errno 地址。
  * @note   仅 USERSPACE 分支；无用户参数需要校验，不分配存储。
  */
static inline int *z_vrfy_z_errno(void)
{
	return z_impl_z_errno();    /* 校验包装委托实现，返回本线程地址，不接收用户输入。 */
}
/* 系统调用编组源由构建生成，只通过 include 使用，不改动生成内容。 */
#include <syscalls/z_errno_mrsh.c>

#else

/**
  * @brief  返回当前线程的 errno 存储地址。
  * @param  无。
  * @retval 可读写 int 地址，归当前线程所有，不转移内存所有权。
  * @note   仅启用 ERRNO 且不把 errno 放在线程局部存储时编译；USERSPACE 分支使用栈内局部数据，其他分支使用控制块成员。
  */
int *z_impl_z_errno(void)
{
	return &_current->errno_var;    /* 无用户态分支直接使用线程控制块内的 errno 字段。 */
}
#endif    /* CONFIG_USERSPACE */

#endif    /* CONFIG_ERRNO_IN_TLS */

#endif    /* CONFIG_ERRNO */
