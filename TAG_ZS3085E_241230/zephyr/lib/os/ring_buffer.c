/* 软件环形队列的索引维护、条目收发和字节预留/提交实现。UART 中断在 RX 字节环中发布数据，Shell 线程通过 ring_buf_get 消费。 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/ring_buffer.h>    /* 引入控制对象和公开接口声明。 */
#include <string.h>             /* 条目外的字节传输使用 memcpy。 */

/* LCOV_EXCL_START */
/**
  * @brief  返回逻辑游标需要折返的阈值。
  * @param  无。
  * @retval 默认返回 RING_BUFFER_MAX_SIZE。
  * @note   弱符号允许测试替换为较小值，提前走到游标折返分支。
  */
uint32_t __weak ring_buf_get_rewind_threshold(void)
{
	return RING_BUFFER_MAX_SIZE;    /* 返回控制对象算法默认使用的折返阈值。 */
}
/* LCOV_EXCL_STOP */

/** @brief 条目模式为每条消息保存一个 32 位头，后面跟 length 个 32 位正文字。字节队列不附带这个头。 */
struct ring_element {
	uint32_t  type   :16;    /* 调用方约定的 16 位条目类型。 */
	uint32_t  length :8;     /* 正文字数，以 32 位为单位，最大 255。 */
	uint32_t  value  :8;     /* 随条目头携带的小整数。 */
};

/**
  * @brief  将逻辑位置映射到环数组中的物理下标。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @param  val [输入] 待读取或写入的逻辑序号。
  * @retval 0 至 buf->size-1 范围内的索引。
  * @note   实例容量须大于零；建立非零掩码时按位与，其余容量使用取余。
  */
static uint32_t mod(struct ring_buf *buf, uint32_t val)
{
	return likely(buf->mask) ? val & buf->mask : val % buf->size;    /* 使用预先计算的掩码，或按非零容量取余。 */
}

/**
  * @brief  求折返时从逻辑游标中减去的容量整倍数。
  * @param  buf_size [输入] 非零环容量。
  * @param  threshold [输入] 启动折返的计数阈值，应不小于容量。
  * @retval 可同时保持物理下标和两端距离的整倍数。
  * @note   在阈值向下取整的基础上留出一整圈，先折返写端，再让落后的读端有足够余量折返。
  */
static uint32_t get_rewind_value(uint32_t buf_size, uint32_t threshold)
{
	return buf_size * (threshold / buf_size - 1);    /* 减去容量的整倍数后仍指向同一个数组单元。 */
}

/**
  * @brief  比较已提交的读、写位置，判断队列是否为空。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @retval 1 为空，0 为有数据。
  * @note   只观察已提交的位置，字节模式中的临时预留不单独计入。
  */
int ring_buf_is_empty(struct ring_buf *buf)
{
	uint32_t tail = buf->tail;                                            /* 读取写方上次完成提交的位置。 */
	uint32_t head = buf->head;                                            /* 读方上次完成消费的位置。 */

	if (tail < head) {                                                    /* 写游标已先折返时，临时将其换回读游标所在的计数区间。 */
		tail += get_rewind_value(buf->size,
					 ring_buf_get_rewind_threshold());    /* 按当前实例容量和运行时阈值恢复等价位置。 */
	}

	return (head == tail);                                                /* 同一计数区间内相等时，没有待取数据。 */
}

/**
  * @brief  从已提交写位置和读位置计算队列占用量。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @retval 字节模式按字节，条目模式按 32 位字计数且包括各条目的头。
  */
uint32_t ring_buf_size_get(struct ring_buf *buf)
{
	uint32_t tail = buf->tail;                                            /* 保存当前已发布的写端位置。 */
	uint32_t head = buf->head;                                            /* 取得读方已消费的边界。 */

	if (tail < head) {                                                    /* 处理写游标先于读游标折返的情形。 */
		tail += get_rewind_value(buf->size,
					 ring_buf_get_rewind_threshold());    /* 加回整圈倍数，避免按较小写指针计算错误的长度。 */
	}

	return tail - head;                                                   /* 快照差值给出已发布但未消费的数量。 */
}

/**
  * @brief  计算扣除已入队数据后的剩余容量。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @retval 字节模式为字节数，条目模式为剩余的 32 位字数。
  * @note   只使用已提交位置；进行中的临时写预留由 put_claim 单独扣除。
  */
uint32_t ring_buf_space_get(struct ring_buf *buf)
{
	return buf->size - ring_buf_size_get(buf);    /* 总容量减去实际占用量。 */
}

