/*
 * Copyright (c) 2020 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Actions 安全引擎寄存器与接口。SPI 文件传输使用 TRNG_CTRL、TRNG_LR、TRNG_MR 取得两段 32 位随机样本。
 */

#ifndef SOC_SE_H_    /* 避免不同安全引擎用户同时包含时重复定义。 */
#define SOC_SE_H_

//#include <device.h>
//#include <drivers/dma.h>
#include <soc.h>    /* 引入当前 SoC 的安全引擎基址和整数类型。 */

/* AES 寄存器地址，相对安全引擎基址。 */

#define AES_CTRL          (SE_REG_BASE+0x0000)    /* AES 的控制字。 */
#define AES_MODE          (SE_REG_BASE+0x0004)    /* AES 的工作模式配置。 */
#define AES_LEN           (SE_REG_BASE+0x0008)    /* AES 的本次数据长度。 */
#define AES_KEY0          (SE_REG_BASE+0x0010)    /* AES 的密钥寄存器字 0。 */
#define AES_KEY1          (SE_REG_BASE+0x0014)    /* AES 的密钥寄存器字 1。 */
#define AES_KEY2          (SE_REG_BASE+0x0018)    /* AES 的密钥寄存器字 2。 */
#define AES_KEY3          (SE_REG_BASE+0x001c)    /* AES 的密钥寄存器字 3。 */
#define AES_KEY4          (SE_REG_BASE+0x0020)    /* AES 的密钥寄存器字 4。 */
#define AES_KEY5          (SE_REG_BASE+0x0024)    /* AES 的密钥寄存器字 5。 */
#define AES_KEY6          (SE_REG_BASE+0x0028)    /* AES 的密钥寄存器字 6。 */
#define AES_KEY7          (SE_REG_BASE+0x002c)    /* AES 的密钥寄存器字 7。 */
#define AES_IV0           (SE_REG_BASE+0x0040)    /* AES 的初始向量寄存器字 0。 */
#define AES_IV1           (SE_REG_BASE+0x0044)    /* AES 的初始向量寄存器字 1。 */
#define AES_IV2           (SE_REG_BASE+0x0048)    /* AES 的初始向量寄存器字 2。 */
#define AES_IV3           (SE_REG_BASE+0x004c)    /* AES 的初始向量寄存器字 3。 */
#define AES_INOUTFIFOCTL  (SE_REG_BASE+0x0080)    /* AES 的输入和输出 FIFO 控制。 */
#define AES_INFIFO        (SE_REG_BASE+0x0084)    /* AES 的输入数据队列。 */
#define AES_OUTFIFO       (SE_REG_BASE+0x0088)    /* AES 的输出数据队列。 */

/* SHA 寄存器地址，相对安全引擎基址。 */

#define SHA_CTRL          (SE_REG_BASE+0x0200)    /* SHA 的控制字。 */
#define SHA_MODE          (SE_REG_BASE+0x0204)    /* SHA 的工作模式配置。 */
#define SHA_LEN           (SE_REG_BASE+0x0208)    /* SHA 的本次数据长度。 */
#define SHA_INFIFOCTL     (SE_REG_BASE+0x020c)    /* SHA 的输入 FIFO 控制。 */
#define SHA_INFIFO        (SE_REG_BASE+0x0210)    /* SHA 的输入数据队列。 */
#define SHA_DATAOUT       (SE_REG_BASE+0x0214)    /* SHA 的计算结果。 */
#define SHA_TOTAL_LEN     (SE_REG_BASE+0x0218)    /* SHA 的累计数据长度。 */

/* TRNG 寄存器地址，相对安全引擎基址。 */

#define TRNG_CTRL         (SE_REG_BASE+0x0400)    /* 控制片内随机源的时钟、采样模式、启动位和就绪标志。 */
#define TRNG_LR           (SE_REG_BASE+0x0404)    /* 就绪后读取本次随机样本的低 32 位。 */
#define TRNG_MR           (SE_REG_BASE+0x0408)    /* 就绪后读取同一次样本的高 32 位。 */
#define TRNG_ILR          (SE_REG_BASE+0x040c)    /* 随机源 ILR 寄存器；当前 SPI 采样路径未读取。 */
#define TRNG_IMR          (SE_REG_BASE+0x0410)    /* 随机源 IMR 寄存器；当前 SPI 采样路径未读取。 */

/* CRC 寄存器地址，相对安全引擎基址。 */

