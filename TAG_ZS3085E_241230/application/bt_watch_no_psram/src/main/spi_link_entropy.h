/* Leopard 芯片的 SE 硬件随机源，提供 TLS 种子和 P4 签名随机值。沿用 SDK 的振荡器与移位寄存器组合，并为就绪位设置等待上限。 */
#ifndef ATS_SPI_LINK_ENTROPY_H
#define ATS_SPI_LINK_ENTROPY_H

#include <drivers/se/se.h>                    /* 提供 SE/TRNG 的寄存器定义和共享互斥量接口。 */

#define SFILE_TRNG_TIMEOUT_US 2000u           /* 单次关中断采样等待就绪的上限，单位微秒。 */
#ifdef CONFIG_SE
#define SFILE_SE_LOCK se_lock                 /* 配有 SE 驱动时与其他 SE 使用者共用一把锁。 */
#else
/* 未配置 SE 公用驱动时，使用此固件内部的独立互斥量串行化采样。 */
static K_MUTEX_DEFINE(sfile_entropy_lock);    /* 未编入 SE 驱动的固件仍需一把静态互斥保护采样现场。 */
#define SFILE_SE_LOCK sfile_entropy_lock
#endif

/**
  * @brief  从片上安全引擎取一组 64 位硬件随机样本。
  * @param  low：[输出]低 32 位的非空地址。
  * @param  high：[输出]高 32 位的非空地址。
  * @retval 0 成功，-EBUSY 为 TRNG 已在用，-ETIMEDOUT 为就绪位超时。
  * @note   调用方持 SE 互斥锁。本次采样关中断且限制最长轮询，退出时恢复进入前的中断状态。
  */
static int sfile_entropy_sample(uint32_t *low, uint32_t *high)
{
    uint32_t start, control;                                                  /* 起点使用硬件周期计数，control 保存分两步写入的控制字。 */
    unsigned int key = irq_lock();                                            /* 保存进入时的中断屏蔽状态，退出时原样恢复。 */
    bool clock_owned = !(sys_read32(CMU_DEVCLKEN0) & (1u << CLOCK_ID_SE));    /* 原时钟关闭时，本次代码负责开启并在退出时关闭。 */
    int ret = 0;                                                              /* 默认成功，等待就绪超时单独记录。 */

    /* AES/CRC 可共享 SE 锁；旧式 TRNG 访问不一定取锁，因此单次采样暂避抢占。若安全引擎已开钟，只在 TRNG 空闲时使用，不复位其他用户。 */
    if (clock_owned) {
        sys_write32(0, CMU_SECCLK);                                           /* 只有本次取得时钟所有权时才改安全引擎时钟选择。 */
        acts_clock_peripheral_enable(CLOCK_ID_SE);                            /* 为本次独占采样打开 SE 外设时钟。 */
        acts_reset_peripheral(RESET_ID_SE);                                   /* 原先无人开钟使用时才复位该引擎。 */
    } else if (sys_read32(TRNG_CTRL) & ((1u << 3) | 1u)) {                    /* 对已有时钟的引擎先检查 TRNG 是否正在工作。 */
        irq_unlock(key);                                                      /* 发现他处正在使用时立即恢复原中断状态。 */
        return -EBUSY;                                                        /* 不打断已经在运行的 TRNG。 */
    }

    /* 按照 SDK 配置先写控制字再触发启动，使用系统周期计数给硬件等待设上限。 */
    control = (1u << 3) | (1u << 8) | (1u << 9) | (1u << 10) | (2u << 6);
    sys_write32(control, TRNG_CTRL);                                          /* 先写配置，保留启动位为零。 */
    sys_write32(control | 1u, TRNG_CTRL);                                     /* 置位启动，开始生成本组随机数。 */
    start = k_cycle_get_32();                                                 /* 记录启动后的计时起点。 */
    while (!(sys_read32(TRNG_CTRL) & (1u << 31))) {                           /* 轮询第 31 位的样本就绪状态。 */
        if (k_cyc_to_us_floor32((uint32_t)(k_cycle_get_32() - start)) >=
            SFILE_TRNG_TIMEOUT_US) {
            ret = -ETIMEDOUT;                                                 /* 到达微秒限额时跳出轮询，走共用的关停路径。 */
            break;                                                            /* 停止轮询，统一执行关闭采样及恢复动作。 */
        }
    }

    /* 就绪才取数据；成功和超时均需关闭本次启用的功能并恢复中断。 */
    if (!ret) {
        *low = sys_read32(TRNG_LR);                                           /* 就绪后读取低 32 位实际样本。 */
        *high = sys_read32(TRNG_MR);                                          /* 同次采样的高 32 位。 */
    }
    sys_write32(0, TRNG_CTRL);                                                /* 关闭本次 TRNG 操作，在超时路径也执行。 */
    if (clock_owned) acts_clock_peripheral_disable(CLOCK_ID_SE);              /* 对进入函数前已开启的 SE 时钟不作关闭。 */
    irq_unlock(key);                                                          /* 恢复进入本次短采样前保存的中断状态。 */
    return ret;                                                               /* 就绪时返回零，遇到采样时限则报告超时。 */
}