/**
  * @brief  将类型、附带值和一段 32 位数据作为一个完整条目入队。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  type [输入] 由应用解释的 16 位类型标识。
  * @param  value [输入] 随条目保存的 8 位数值。
  * @param  data [输入] 至少 size32 个可读的 32 位字，长度为零时可为空。
  * @param  size32 [输入] 正文字数，0～255；头部另占一个 32 位字。
  * @retval 0 表示条目已提交；-EMSGSIZE 表示空位不足以容纳完整条目。
  * @note   同一实例使用条目接口，多个写方须互斥或以其他方式避免同时写入。
  */
int ring_buf_item_put(struct ring_buf *buf, uint16_t type, uint8_t value,
		      uint32_t *data, uint8_t size32)
{
	uint32_t i, space, index, rc;                                               /* i 遍历正文，space 为可写字数，index 为数组下标，rc 保存结果。 */
	uint32_t threshold = ring_buf_get_rewind_threshold();                       /* 取得这次提交使用的游标阈值。 */
	uint32_t rew;                                                               /* 超过阈值时保存本次要扣掉的整圈数值。 */

	/* 条目写入以 32 位字为单位，须一次容纳头和完整正文。 */
	space = ring_buf_space_get(buf);                                            /* 先计算本条记录能否一次容纳。 */
	if (space >= (size32 + 1)) {                                                /* 头部占一个字，即使正文为零也必须留出头的位置。 */
		struct ring_element *header =
		    (struct ring_element *)&buf->buf.buf32[mod(buf, buf->tail)];    /* 在当前写端定位这一条的单字头。 */

		header->type = type;                                                /* 写入应用规定的类型。 */
		header->length = size32;                                            /* 记录后续正文字数，供读方决定拷贝长度。 */
		header->value = value;                                              /* 将小整数直接放入头部。 */

		/* 分别处理幂次容量和任意容量的跨尾写入。 */
		if (likely(buf->mask)) {                                            /* 使用掩码的快速索引路径。 */
			for (i = 0U; i < size32; ++i) {                             /* 遍历全部正文字，完成后再提交 tail。 */
				index = (i + buf->tail + 1) & buf->mask;            /* 正文在头部后一格开始，跨数组尾端时环回。 */
				buf->buf.buf32[index] = data[i];                    /* 将当前输入字写入对应位置。 */
			}
		} else {
			for (i = 0U; i < size32; ++i) {                             /* 任意非零容量采用取余寻址。 */
				index = (i + buf->tail + 1) % buf->size;            /* 同样跨过一个头部位置。 */
				buf->buf.buf32[index] = data[i];                    /* 写入该字的环内位置。 */
			}
		}

		/* 在数据就位以后统一提交写端位置。 */
		if (buf->tail > threshold) {                                        /* 使用提交前的 tail 决定本轮是否折返。 */
			rew = get_rewind_value(buf->size, threshold);               /* 扣掉容量倍数可保持刚写入内容的物理位置。 */
		} else {
			rew = 0;                                                    /* 本次使用原计数区间。 */
		}

		buf->tail = buf->tail + (size32 + 1 - rew);                         /* 发布整个条目，读方此后才应看到这份记录。 */
		rc = 0U;                                                            /* 标记完整入队。 */
	} else {
		buf->misc.item_mode.dropped_put_count++;                            /* 空位不足时累计一次丢弃，保留原有条目。 */
		rc = -EMSGSIZE;                                                     /* 报告空间不足，与未提供数据的空正文条目区分。 */
	}

	return rc;                                                                  /* 将本次完整写入或失败的状态交还调用者。 */
}

/**
  * @brief  取出队首条目的类型、附带值和可选正文。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  type [输出] 非空指针，成功时接收类型标识。
  * @param  value [输出] 非空指针，成功时接收条目附带的 8 位数值。
  * @param  data [输出] 可写数组容量由 *size32 给定；传空会消费条目并丢弃正文。
  * @param  size32 [输入输出] 非空；输入正文容量，成功或容量不足时返回实际所需的 32 位字数。
  * @retval 0 表示已消费；-EAGAIN 表示缓冲为空；-EMSGSIZE 表示输出容量不足，此时条目保留。
  * @note   同一实例使用条目接口，多个读方须串行化读取。
  */
