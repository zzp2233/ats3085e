/*
 * Copyright (c) 2013-2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 双向链表的内联宏和函数。
 *
 * 接口本身不保证线程安全；跨线程/中断使用时由调用者持锁或提供其他同步。
 * 空链表的头尾都指向链表自身作为哨兵，非空节点首尾也通过该哨兵连接成环，
 * 因而插入/删除无需为首尾单独修改算法。超时队列用此链表保存按到期顺序排列的对象。
 */

#ifndef ZEPHYR_INCLUDE_SYS_DLIST_H_
#define ZEPHYR_INCLUDE_SYS_DLIST_H_    /* 头文件保护，避免链表类型与内联定义重复。 */

#include <stddef.h>
#include <stdbool.h>
#include <toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup doubly-linked-list_apis 双向链表
 * @ingroup datastructure_apis
 * @{
 */

/* 链表头和普通节点复用两个指针槽：头部解释为head/tail，节点解释为next/prev，避免额外哨兵类型。 */
struct _dnode {
	union {
		struct _dnode *head;    /* 作链表头时指向首节点，空表时指向自身。 */
		struct _dnode *next;    /* 作节点时指向后继，未链接时为NULL。 */
	};
	union {
		struct _dnode *tail;    /* 作链表头时指向尾节点，空表时指向自身。 */
		struct _dnode *prev;    /* 作节点时指向前驱，未链接时为NULL。 */
	};
};

typedef struct _dnode sys_dlist_t;    /* 链表头类型，与节点共享布局但承担哨兵职责。 */
typedef struct _dnode sys_dnode_t;    /* 嵌入宿主对象的节点类型，不单独分配内存。 */


/**
  * @brief  顺序遍历节点，循环体必须自行加花括号；不允许在循环体删除当前节点。
  * @param  __dl：[输入] 已初始化链表的非空指针。
  * @param  __dn：[输出] sys_dnode_t*循环变量，依次接收节点。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_FOR_EACH_NODE(__dl, __dn)				\
	for (__dn = sys_dlist_peek_head(__dl); __dn != NULL;		\
	     __dn = sys_dlist_peek_next(__dl, __dn))

/**
  * @brief  从给定节点的后继继续遍历，起点为空则从首节点开始。
  * @param  __dl：[输入] 已初始化链表。
  * @param  __dn：[输入输出] 起始节点或NULL，随后作为循环变量；不能在循环体删除当前项。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_ITERATE_FROM_NODE(__dl, __dn) \
	for (__dn = __dn ? sys_dlist_peek_next_no_check(__dl, __dn) \
			 : sys_dlist_peek_head(__dl); \
	     __dn != NULL; \
	     __dn = sys_dlist_peek_next(__dl, __dn))

/**
  * @brief  保存后继后遍历节点，允许从同一线程的循环体移除当前项。
  * @param  __dl：[输入] 已初始化非空链表头指针。
  * @param  __dn：[输出] 当前节点循环变量。
  * @param  __dns：[输出] 后继暂存变量，不能在循环体破坏其目标。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_FOR_EACH_NODE_SAFE(__dl, __dn, __dns)			\
	for (__dn = sys_dlist_peek_head(__dl),				\
		     __dns = sys_dlist_peek_next(__dl, __dn);		\
	     __dn != NULL; __dn = __dns,				\
		     __dns = sys_dlist_peek_next(__dl, __dn))

/**
  * @brief  由嵌入节点还原宿主结构指针，节点为空时返回NULL。
  * @param  __dn：[输入] 节点指针或NULL。
  * @param  __cn：[输入] 用于推导宿主类型的指针表达式。
  * @param  __n：[输入] 宿主结构中的节点成员名。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_CONTAINER(__dn, __cn, __n) \
	((__dn != NULL) ? CONTAINER_OF(__dn, __typeof__(*__cn), __n) : NULL)

/**
  * @brief  读取链表首节点的宿主对象。
  * @param  __dl：[输入] 已初始化链表。
  * @param  __cn：[输入] 用于推导宿主结构类型的指针。
  * @param  __n：[输入] 节点成员名。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_PEEK_HEAD_CONTAINER(__dl, __cn, __n) \
	SYS_DLIST_CONTAINER(sys_dlist_peek_head(__dl), __cn, __n)

/**
  * @brief  读取当前宿主对象之后的链表对象。
  * @param  __dl：[输入] 已初始化链表。
  * @param  __cn：[输入] 当前宿主指针，可为空。
  * @param  __n：[输入] 节点成员名。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_PEEK_NEXT_CONTAINER(__dl, __cn, __n) \
	((__cn != NULL) ? \
	 SYS_DLIST_CONTAINER(sys_dlist_peek_next(__dl, &(__cn->__n)),	\
				      __cn, __n) : NULL)

/**
  * @brief  按宿主结构指针遍历链表，循环体须加花括号且不能删除当前项。
  * @param  __dl：[输入] 已初始化链表。
  * @param  __cn：[输出] 宿主指针循环变量。
  * @param  __n：[输入] 宿主节点成员名。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_FOR_EACH_CONTAINER(__dl, __cn, __n)			\
	for (__cn = SYS_DLIST_PEEK_HEAD_CONTAINER(__dl, __cn, __n);     \
	     __cn != NULL;                                              \
	     __cn = SYS_DLIST_PEEK_NEXT_CONTAINER(__dl, __cn, __n))

/**
  * @brief  保存后继宿主后遍历，允许在循环体移除当前对象。
  * @param  __dl：[输入] 已初始化链表。
  * @param  __cn：[输出] 当前宿主指针变量。
  * @param  __cns：[输出] 后继宿主暂存变量。
  * @param  __n：[输入] 节点成员名。
  * @retval 遍历宏展开为for循环；查询宏返回宿主指针或NULL。
  * @note   所谓安全只保证删除当前项不破坏本次遍历，不提供并发互斥；调用者同步并保证宿主生命周期。
  */
#define SYS_DLIST_FOR_EACH_CONTAINER_SAFE(__dl, __cn, __cns, __n)	\
	for (__cn = SYS_DLIST_PEEK_HEAD_CONTAINER(__dl, __cn, __n),	\
	     __cns = SYS_DLIST_PEEK_NEXT_CONTAINER(__dl, __cn, __n);    \
	     __cn != NULL; __cn = __cns,				\
	     __cns = SYS_DLIST_PEEK_NEXT_CONTAINER(__dl, __cn, __n))

/**
  * @brief  建立首尾均指向自身的空链表。
  * @param  list：[输出] 非空可写链表头，重初始化前须确认原节点已妥善处理。
  * @retval 无。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline void sys_dlist_init(sys_dlist_t *list)
{
	list->head = (sys_dnode_t *)list;    /* 空表首指针指向哨兵自身。 */
	list->tail = (sys_dnode_t *)list;    /* 空表尾指针也指向哨兵，构成闭环。 */
}