#define CRC_CTRL          (SE_REG_BASE+0x0600)    /* CRC 的控制字。 */
#define CRC_MODE          (SE_REG_BASE+0x0604)    /* CRC 的工作模式配置。 */
#define CRC_LEN           (SE_REG_BASE+0x0608)    /* CRC 的本次数据长度。 */
#define CRC_INFIFOCTL     (SE_REG_BASE+0x060c)    /* CRC 的输入 FIFO 控制。 */
#define CRC_INFIFO        (SE_REG_BASE+0x0610)    /* CRC 的输入数据队列。 */
#define CRC_DATAOUT       (SE_REG_BASE+0x0614)    /* CRC 的计算结果。 */
#define CRC_DATAINIT      (SE_REG_BASE+0x0618)    /* CRC 的CRC 初值。 */
#define CRC_DATAOUTXOR    (SE_REG_BASE+0x061c)    /* CRC 的结果异或设置。 */
#define CRC_DEBUGOUT      (SE_REG_BASE+0x0630)    /* CRC 的调试输出。 */

/* 安全引擎 FIFO 选择或访问控制寄存器。 */

#define SE_FIFOCTRL       (SE_REG_BASE+0x0800)    /* SE 的共享 FIFO 所属模块。 */
#define SE_FIFOCTRL_AES   (0)                     /* SE 的选择 AES 使用共享 FIFO。 */
#define SE_FIFOCTRL_CRC   (0x1)                   /* SE 的选择 CRC 使用共享 FIFO。 */
#define SE_FIFOCTRL_SHA   (0x2)                   /* SE 的选择 SHA 使用共享 FIFO。 */

/* AES 寄存器位定义，以下掩码供读改写保留其他字段。 */

/* 控制寄存器的启动、时钟及状态字段。 */


#define AES_CTRL_EN_SHIFT           (0)                                /* AES 模块使能字段最低位的位置。 */
#define AES_CTRL_EN_MASK            (0x1 << AES_CTRL_EN_SHIFT)         /* AES 模块使能字段的取位掩码。 */
#define AES_CTRL_EN                 (0x1 << AES_CTRL_EN_SHIFT)         /* AES 模块使能所用的寄存器编码。 */
#define AES_CTRL_RESET_SHIFT        (1)                                /* AES 模块复位字段最低位的位置。 */
#define AES_CTRL_RESET_MASK         (0x1 << AES_CTRL_RESET_SHIFT)      /* AES 模块复位字段的取位掩码。 */
#define AES_CTRL_RESET              (0x1 << AES_CTRL_RESET_SHIFT)      /* AES 模块复位所用的寄存器编码。 */
#define AES_CTRL_INT_EN_SHIFT       (2)                                /* AES 中断使能字段最低位的位置。 */
#define AES_CTRL_INT_EN_MASK        (0x1 << AES_CTRL_INT_EN_SHIFT)     /* AES 中断使能字段的取位掩码。 */
#define AES_CTRL_INT_EN             (0x1 << AES_CTRL_INT_EN_SHIFT)     /* AES 中断使能所用的寄存器编码。 */
#define AES_CTRL_CLK_EN_SHIFT       (3)                                /* AES 内部时钟使能字段最低位的位置。 */
#define AES_CTRL_CLK_EN_MASK        (0x1 << AES_CTRL_CLK_EN_SHIFT)     /* AES 内部时钟使能字段的取位掩码。 */
#define AES_CTRL_CLK_EN             (0x1 << AES_CTRL_CLK_EN_SHIFT)     /* AES 内部时钟使能所用的寄存器编码。 */
#define AES_CTRL_CNT_OVF_SHIFT      (30)                               /* AES 计数溢出字段最低位的位置。 */
#define AES_CTRL_CNT_OVF_MASK       (0x1 << AES_CTRL_CNT_OVF_SHIFT)    /* AES 计数溢出字段的取位掩码。 */
#define AES_CTRL_CNT_OVF            (0x1 << AES_CTRL_CNT_OVF_SHIFT)    /* AES 计数溢出所用的寄存器编码。 */
#define AES_CTRL_END_SHIFT          (31)                               /* AES 计算结束字段最低位的位置。 */
#define AES_CTRL_END_MASK           (0x1 << AES_CTRL_END_SHIFT)        /* AES 计算结束字段的取位掩码。 */
#define AES_CTRL_END                (0x1 << AES_CTRL_END_SHIFT)        /* AES 计算结束所用的寄存器编码。 */

/* 模式寄存器中的工作模式选项。 */