int ring_buf_item_get(struct ring_buf *buf, uint16_t *type, uint8_t *value,
		      uint32_t *data, uint8_t *size32)
{
	struct ring_element *header;                                              /* 指向下一条已发布记录的单字头。 */
	uint32_t i, index;                                                        /* 遍历正文字及其物理下标。 */
	uint32_t tail = buf->tail;                                                /* 快照写方已发布位置。 */
	uint32_t rew;                                                             /* 读端本次提交时需要扣除的整圈偏移。 */

	/* 先检查游标折返与空队列，再接触队首记录。 */
	if (tail < buf->head) {                                                   /* 写端已经折返时，读端稍后要同步扣除同一整圈偏移。 */
		rew = get_rewind_value(buf->size,
				       ring_buf_get_rewind_threshold());          /* 依据容量和写端使用的阈值恢复偏移量。 */
		tail += rew;                                                      /* 仅在本地还原写端的逻辑位置。 */
	} else if (ring_buf_is_empty(buf)) {                                      /* 若没有折返而且已空，则不能读取队首。 */
		return -EAGAIN;                                                   /* 用暂时无数据的错误码结束本次尝试。 */
	} else {
		rew = 0;                                                          /* 可按当前计数区间前移读端。 */
	}

	/* 根据头部核对输出容量；失配时允许先获取所需大小再重试。 */
	header = (struct ring_element *) &buf->buf.buf32[mod(buf, buf->head)];    /* 找到队首，先读长度来确定输出空间是否够用。 */

	if (data && (header->length > *size32)) {                                 /* 需要拷贝正文时，先核对调用者提供的容量。 */
		*size32 = header->length;                                         /* 告知调用者重试所需的字数。 */
		return -EMSGSIZE;                                                 /* 尚未推进 head，条目仍可重读。 */
	}

	*size32 = header->length;                                                 /* 返回实际正文字数；丢弃正文时也回报长度。 */
	*type = header->type;                                                     /* 回填条目类型。 */
	*value = header->value;                                                   /* 回填头部附带值。 */

	/* 复制或跳过正文后才允许复用条目占用的槽。 */
	if (data) {                                                               /* 有目标数组时复制正文，空指针仅跳过复制。 */
		if (likely(buf->mask)) {                                          /* 选择与写端相同的掩码索引方式。 */
			for (i = 0U; i < header->length; ++i) {                   /* 依次读取头部标出的正文字数。 */
				index = (i + buf->head + 1) & buf->mask;          /* 从头后的第一字开始，越界时按掩码回绕。 */
				data[i] = buf->buf.buf32[index];                  /* 写入调用者的外部数组。 */
			}
		} else {
			for (i = 0U; i < header->length; ++i) {                   /* 任意容量队列同样逐字读取。 */
				index = (i + buf->head + 1) % buf->size;          /* 通过余数定位正文在环中的位置。 */
				data[i] = buf->buf.buf32[index];                  /* 复制当前字到线性目标。 */
			}
		}
	}

	buf->head = buf->head + header->length + 1 - rew;                         /* 正文及头部都处理完成，再释放整条记录占用的空间。 */

	return 0;                                                                 /* 完整条目已被消费。 */
}

/**
  * @brief  将已知最多越过一个环尾的索引折回。
  * @param  val [输入] 候选下标，调用者须保证小于 2 × max。
  * @param  max [输入] 环容量，必须大于零。
  * @retval val 小于 max 时原值返回，否则返回 val-max。
  * @note   此辅助函数仅减一次；当前实现未调用它。
  */
__unused static inline uint32_t wrap(uint32_t val, uint32_t max)
{
	return val >= max ? (val - max) : val;    /* 只处理一次跨过末端的情形。 */
}

/**
  * @brief  在内部字节数组中预留一段连续可写空间。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 非空地址槽，接收内部写指针，仅可写返回长度覆盖的区域。
  * @param  size [输入] 希望预留的字节数。
  * @retval 本段可写字节数，受空位和数组尾端限制，可为零。
  * @note   同一实例使用字节模式；多个写方须串行化整个写操作，组合使用 claim/finish 时覆盖两步之间的区间。 缓冲跨尾时可再次申请，写完用 @ref ring_buf_put_finish 发布实际字节数。
  */