#define SYS_DLIST_STATIC_INIT(ptr_to_list) { {(ptr_to_list)}, {(ptr_to_list)} }    /* 静态初始化空链表：两个联合体字段都指回ptr_to_list。 */

/**
  * @brief  将节点标记为未链接状态。
  * @param  node：[输出] 非空可写节点；不得直接用于仍在链表中的节点。
  * @retval 无。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline void sys_dnode_init(sys_dnode_t *node)
{
	node->next = NULL;    /* 清后继，后续is_linked据此识别未链接状态。 */
	node->prev = NULL;    /* 清前驱，避免保留已经失效的链表关系。 */
}

/**
  * @brief  检查节点的后继指针是否表示已链接。
  * @param  node：[输入] 已初始化的非空节点。
  * @retval next非NULL为true，否则false；不验证其是否属于某条指定链表。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline bool sys_dnode_is_linked(const sys_dnode_t *node)
{
	return node->next != NULL;    /* 按后继是否为NULL判定，不做完整链表一致性校验。 */
}

/**
  * @brief  检查给定节点是否为链表首节点。
  * @param  list：[输入] 已初始化非空链表头。
  * @param  node：[输入] 待比较节点地址，不会被解引用。
  * @retval 等于head为true，否则false。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline bool sys_dlist_is_head(sys_dlist_t *list, sys_dnode_t *node)
{
	return list->head == node;    /* 只比较节点地址与head。 */
}

/**
  * @brief  检查给定节点是否为链表尾节点。
  * @param  list：[输入] 已初始化非空链表头。
  * @param  node：[输入] 待比较节点地址，不会被解引用。
  * @retval 等于tail为true，否则false。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline bool sys_dlist_is_tail(sys_dlist_t *list, sys_dnode_t *node)
{
	return list->tail == node;    /* 只比较节点地址与tail。 */
}

/**
  * @brief  检查链表是否没有有效节点。
  * @param  list：[输入] 已初始化非空链表头。
  * @retval head指回链表头为true，否则false。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline bool sys_dlist_is_empty(sys_dlist_t *list)
{
	return list->head == list;    /* 哨兵自指表示空链表。 */
}