#define AES_MODE_TYP_SHIFT          (0)                                 /* AES 加解密选择字段最低位的位置。 */
#define AES_MODE_TYP_MASK           (0x1 << AES_MODE_TYP_SHIFT)         /* AES 加解密选择字段的取位掩码。 */
#define AES_MODE_DECRYPT            (0x0 << AES_MODE_TYP_SHIFT)         /* AES 解密模式所用的寄存器编码。 */
#define AES_MODE_ENCRYPT            (0x1 << AES_MODE_TYP_SHIFT)         /* AES 加密模式所用的寄存器编码。 */
#define AES_MODE_MOD_SHIFT          (2)                                 /* AES 工作模式字段最低位的位置。 */
#define AES_MODE_MOD_MASK           (0x3 << AES_MODE_MOD_SHIFT)         /* AES 工作模式字段的取位掩码。 */
#define AES_MODE_MOD_ECB            (0x0 << AES_MODE_MOD_SHIFT)         /* AES 选择 ECB 模式所用的寄存器编码。 */
#define AES_MODE_MOD_CTR            (0x1 << AES_MODE_MOD_SHIFT)         /* AES 选择 CTR 模式所用的寄存器编码。 */
#define AES_MODE_MOD_CBC            (0x2 << AES_MODE_MOD_SHIFT)         /* AES 选择 CBC 模式所用的寄存器编码。 */
#define AES_MODE_MOD_CBC_CTS        (0x3 << AES_MODE_MOD_SHIFT)         /* AES 选择 CBC_CTS 模式所用的寄存器编码。 */
#define AES_MODE_BYPASS_SHIFT       (4)                                 /* AES 旁路控制字段最低位的位置。 */
#define AES_MODE_BYPASS_MASK        (0x1 << AES_MODE_BYPASS_SHIFT)      /* AES 旁路控制字段的取位掩码。 */
#define AES_MODE_BYPASS_DECRYPT     (0x1 << AES_MODE_BYPASS_SHIFT)      /* AES 解密旁路所用的寄存器编码。 */
#define AES_MODE_IV_UPDAT_SHIFT     (8)                                 /* AES 初始向量更新字段最低位的位置。 */
#define AES_MODE_IV_UPDAT_MASK      (0x1 << AES_MODE_IV_UPDAT_SHIFT)    /* AES 初始向量更新字段的取位掩码。 */
#define AES_MODE_IV_UPDAT           (0x1 << AES_MODE_IV_UPDAT_SHIFT)    /* AES 初始向量更新所用的寄存器编码。 */
#define AES_MODE_KEY_SRC_SHIFT      (16)                                /* AES 密钥来源字段最低位的位置。 */
#define AES_MODE_KEY_SRC_MASK       (0x1 << AES_MODE_KEY_SRC_SHIFT)     /* AES 密钥来源字段的取位掩码。 */
#define AES_MODE_REG_KEY            (0x1 << AES_MODE_KEY_SRC_SHIFT)     /* AES 使用寄存器提供的密钥所用的寄存器编码。 */
#define AES_MODE_KEY_SIZE_SHIFT     (17)                                /* AES 密钥宽度选择字段最低位的位置。 */
#define AES_MODE_KEY_SIZE_MASK      (0x1 << AES_MODE_KEY_SIZE_SHIFT)    /* AES 密钥宽度选择字段的取位掩码。 */
#define AES_MODE_KEY_128BIT         (0x0 << AES_MODE_KEY_SIZE_SHIFT)    /* AES 选择 128BIT 模式所用的寄存器编码。 */
#define AES_MODE_KEY_192BIT         (0x1 << AES_MODE_KEY_SIZE_SHIFT)    /* AES 选择 192BIT 模式所用的寄存器编码。 */
#define AES_MODE_KEY_256BIT         (0x2 << AES_MODE_KEY_SIZE_SHIFT)    /* AES 选择 256BIT 模式所用的寄存器编码。 */

/* 安全引擎 FIFO 选择或访问控制寄存器。 */

#define AES_FIFOCTL_INFIFODE_SHIFT  (1)                                     /* AES 输入 FIFO 的 DMA 请求控制字段最低位的位置。 */
#define AES_FIFOCTL_INFIFODE_MASK   (0x1 << AES_FIFOCTL_INFIFODE_SHIFT)     /* AES 输入 FIFO 的 DMA 请求控制字段的取位掩码。 */
#define AES_FIFOCTL_INFIFODE        (0x1 << AES_FIFOCTL_INFIFODE_SHIFT)     /* AES 输入 FIFO 的 DMA 请求控制所用的寄存器编码。 */
#define AES_FIFOCTL_OUTFIFODE_SHIFT (5)                                     /* AES 输出 FIFO 的 DMA 请求控制字段最低位的位置。 */
#define AES_FIFOCTL_OUTFIFODE_MASK  (0x1 << AES_FIFOCTL_OUTFIFODE_SHIFT)    /* AES 输出 FIFO 的 DMA 请求控制字段的取位掩码。 */
#define AES_FIFOCTL_OUTFIFODE       (0x1 << AES_FIFOCTL_OUTFIFODE_SHIFT)    /* AES 输出 FIFO 的 DMA 请求控制所用的寄存器编码。 */
#define AES_FIFOCTL_OUTFIFOFP_SHIFT (9)                                     /* AES 输出 FIFO 的 FP 控制字段最低位的位置。 */
#define AES_FIFOCTL_OUTFIFOFP_MASK  (0x1 << AES_FIFOCTL_OUTFIFOFP_SHIFT)    /* AES 输出 FIFO 的 FP 控制字段的取位掩码。 */
#define AES_FIFOCTL_OUTFIFOFP       (0x1 << AES_FIFOCTL_OUTFIFOFP_SHIFT)    /* AES 输出 FIFO 的 FP 控制所用的寄存器编码。 */
#define AES_FIFOCTL_OUTFIFOLP_SHIFT (10)                                    /* AES 输出 FIFO 的 LP 控制字段最低位的位置。 */
#define AES_FIFOCTL_OUTFIFOLP_MASK  (0x1 << AES_FIFOCTL_OUTFIFOLP_SHIFT)    /* AES 输出 FIFO 的 LP 控制字段的取位掩码。 */
#define AES_FIFOCTL_OUTFIFOLP       (0x1 << AES_FIFOCTL_OUTFIFOLP_SHIFT)    /* AES 输出 FIFO 的 LP 控制所用的寄存器编码。 */