uint32_t ring_buf_put_claim(struct ring_buf *buf, uint8_t **data, uint32_t size)
{
	uint32_t space, trail_size, allocated, tmp_trail_mod;                                /* 保存未占空间、尾部连续长度、授权长度和预留写下标。 */
	uint32_t head = buf->head;                                                           /* 读端已释放的位置决定写入上限。 */
	uint32_t tmp_tail = buf->misc.byte_mode.tmp_tail;                                    /* 写端临时游标包括此前已预留而尚未发布的字节。 */

	/* 以读端已经释放的位置约束写方预留。 */
	if (buf->misc.byte_mode.tmp_tail < head) {                                           /* 比较前将小于 head 的临时写位置还原到相同的计数区间。 */
		tmp_tail += get_rewind_value(buf->size, ring_buf_get_rewind_threshold());    /* 仅修正用于计算可用空间的快照。 */
	}

	tmp_trail_mod = mod(buf, buf->misc.byte_mode.tmp_tail);                              /* 用真实临时游标计算数组内起点，整圈偏移对位置无影响。 */
	space = (head + buf->size) - tmp_tail;                                               /* 从环容量扣除已占和已经预留的字节。 */
	trail_size = buf->size - tmp_trail_mod;                                              /* 当前写下标到数组末端的字节数。 */

	/* 最终返回的区域必须在单个连续物理段中。 */
	size = MIN(size, space);                                                             /* 先限制在仍可预留的总空间内。 */

	trail_size = buf->size - (tmp_trail_mod);                                            /* 重新取起点至数组尾端的连续长度。 */

	allocated = MIN(trail_size, size);                                                   /* 本次给调用者的指针不能跨过数组末端。 */
	*data = &buf->buf.buf8[tmp_trail_mod];                                               /* 返回内部连续写段的首地址，已满时可用长度为零。 */

	buf->misc.byte_mode.tmp_tail =
		buf->misc.byte_mode.tmp_tail + allocated;                                    /* 只推进临时写端；此前的 tail 仍是读方可见的发布界限。 */

	return allocated;                                                                    /* 返回可以实际写入的连续字节数。 */
}

/**
  * @brief  发布写方已写好的字节，并撤销多余的连续预留。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  size [输入] 从本轮预留起点实际写好的字节数，可传零放弃。
  * @retval 0 为已更新提交位置；-EINVAL 表示提交位置会越过总可写界限。
  * @note   同一实例使用字节模式；多个写方须串行化整个写操作，组合使用 claim/finish 时覆盖两步之间的区间。 调用者负责保证提交量在自己已经取得并写好的空间内。
  */
int ring_buf_put_finish(struct ring_buf *buf, uint32_t size)
{
	uint32_t rew;                                            /* 保存本次提交的逻辑游标折返量。 */
	uint32_t threshold = ring_buf_get_rewind_threshold();    /* 读取可由测试替换的折返阈值。 */

	if ((buf->tail + size) > (buf->head + buf->size)) {      /* 检查提交后的逻辑写位置是否越过容量边界。 */
		return -EINVAL;                                  /* 拒绝超出空间的提交，不改动游标。 */
	}

	/* 将实际写入字节数作为一次发布，未填满的预留区留待下次使用。 */
	if (buf->tail > threshold) {                             /* 在提交前的写位置已超过阈值时折返。 */
		rew = get_rewind_value(buf->size, threshold);    /* 折返保持物理数组下标不变。 */
	} else {
		rew = 0;                                         /* 本次提交仅增加实际写入量。 */
	}

	buf->tail += (size - rew);                               /* 更新公开写位置，从这里起这些字节可以读出。 */
	buf->misc.byte_mode.tmp_tail = buf->tail;                /* 临时游标回到已提交边界，放弃未用预留。 */

	return 0;                                                /* 写入已发布。 */
}

/**
  * @brief  把调用者的数据复制进字节队列，在各段复制完成后一并发布。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输入] 指向至少 size 字节的可读数据。
  * @param  size [输入] 希望写入的字节数。
  * @retval 实际提交的字节数；空位不足时可以只写一部分或返回零。
  * @note   同一实例使用字节模式；多个写方须串行化整个写操作，组合使用 claim/finish 时覆盖两步之间的区间。
  */