/**
  * @brief  按八字节一组采集 TLS 种子或签名随机数。
  * @param  out：[输出]至少 length 字节的非空目标。
  * @param  length：[输入]正的八字节倍数。
  * @retval 0 成功，-EINVAL 为参数错误，-EBUSY 为资源占用，-ETIMEDOUT 或 -EIO 为样本异常。
  * @note   互斥保护硬件和前一次样本；读取中途失败会擦除整个目标。
  */
static int sfile_entropy_fill(uint8_t *out, size_t length)
{
    static uint32_t previous_low, previous_high;                                   /* 上一组 64 位样本跨调用保存，供连续重复检测。 */
    static bool previous_valid;                                                    /* 首次采样没有历史值，第二组起才做连续重复检测。 */
    size_t offset;                                                                 /* 已装入输出区的字节偏移。 */
    int ret;                                                                       /* 保存互斥和单组硬件采样状态。 */

    /* 检查输出区并即时获取 SE 互斥，失败时不等待其他用户释放引擎。 */
    if (!out || !length || (length & 7u)) return -EINVAL;
    memset(out, 0, length);                                                        /* 获取硬件锁前先将目标清零，互斥失败也不会留下旧数据。 */
    ret = k_mutex_lock(&SFILE_SE_LOCK, K_NO_WAIT);                                 /* 以不等待方式取锁，防止请求准备卡在其他 SE 用户处。 */
    if (ret) return -EBUSY;                                                        /* 无法立即获得 SE 的使用权即返回忙。 */
    for (offset = 0; offset < length; offset += 8u) {                              /* 以一组 64 位为单位填满指定缓冲。 */
        uint32_t low = 0, high = 0;                                                /* 分开暂存硬件的低、高 32 位，在确认成功前不写输出。 */
        unsigned int i;                                                            /* 将每个硬件字拆成四字节时的索引。 */
        ret = sfile_entropy_sample(&low, &high);                                   /* 每八字节重新执行一次有限时的硬件采样。 */
        if (ret) break;                                                            /* 此次样本无效，退出后清除之前已写的部分。 */
        /* 识别全零、全一及连续相同的 64 位样本，仅用于发现随机源卡死。 */
        if ((!low && !high) || (low == UINT32_MAX && high == UINT32_MAX) ||
            (previous_valid && low == previous_low && high == previous_high)) {    /* 相邻两次低位和高位全部相同即视作可疑卡死。 */
            ret = -EIO;                                                            /* 样本表现为卡死，按 I/O 故障向 TLS 或签名层报告。 */
            break;                                                                 /* 放弃后续样本，统一释放 SE 锁。 */
        }

        /* 记住本次样本作下次连续性检查，并分解为八个可直接使用的字节。 */
        previous_low = low;
        previous_high = high;                                                      /* 将高字也保存为下一次比较基准。 */
        previous_valid = true;                                                     /* 记住本组上下半字供下一组对照。 */
        for (i = 0; i < 4u; ++i) {                                                 /* 低、高两字分别从低有效字节起解包。 */
            out[offset + i] = (uint8_t)(low >> (8u * i));                          /* 把低寄存器从低有效字节起写入前四字节。 */
            out[offset + 4u + i] = (uint8_t)(high >> (8u * i));                    /* 把高寄存器按相同顺序写入后四字节。 */
        }
    }
    k_mutex_unlock(&SFILE_SE_LOCK);                                                /* 采样状态已更新，释放引擎互斥。 */
    if (ret) {                                                                     /* 只要有一次采样失败，之前的部分随机值就不能使用。 */
        volatile uint8_t *p = out;                                                 /* 以 volatile 指针执行擦除，保留实际的逐字节写操作。 */
        for (offset = 0; offset < length; ++offset) p[offset] = 0;                 /* 任一组失败时逐字节擦除本次全部输出。 */
    }
    return ret;                                                                    /* 返回所有样本的最终状态。 */
}

#endif