/* TRNG 寄存器位定义，以下掩码供读改写保留其他字段。 */

/* 控制寄存器的启动、时钟及状态字段。 */

#define TRNG_CTRL_EN_SHIFT          (0)                                     /* TRNG 模块使能字段最低位的位置。 */
#define TRNG_CTRL_EN_MASK           (0x1 << TRNG_CTRL_EN_SHIFT)             /* TRNG 模块使能字段的取位掩码。 */
#define TRNG_CTRL_EN                (0x1 << TRNG_CTRL_EN_SHIFT)             /* TRNG 模块使能所用的寄存器编码。 */
#define TRNG_CTRL_MODE_SHIFT        (1)                                     /* TRNG 随机源组合方式字段最低位的位置。 */
#define TRNG_CTRL_MODE_MASK         (0x3 << TRNG_CTRL_MODE_SHIFT)           /* TRNG 随机源组合方式字段的取位掩码。 */
#define TRNG_CTRL_MODE_OSC_LFSR     (0x0 << TRNG_CTRL_MODE_SHIFT)           /* TRNG 振荡器与线性反馈移位寄存器组合所用的寄存器编码。 */
#define TRNG_CTRL_MODE_OSC          (0x1 << TRNG_CTRL_MODE_SHIFT)           /* TRNG 振荡器模式所用的寄存器编码。 */
#define TRNG_CTRL_MODE_LFSR         (0x2 << TRNG_CTRL_MODE_SHIFT)           /* TRNG 线性反馈移位寄存器模式所用的寄存器编码。 */
#define TRNG_CTRL_CLK_EN_SHIFT      (3)                                     /* TRNG 内部时钟使能字段最低位的位置。 */
#define TRNG_CTRL_CLK_EN_MASK       (0x1 << TRNG_CTRL_CLK_EN_SHIFT)         /* TRNG 内部时钟使能字段的取位掩码。 */
#define TRNG_CTRL_CLK_EN            (0x1 << TRNG_CTRL_CLK_EN_SHIFT)         /* TRNG 内部时钟使能所用的寄存器编码。 */
#define TRNG_CTRL_OSAMPLE_MOD_SHIFT (6)                                     /* TRNG 振荡器采样模式字段最低位的位置。 */
#define TRNG_CTRL_OSAMPLE_MOD_MASK  (0x3 << TRNG_CTRL_OSAMPLE_MOD_SHIFT)    /* TRNG 振荡器采样模式字段的取位掩码。 */
#define TRNG_CTRL_OSAMPLE_HIG_LOW   (0x0 << TRNG_CTRL_OSAMPLE_MOD_SHIFT)    /* TRNG 采样高、低电平所用的寄存器编码。 */
#define TRNG_CTRL_OSAMPLE_HIG       (0x1 << TRNG_CTRL_OSAMPLE_MOD_SHIFT)    /* TRNG 高电平采样所用的寄存器编码。 */
#define TRNG_CTRL_OSAMPLE_LOW       (0x2 << TRNG_CTRL_OSAMPLE_MOD_SHIFT)    /* TRNG 低电平采样所用的寄存器编码。 */
#define TRNG_CTRL_OPOST_EN_SHIFT    (8)                                     /* TRNG 振荡器后处理使能字段最低位的位置。 */
#define TRNG_CTRL_OPOST_EN_MASK     (0x1 << TRNG_CTRL_OPOST_EN_SHIFT)       /* TRNG 振荡器后处理使能字段的取位掩码。 */
#define TRNG_CTRL_OPOST_EN          (0x1 << TRNG_CTRL_OPOST_EN_SHIFT)       /* TRNG 振荡器后处理使能所用的寄存器编码。 */
#define TRNG_CTRL_OXOR_MODE_SHIFT   (9)                                     /* TRNG 振荡器异或模式字段最低位的位置。 */
#define TRNG_CTRL_OXOR_MODE_MASK    (0x1 << TRNG_CTRL_OXOR_MODE_SHIFT)      /* TRNG 振荡器异或模式字段的取位掩码。 */
#define TRNG_CTRL_OXOR_MODE         (0x1 << TRNG_CTRL_OXOR_MODE_SHIFT)      /* TRNG 振荡器异或模式所用的寄存器编码。 */
#define TRNG_CTRL_SMPCLK_MODE_SHIFT (10)                                    /* TRNG 采样时钟选择字段最低位的位置。 */
#define TRNG_CTRL_SMPCLK_MODE_MASK  (0x1 << TRNG_CTRL_SMPCLK_MODE_SHIFT)    /* TRNG 采样时钟选择字段的取位掩码。 */
#define TRNG_CTRL_SMP_SE_CLK        (0x1 << TRNG_CTRL_SMPCLK_MODE_SHIFT)    /* TRNG 选取安全引擎时钟作为采样时钟所用的寄存器编码。 */
#define TRNG_CTRL_READY_SHIFT       (31)                                    /* TRNG 随机样本就绪字段最低位的位置。 */
#define TRNG_CTRL_READY_MASK        (0x1 << TRNG_CTRL_READY_SHIFT)          /* TRNG 随机样本就绪字段的取位掩码。 */
#define TRNG_CTRL_READY             (0x1 << TRNG_CTRL_READY_SHIFT)          /* TRNG 随机样本就绪所用的寄存器编码。 */