uint32_t ring_buf_put(struct ring_buf *buf, const uint8_t *data, uint32_t size)
{
	uint8_t *dst;                                                  /* 指向本次预留的环内连续写段。 */
	uint32_t partial_size;                                         /* 当前段获得的字节数。 */
	uint32_t total_size = 0U;                                      /* 累计已复制但待发布的字节数。 */
	int err;                                                       /* 保存最后一次提交的状态码。 */

	/* 跨越环尾需要分别取得两个连续的目标段。 */
	do {                                                           /* 每次只处理一段连续空间，跨尾时需再申请一段。 */
		partial_size = ring_buf_put_claim(buf, &dst, size);    /* 申请本轮尚需写入的字节。 */
		memcpy(dst, data, partial_size);                       /* 将当前源数据段复制到环内。 */
		total_size += partial_size;                            /* 把已拷贝长度计入最后要发布的数量。 */
		size -= partial_size;                                  /* 扣减尚未复制的字节数。 */
		data += partial_size;                                  /* 将输入指针移到下段起点。 */
	} while (size && partial_size);                                /* 达到请求长度或无剩余空间时结束。 */

	/* 所有分段均已处理后，一次性推进读方可见的发布边界。 */
	err = ring_buf_put_finish(buf, total_size);                    /* 一次性发布所有实际复制的字节。 */
	__ASSERT_NO_MSG(err == 0);                                     /* 检查与前序预留相符的提交是否成功。 */

	return total_size;                                             /* 报告实际入队长度。 */
}

/**
  * @brief  取得队首尚未消费数据的一段连续内部地址。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 非空地址槽，接收内部指针，只能读取返回长度内的数据。
  * @param  size [输入] 请求读取的字节数。
  * @retval 当次授予的连续可读字节数，受已发布量及数组尾部限制。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 使用后调用 @ref ring_buf_get_finish，已释放区域可能立即被写方覆盖。
  */
uint32_t ring_buf_get_claim(struct ring_buf *buf, uint8_t **data, uint32_t size)
{
	uint32_t space, granted_size, trail_size, tmp_head_mod;               /* 保存可读总量、本段授权长度、尾段长度和临时读下标。 */
	uint32_t tail = buf->tail;                                            /* 保存写方已发布的数据边界。 */

	/* 计算从尚未预取的位置到生产者已提交位置之间的距离。 */
	if (tail < buf->misc.byte_mode.tmp_head) {                            /* 写端已折返而临时读位置尚未折返时，须对齐逻辑区间。 */
		tail += get_rewind_value(buf->size,
					 ring_buf_get_rewind_threshold());    /* 仅对本地快照加回偏移，保留写方实际状态。 */
	}

	tmp_head_mod = mod(buf, buf->misc.byte_mode.tmp_head);                /* 确定本次读取在内部数组中的起点。 */
	space = tail - buf->misc.byte_mode.tmp_head;                          /* 已发布位置减临时读位置，排除已预取的部分。 */
	trail_size = buf->size - tmp_head_mod;                                /* 当次连续指针最多可覆盖到数组末端。 */

	/* 同时满足已发布量和连续存储区域两项限制。 */
	granted_size = MIN(size, space);                                      /* 请求过大时仅授予已有数据。 */

	granted_size = MIN(trail_size, granted_size);                         /* 将授予长度再限制在本段连续范围内。 */

	*data = &buf->buf.buf8[tmp_head_mod];                                 /* 返回环内的只读数据位置，具体可读长度用返回值判断。 */
	buf->misc.byte_mode.tmp_head += granted_size;                         /* 仅将临时读位置前移；正式 head 尚未释放空间。 */

	return granted_size;                                                  /* 返回可以直接访问的字节数。 */
}

/**
  * @brief  确认已处理的字节数，将对应环内空间归还给写方。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  size [输入] 从本轮预取起点消费的字节数，零表示保留全部数据。
  * @retval 0 为成功；-EINVAL 表示拟消费范围越过已发布的数据。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 调用者负责保证消费量在已取得的数据范围内。
  */
int ring_buf_get_finish(struct ring_buf *buf, uint32_t size)
{
	uint32_t tail = buf->tail;                                          /* 取写方最近发布的边界。 */
	uint32_t rew;                                                       /* 求出确认消费时读端需要扣回的整圈偏移。 */

	if (tail < buf->misc.byte_mode.tmp_head) {                          /* 写方先折返时，将其快照对齐到读方的计数区间。 */
		rew = get_rewind_value(buf->size,
				       ring_buf_get_rewind_threshold());    /* 计算能保留物理下标的修正量。 */
		tail += rew;                                                /* 临时还原用于范围检查的尾部。 */
	} else {
		rew = 0;                                                    /* 沿用原计数区间。 */
	}

	/* 核对本轮消费终点后再更新读游标。 */
	if ((buf->head + size) > tail) {                                    /* 消费不能越过已发布数据的末端。 */
		return -EINVAL;                                             /* 失败时保留正式和临时读位置。 */
	}

	buf->head += (size - rew);                                          /* 按实际消费量归还空间，并在必要时折返。 */
	buf->misc.byte_mode.tmp_head = buf->head;                           /* 放弃未确认部分的临时预取，让下次从实际 head 重新开始。 */

	return 0;                                                           /* 已完成本次读事务。 */
}