/**
  * @brief  检查链表是否含至少两个节点。
  * @param  list：[输入] 已初始化非空链表头。
  * @retval 首尾不同为true；空表或单节点表为false。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline bool sys_dlist_has_multiple_nodes(sys_dlist_t *list)
{
	return list->head != list->tail;    /* 空表与单节点首尾相同，至少两个节点才不同。 */
}

/**
  * @brief  读取首节点而不移除。
  * @param  list：[输入] 已初始化非空链表头。
  * @retval 首节点地址；空表返回NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_peek_head(sys_dlist_t *list)
{
	return sys_dlist_is_empty(list) ? NULL : list->head;    /* 空表返回NULL，避免把哨兵当成宿主节点。 */
}

/**
  * @brief  在已知非空时直接读取首节点。
  * @param  list：[输入] 必须为已初始化且非空的链表。
  * @retval 首节点地址；误传空表将返回哨兵而非NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_peek_head_not_empty(sys_dlist_t *list)
{
	return list->head;    /* 调用者已保证非空，省去一次空表判断。 */
}

/**
  * @brief  读取已知有效节点的后继。
  * @param  list：[输入] 节点所属的已初始化链表。
  * @param  node：[输入] 该表中的非空有效节点。
  * @retval 后继地址；当前为尾节点则返回NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_peek_next_no_check(sys_dlist_t *list,
							sys_dnode_t *node)
{
	return (node == list->tail) ? NULL : node->next;    /* 在尾节点处终止逻辑遍历，不暴露环形哨兵。 */
}

/**
  * @brief  允许当前节点为空地查询后继。
  * @param  list：[输入] 已初始化链表。
  * @param  node：[输入] 该表内节点或NULL。
  * @retval 后继地址；当前为空或尾节点时返回NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_peek_next(sys_dlist_t *list,
					       sys_dnode_t *node)
{
	return (node != NULL) ? sys_dlist_peek_next_no_check(list, node) : NULL;    /* 先排除空节点，再按尾节点规则读取后继。 */
}

/**
  * @brief  读取已知有效节点的前驱。
  * @param  list：[输入] 节点所属已初始化链表。
  * @param  node：[输入] 该表中的非空有效节点。
  * @retval 前驱地址；当前为首节点则返回NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_peek_prev_no_check(sys_dlist_t *list,
							sys_dnode_t *node)
{
	return (node == list->head) ? NULL : node->prev;    /* 首节点没有逻辑前驱，返回NULL而非哨兵。 */
}

/**
  * @brief  允许当前节点为空地查询前驱。
  * @param  list：[输入] 已初始化链表。
  * @param  node：[输入] 该表内节点或NULL。
  * @retval 前驱地址；当前为空或首节点时返回NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_peek_prev(sys_dlist_t *list,
					       sys_dnode_t *node)
{
	return (node != NULL) ? sys_dlist_peek_prev_no_check(list, node) : NULL;    /* 空当前项直接返回NULL，否则读取前驱。 */
}

/**
  * @brief  读取尾节点而不移除。
  * @param  list：[输入] 已初始化非空链表头。
  * @retval 尾节点地址；空表返回NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_peek_tail(sys_dlist_t *list)
{
	return sys_dlist_is_empty(list) ? NULL : list->tail;    /* 空表不返回哨兵，非空返回尾节点。 */
}

/**
  * @brief  把未链接节点添加到链表尾部。
  * @param  list：[输入输出] 已初始化非空链表头。
  * @param  node：[输入输出] 非空未链接节点，存储须保持有效直到被移除。
  * @retval 无。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline void sys_dlist_append(sys_dlist_t *list, sys_dnode_t *node)
{
	sys_dnode_t *const tail = list->tail;    /* 保存原尾节点，空表时即为哨兵。 */

	node->next = list;                       /* 新尾的后继连接到链表头哨兵。 */
	node->prev = tail;                       /* 新尾的前驱连接原尾节点。 */

	tail->next = node;                       /* 原尾向后连接新节点，空表时同时更新head。 */
	list->tail = node;                       /* 把链表尾入口改为新节点。 */
}