/* CRC 寄存器位定义，以下掩码供读改写保留其他字段。 */

/* 控制寄存器的启动、时钟及状态字段。 */

#define CRC_CTRL_EN_SHIFT           (0)                               /* CRC 模块使能字段最低位的位置。 */
#define CRC_CTRL_EN_MASK            (0x1 << CRC_CTRL_EN_SHIFT)        /* CRC 模块使能字段的取位掩码。 */
#define CRC_CTRL_EN                 (0x1 << CRC_CTRL_EN_SHIFT)        /* CRC 模块使能所用的寄存器编码。 */
#define CRC_CTRL_RESET_SHIFT        (1)                               /* CRC 模块复位字段最低位的位置。 */
#define CRC_CTRL_RESET_MASK         (0x1 << CRC_CTRL_RESET_SHIFT)     /* CRC 模块复位字段的取位掩码。 */
#define CRC_CTRL_RESET              (0x1 << CRC_CTRL_RESET_SHIFT)     /* CRC 模块复位所用的寄存器编码。 */
#define CRC_CTRL_INT_EN_SHIFT       (2)                               /* CRC 中断使能字段最低位的位置。 */
#define CRC_CTRL_INT_EN_MASK        (0x1 << CRC_CTRL_INT_EN_SHIFT)    /* CRC 中断使能字段的取位掩码。 */
#define CRC_CTRL_INT_EN             (0x1 << CRC_CTRL_INT_EN_SHIFT)    /* CRC 中断使能所用的寄存器编码。 */
#define CRC_CTRL_CLK_EN_SHIFT       (3)                               /* CRC 内部时钟使能字段最低位的位置。 */
#define CRC_CTRL_CLK_EN_MASK        (0x1 << CRC_CTRL_CLK_EN_SHIFT)    /* CRC 内部时钟使能字段的取位掩码。 */
#define CRC_CTRL_CLK_EN             (0x1 << CRC_CTRL_CLK_EN_SHIFT)    /* CRC 内部时钟使能所用的寄存器编码。 */
#define CRC_CTRL_END_SHIFT          (31)                              /* CRC 计算结束字段最低位的位置。 */
#define CRC_CTRL_END_MASK           (0x1 << CRC_CTRL_END_SHIFT)       /* CRC 计算结束字段的取位掩码。 */
#define CRC_CTRL_END                (0x1 << CRC_CTRL_END_SHIFT)       /* CRC 计算结束所用的寄存器编码。 */

/* 模式寄存器中的工作模式选项。 */