/**
  * @brief  从字节队列取走已发布的数据，必要时分两段越过数组末端。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 有效时为至少 size 字节的可写区域；传空会丢弃实际取到的字节。
  * @param  size [输入] 希望消费的字节数。
  * @retval 实际消费的字节数，空队列返回零。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 UART Shell 在读后端中用它从 rx_ringbuf 取字节，函数本身不解析命令或等待中断。
  */
uint32_t ring_buf_get(struct ring_buf *buf, uint8_t *data, uint32_t size)
{
	uint8_t *src;                                                  /* 指向本次取得的环内连续段。 */
	uint32_t partial_size;                                         /* 当前可消费的连续字节数。 */
	uint32_t total_size = 0U;                                      /* 累计本轮实际取得的数据量。 */
	int err;                                                       /* 保存将读位置正式前移的结果。 */

	/* Shell 后端可以只请求一个字节，其他调用者可一次取得跨界的多字节数据。 */
	do {                                                           /* 请求可能横跨数组末端，分段消费但统一提交。 */
		partial_size = ring_buf_get_claim(buf, &src, size);    /* 从临时读端取得本次可连续处理的那一段。 */
		if (data) {                                            /* 调用者给出接收缓冲才需要复制，传空也会消费。 */
			memcpy(data, src, partial_size);               /* 将已经发布的连续数据拷贝到外部，例如 Shell 的输入字节。 */
			data += partial_size;                          /* 更新外部目标指针，以便跨尾后接着写。 */
		}
		total_size += partial_size;                            /* 累加本轮已处理的字节数。 */
		size -= partial_size;                                  /* 缩小下一次申请的剩余数量。 */
	} while (size && partial_size);                                /* 请求已满足或再无数据时结束。 */

	/* 拷贝或丢弃完成后统一释放这些环内位置。 */
	err = ring_buf_get_finish(buf, total_size);                    /* 到此才更新正式 head，允许生产者复用这些位置。 */
	__ASSERT_NO_MSG(err == 0);                                     /* 验证消费量与此前取得的数据相容。 */

	return total_size;                                             /* 将实际消费量交给调用方作为有效数据长度。 */
}

/**
  * @brief  复制已发布的队首数据，保留原队首供后续读取。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 非空，指向至少 size 字节的可写缓冲。
  * @param  size [输入] 最多要查看的字节数。
  * @retval 实际复制的字节数，可小于请求值。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 多次查看可读到同一数据；要消费可用 @ref ring_buf_get 或非零长度的 @ref ring_buf_get_finish。
  */
uint32_t ring_buf_peek(struct ring_buf *buf, uint8_t *data, uint32_t size)
{
	uint8_t *src;                                                  /* 指向供查看的当前连续段。 */
	uint32_t partial_size;                                         /* 本段实际可读字节数。 */
	uint32_t total_size = 0U;                                      /* 记录已经复制进调用者缓冲的数量。 */
	int err;                                                       /* 保存撤销预取的结果。 */

	size = MIN(size, ring_buf_size_get(buf));                      /* 把查看量限制在当前已发布数据以内。 */

	/* 临时读游标与普通 get 相同，但不确认实际消费。 */
	do {                                                           /* 分段拷贝以便在数组末端回绕。 */
		partial_size = ring_buf_get_claim(buf, &src, size);    /* 取得一段数据并仅推进临时读端。 */
		__ASSERT_NO_MSG(data != NULL);                         /* 查看需要有效目标来保存副本。 */
		memcpy(data, src, partial_size);                       /* 复制当前连续段。 */
		data += partial_size;                                  /* 调整外部写端以接续下段。 */
		total_size += partial_size;                            /* 累计已经成功查看的总数。 */
		size -= partial_size;                                  /* 减去本段已查看长度。 */
	} while (size && partial_size);                                /* 请求满足或不再取得新数据时停止。 */

	/* 对刚才查看的各段撤销预取，以便下一次仍可从相同的队首读取。 */
	err = ring_buf_get_finish(buf, 0);                             /* 用零消费量回收临时预取位置，正式队首保持不动。 */
	__ASSERT_NO_MSG(err == 0);                                     /* 核验取消预取的操作成功。 */

	return total_size;                                             /* 返回复制出的字节数，队列仍保有这些数据。 */
}