/**
  * @brief  把未链接节点添加到链表头部。
  * @param  list：[输入输出] 已初始化非空链表头。
  * @param  node：[输入输出] 非空未链接节点，生命周期覆盖在表期间。
  * @retval 无。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline void sys_dlist_prepend(sys_dlist_t *list, sys_dnode_t *node)
{
	sys_dnode_t *const head = list->head;    /* 保存原首节点，空表时为哨兵。 */

	node->next = head;                       /* 新首向后连接原首节点。 */
	node->prev = list;                       /* 新首的前驱为链表头哨兵。 */

	head->prev = node;                       /* 原首向前连接新节点，空表时同时更新tail。 */
	list->head = node;                       /* 把链表首入口改为新节点。 */
}

/**
  * @brief  将未链接节点插在指定后继之前。
  * @param  successor：[输入输出] 目标位置的有效后继节点，或已初始化链表头哨兵。
  * @param  node：[输入输出] 非空未链接节点，不能与successor相同。
  * @retval 无。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline void sys_dlist_insert(sys_dnode_t *successor, sys_dnode_t *node)
{
	sys_dnode_t *const prev = successor->prev;    /* 保存插入位置的原前驱。 */

	node->prev = prev;                            /* 新节点前驱指向原前驱。 */
	node->next = successor;                       /* 新节点后继指向指定后继。 */
	prev->next = node;                            /* 原前驱向后连接新节点。 */
	successor->prev = node;                       /* 原后继向前连接新节点，四个链接完成插入。 */
}

/**
  * @brief  根据条件回调选择插入位置，未找到时添加到尾部。
  * @param  list：[输入输出] 已初始化链表。
  * @param  node：[输入输出] 未链接且长期有效的节点。
  * @param  cond：[输入] 非空回调，接收当前候选节点及data；非0表示应插在其前，0继续扫描。
  * @param  data：[输入输出] 原样传给cond的上下文，回调允许时可为空。
  * @retval 无。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline void sys_dlist_insert_at(sys_dlist_t *list, sys_dnode_t *node,
	int (*cond)(sys_dnode_t *node, void *data), void *data)
{
	if (sys_dlist_is_empty(list)) {                              /* 空表无需调用条件回调，直接添加唯一节点。 */
		sys_dlist_append(list, node);                        /* 无更早插入位置时追加到尾部。 */
	} else {
		sys_dnode_t *pos = sys_dlist_peek_head(list);        /* 从首节点开始查找满足条件的位置。 */

		while ((pos != NULL) && (cond(pos, data) == 0)) {    /* 候选存在且回调未选中时继续后移，无超时但受有限链表长度限制。 */
			pos = sys_dlist_peek_next(list, pos);        /* 读取下个候选，尾后得到NULL结束扫描。 */
		}
		if (pos != NULL) {                                   /* 找到了回调选择的位置，插在它之前。 */
			sys_dlist_insert(pos, node);                 /* 完成条件选择后的实际插入。 */
		} else {
			sys_dlist_append(list, node);                /* 无更早插入位置时追加到尾部。 */
		}
	}
}

/**
  * @brief  从所属链表摘除节点并将其标为未链接。
  * @param  node：[输入输出] 已链接的非空有效节点，不能传链表哨兵或未链接节点。
  * @retval 无。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline void sys_dlist_remove(sys_dnode_t *node)
{
	sys_dnode_t *const prev = node->prev;    /* 保存待移除节点的前驱。 */
	sys_dnode_t *const next = node->next;    /* 保存待移除节点的后继。 */

	prev->next = next;                       /* 前驱绕过当前节点，直接连接后继。 */
	next->prev = prev;                       /* 后继直接回连前驱，完成链表摘除。 */
	sys_dnode_init(node);                    /* 断开被摘下节点的链接，允许之后再次入队。 */
}

/**
  * @brief  摘除并返回链表首节点。
  * @param  list：[输入输出] 已初始化非空链表头。
  * @retval 摘下的首节点；空表返回NULL。
  * @note   接口不加锁，不分配或释放宿主内存；跨线程/中断使用时由调用者保证同步。
  */
static inline sys_dnode_t *sys_dlist_get(sys_dlist_t *list)
{
	sys_dnode_t *node = NULL;           /* 默认空表时没有可返回节点。 */

	if (!sys_dlist_is_empty(list)) {    /* 只有非空表才允许摘取有效首节点。 */
		node = list->head;          /* 保存即将移除的首节点地址。 */
		sys_dlist_remove(node);     /* 通过同一摘除逻辑更新首尾及节点状态。 */
	}

	return node;                        /* 返回摘下节点或NULL，宿主内存仍由调用者管理。 */
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_SYS_DLIST_H_ */