#define CRC_MODE_TYPE_SHIFT         (0)                                     /* CRC 算法类型字段最低位的位置。 */
#define CRC_MODE_TYPE_MASK          (0x1 << CRC_MODE_TYPE_SHIFT)            /* CRC 算法类型字段的取位掩码。 */
#define CRC_MODE_TYPE_CRC32         (0x1 << CRC_MODE_TYPE_SHIFT)            /* CRC 选择 CRC32所用的寄存器编码。 */
#define CRC_MODE_CRC16_POLY_SHIFT   (4)                                     /* CRC CRC16 多项式选择字段最低位的位置。 */
#define CRC_MODE_CRC16_POLY_8005    (0x1 << CRC_MODE_CRC16_POLY_SHIFT)      /* CRC 使用 0x8005 多项式所用的寄存器编码。 */
#define CRC_MODE_IN_INV_SHIFT       (12)                                    /* CRC 输入反转字段最低位的位置。 */
#define CRC_MODE_IN_INV_TRUE        (0x1 << CRC_MODE_IN_INV_SHIFT)          /* CRC 启用输入反转所用的寄存器编码。 */
#define CRC_MODE_OUT_INV_SHIFT      (13)                                    /* CRC 输出反转字段最低位的位置。 */
#define CRC_MODE_OUT_INV_TRUE       (0x1 << CRC_MODE_OUT_INV_SHIFT)         /* CRC 启用输出反转所用的寄存器编码。 */
#define CRC_MODE_INITDATA_UPD_SHIFT (17)                                    /* CRC 更新 CRC 初值字段最低位的位置。 */
#define CRC_MODE_INITDATA_UPD       (0x1 << CRC_MODE_INITDATA_UPD_SHIFT)    /* CRC 更新 CRC 初值所用的寄存器编码。 */

/* 安全引擎 FIFO 选择或访问控制寄存器。 */

#define CRC_INFIFOCTL_DE_SHIFT      (1)                                /* CRC 输入 FIFO 的 DMA 请求控制字段最低位的位置。 */
#define CRC_INFIFOCTL_DE_MASK       (0x1 << CRC_INFIFOCTL_DE_SHIFT)    /* CRC 输入 FIFO 的 DMA 请求控制字段的取位掩码。 */
#define CRC_INFIFOCTL_DE            (0x1 << CRC_INFIFOCTL_DE_SHIFT)    /* CRC 输入 FIFO 的 DMA 请求控制所用的寄存器编码。 */

/* SHA 寄存器位定义，以下掩码供读改写保留其他字段。 */

/* 控制寄存器的启动、时钟及状态字段。 */

#define SHA_CTRL_EN_SHIFT           (0)                                /* SHA 模块使能字段最低位的位置。 */
#define SHA_CTRL_EN_MASK            (0x1 << SHA_CTRL_EN_SHIFT)         /* SHA 模块使能字段的取位掩码。 */
#define SHA_CTRL_EN                 (0x1 << SHA_CTRL_EN_SHIFT)         /* SHA 模块使能所用的寄存器编码。 */
#define SHA_CTRL_RESET_SHIFT        (1)                                /* SHA 模块复位字段最低位的位置。 */
#define SHA_CTRL_RESET_MASK         (0x1 << SHA_CTRL_RESET_SHIFT)      /* SHA 模块复位字段的取位掩码。 */
#define SHA_CTRL_RESET              (0x1 << SHA_CTRL_RESET_SHIFT)      /* SHA 模块复位所用的寄存器编码。 */
#define SHA_CTRL_INT_EN_SHIFT       (2)                                /* SHA 中断使能字段最低位的位置。 */
#define SHA_CTRL_INT_EN_MASK        (0x1 << SHA_CTRL_INT_EN_SHIFT)     /* SHA 中断使能字段的取位掩码。 */
#define SHA_CTRL_INT_EN             (0x1 << SHA_CTRL_INT_EN_SHIFT)     /* SHA 中断使能所用的寄存器编码。 */
#define SHA_CTRL_CLK_EN_SHIFT       (3)                                /* SHA 内部时钟使能字段最低位的位置。 */
#define SHA_CTRL_CLK_EN_MASK        (0x1 << SHA_CTRL_CLK_EN_SHIFT)     /* SHA 内部时钟使能字段的取位掩码。 */
#define SHA_CTRL_CLK_EN             (0x1 << SHA_CTRL_CLK_EN_SHIFT)     /* SHA 内部时钟使能所用的寄存器编码。 */
#define SHA_CTRL_GAT_EN_SHIFT       (4)                                /* SHA 时钟门控使能字段最低位的位置。 */
#define SHA_CTRL_GAT_EN_MASK        (0x1 << SHA_CTRL_GAT_EN_SHIFT)     /* SHA 时钟门控使能字段的取位掩码。 */
#define SHA_CTRL_GAT_EN             (0x1 << SHA_CTRL_GAT_EN_SHIFT)     /* SHA 时钟门控使能所用的寄存器编码。 */
#define SHA_CTRL_LEN_ERR_SHIFT      (30)                               /* SHA 输入长度错误字段最低位的位置。 */
#define SHA_CTRL_LEN_ERR_MASK       (0x1 << SHA_CTRL_LEN_ERR_SHIFT)    /* SHA 输入长度错误字段的取位掩码。 */
#define SHA_CTRL_LEN_ERR            (0x1 << SHA_CTRL_LEN_ERR_SHIFT)    /* SHA 输入长度错误所用的寄存器编码。 */
#define SHA_CTRL_END_SHIFT          (31)                               /* SHA 计算结束字段最低位的位置。 */
#define SHA_CTRL_END_MASK           (0x1 << SHA_CTRL_END_SHIFT)        /* SHA 计算结束字段的取位掩码。 */
#define SHA_CTRL_END                (0x1 << SHA_CTRL_END_SHIFT)        /* SHA 计算结束所用的寄存器编码。 */

/* 模式寄存器中的工作模式选项。 */

#define SHA_MODE_TYPE_SHIFT         (0)                                /* SHA 算法类型字段最低位的位置。 */
#define SHA_MODE_TYPE_MASK          (0x3 << SHA_MODE_TYPE_SHIFT)       /* SHA 算法类型字段的取位掩码。 */
#define SHA_MODE_TYPE_1             (0x0 << SHA_MODE_TYPE_SHIFT)       /* SHA 选择 SHA-1所用的寄存器编码。 */
#define SHA_MODE_TYPE_256           (0x1 << SHA_MODE_TYPE_SHIFT)       /* SHA 选择 SHA-256所用的寄存器编码。 */
#define SHA_MODE_TYPE_224           (0x2 << SHA_MODE_TYPE_SHIFT)       /* SHA 选择 SHA-224所用的寄存器编码。 */
#define SHA_MODE_PADDING_SHIFT      (4)                                /* SHA 填充控制字段最低位的位置。 */
#define SHA_MODE_PADDING_MASK       (0x1 << SHA_MODE_PADDING_SHIFT)    /* SHA 填充控制字段的取位掩码。 */
#define SHA_MODE_PADDING            (0x1 << SHA_MODE_PADDING_SHIFT)    /* SHA 填充控制所用的寄存器编码。 */
#define SHA_MODE_FIRST_SHIFT        (5)                                /* SHA 首个计算块标志字段最低位的位置。 */
#define SHA_MODE_FIRST_MASK         (0x1 << SHA_MODE_FIRST_SHIFT)      /* SHA 首个计算块标志字段的取位掩码。 */
#define SHA_MODE_FIRST              (0x1 << SHA_MODE_FIRST_SHIFT)      /* SHA 首个计算块标志所用的寄存器编码。 */

/* 安全引擎 FIFO 选择或访问控制寄存器。 */

#define SHA_INFIFOCTL_DE_SHIFT      (1)                                /* SHA 输入 FIFO 的 DMA 请求控制字段最低位的位置。 */
#define SHA_INFIFOCTL_DE_MASK       (0x1 << SHA_INFIFOCTL_DE_SHIFT)    /* SHA 输入 FIFO 的 DMA 请求控制字段的取位掩码。 */
#define SHA_INFIFOCTL_DE            (0x1 << SHA_INFIFOCTL_DE_SHIFT)    /* SHA 输入 FIFO 的 DMA 请求控制所用的寄存器编码。 */


struct acts_se_data {
	void *dma_dev;            /* 安全引擎持有的 DMA 控制器设备引用。 */
	uint32_t dma_chan;        /* 输入数据使用的 DMA 通道。 */
	uint32_t dma_chan_out;    /* 输出通道仅由 AES 双向数据流使用。 */
};

#if 0
/**
  * @brief  声明保留的 AES 初始化入口。
  * @param  无。
  * @retval 此禁用声明返回整数状态，当前路径无实际调用。
  * @note   编译条件为 #if 0。
  */
int se_aes_init(void);
/**
  * @brief  声明保留的 AES 关闭入口。
  * @param  无。
  * @retval 整数状态，当前分支未构建。
  * @note   与保留的 AES 初始化配对。
  */
int se_aes_deinit(void);
/**
  * @brief  声明保留的 AES 数据处理入口。
  * @param  mode：[输入]AES 模式。
  * @param  in_buf：[输入]可读缓冲。
  * @param  in_len：[输入]输入字节数。
  * @param  out_buf：[输出]可写的结果缓冲。
  * @retval 本声明未给出具体状态码。
  * @note   禁用分支未约定空指针与具体输出容量。
  */
uint32_t se_aes_process(int mode, uint8_t *in_buf, uint32_t in_len, uint8_t *out_buf);
#endif

/**
  * @brief  初始化片内随机源，打开安全引擎和 TRNG 时钟。
  * @param  无。
  * @retval 当前 SoC 实现返回 0。
  * @note   此通用接口自行初始化安全引擎。SPI 种子采样另行管理占用及超时。
  */
int se_trng_init(void);
/**
  * @brief  关闭 TRNG 时钟并释放安全引擎时钟。
  * @param  无。
  * @retval 当前 SoC 实现返回 0。
  * @note   应与 se_trng_init 配对，避免影响其他安全引擎用户。
  */
int se_trng_deinit(void);
/**
  * @brief  启动随机源并等待一组 64 位随机样本就绪。
  * @param  trng_low：[输出]非空、可写的低 32 位结果地址。
  * @param  trng_high：[输出]非空、可写的高 32 位结果地址。
  * @retval 样本写出后返回 0。
  * @note   通用实现在 READY 位置位前持续轮询。上传路径另用有超时的寄存器采样。
  */
uint32_t se_trng_process(uint32_t *trng_low, uint32_t *trng_high);

#if 0
/**
  * @brief  声明保留的 CRC 初始化入口。
  * @param  无。
  * @retval 整数状态，当前分支未构建。
  * @note   以下 CRC 接口及模式枚举位于 #if 0。
  */
int se_crc_init(void);
/**
  * @brief  声明保留的 CRC 释放入口。
  * @param  无。
  * @retval 整数状态，当前分支未构建。
  * @note   与保留的 CRC 初始化配对。
  */
int se_crc_deinit(void);

#define CRC32_MODE_CRC32         0                   /* CRC 算法变体 CRC32 的选择值。 */
#define CRC32_MODE_MPEG2         1                   /* CRC 算法变体 MPEG2 的选择值。 */
#define CONFIG_CRC32_MODE        CRC32_MODE_CRC32    /* 禁用分支中选择缺省算法。 */
/**
  * @brief  声明保留的 CRC32 分段校验入口。
  * @param  buffer：[输入]可读的待校验缓冲。
  * @param  buf_len：[输入]参与计算的字节数。
  * @param  crc_initial：[输入]本段 CRC 初值。
  * @param  last：[输入]是否为最后一段。
  * @retval 返回 32 位校验结果。
  * @note   声明位于禁用分支，具体模式由同组配置宏选择。
  */
uint32_t se_crc32_process(uint8_t *buffer, uint32_t buf_len, uint32_t crc_initial, bool last);
/**
  * @brief  声明保留的 CRC32 完整缓冲校验入口。
  * @param  buffer：[输入]可读的待校验缓冲。
  * @param  buf_len：[输入]参与计算的字节数。
  * @retval 返回 32 位校验结果。
  * @note   声明位于禁用分支，具体模式由同组配置宏选择。
  */
uint32_t se_crc32(uint8_t *buffer, uint32_t buf_len);

#define CRC16_MODE_CCITT         0                   /* CRC 算法变体 CCITT 的选择值。 */
#define CRC16_MODE_CCITT_FALSE   1                   /* CRC 算法变体 CCITT_FALSE 的选择值。 */
#define CRC16_MODE_XMODEM        2                   /* CRC 算法变体 XMODEM 的选择值。 */
#define CRC16_MODE_X25           3                   /* CRC 算法变体 X25 的选择值。 */
#define CRC16_MODE_MODBUS        4                   /* CRC 算法变体 MODBUS 的选择值。 */
#define CRC16_MODE_IBM           5                   /* CRC 算法变体 IBM 的选择值。 */
#define CRC16_MODE_MAXIM         6                   /* CRC 算法变体 MAXIM 的选择值。 */
#define CRC16_MODE_USB           7                   /* CRC 算法变体 USB 的选择值。 */
#define CONFIG_CRC16_MODE        CRC16_MODE_CCITT    /* 禁用分支中选择缺省算法。 */
/**
  * @brief  声明保留的 CRC16 分段校验入口。
  * @param  buffer：[输入]可读的待校验缓冲。
  * @param  buf_len：[输入]参与计算的字节数。
  * @param  crc_initial：[输入]本段 CRC 初值。
  * @param  last：[输入]是否为最后一段。
  * @retval 返回 16 位校验结果。
  * @note   声明位于禁用分支，具体模式由同组配置宏选择。
  */
uint16_t se_crc16_process(uint8_t *buffer, uint32_t buf_len, uint16_t crc_initial, bool last);
/**
  * @brief  声明保留的 CRC16 完整缓冲校验入口。
  * @param  buffer：[输入]可读的待校验缓冲。
  * @param  buf_len：[输入]参与计算的字节数。
  * @retval 返回 16 位校验结果。
  * @note   声明位于禁用分支，具体模式由同组配置宏选择。
  */
uint16_t se_crc16(uint8_t *buffer, uint32_t buf_len);
#endif

#endif    /* SOC_SE_H_ */
