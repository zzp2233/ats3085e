/* 应用Shell命令集合。SPI2测试同步运行在Shell线程内，不另建栈；经spi_transceive的API表进入spi_acts.c，持FULL_WAKE_LOCK保护测试期间引脚。UART2/HTTP及配置、按键辅助命令也在本文件按各CONFIG分支编译；不因整文件注释而认定这些辅助命令属于SPI依赖。 */
/*
 * Copyright (c) 2019 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 应用Shell命令及SPI/UART链路诊断。
 */
#include <os_common_api.h>
#include <mem_manager.h>
#include <msg_manager.h>
#include <stdio.h>
#include <string.h>
#include <shell/shell.h>
#include <stdlib.h>
#include "system_app.h"
#include <drivers/uart.h>
#include <drivers/i2c.h>
#if defined(CONFIG_SPI_SLAVE) && IS_ENABLED(CONFIG_SPI_2)    /* 只为启用 SPI2 从机的固件编译测试命令。 */
#include <drivers/spi.h>                                     /* Zephyr SPI 配置、缓冲区及传输接口。 */
#include "spi_link_stress_v6.h"                              /* 两端一致的帧格式、序号和 CRC 工具函数。 */
#include <sys_wakelock.h>                                    /* 测试期间保持系统唤醒，避免休眠改变引脚状态。 */
#endif                                                       /* 结束当前条件编译分支。 */
#include <sensor_hal.h>
#ifdef CONFIG_PROPERTY
#include <property_manager.h>
#endif
#ifdef CONFIG_PLAYTTS
#include "tts_manager.h"
#endif
#ifdef CONFIG_BT_MANAGER
#include "bt_manager.h"
#endif

#ifdef CONFIG_MONKEY_TEST
#include<system_monkey_test.h>
#endif

#ifdef CONFIG_SHELL
#if defined(CONFIG_SPI_SLAVE) && IS_ENABLED(CONFIG_SPI_2)    /* 只为启用 SPI2 从机的固件编译测试命令。 */

/**
  * @brief  以Mode3从机同步执行旧固定图案收发并核对主机图案。
  * @param  sh：[输入] 有效Shell实例，用于打印及十六进制输出。
  * @param  send_pattern：[输入] true发送B0～BF，false发送16个零字节。
  * @retval 0：收到完整A0～AF；-ENODEV：未绑定SPI2；底层负值：传输失败；-EIO：字节不匹配。
  * @note   使用栈上16字节收发空间，同步调用完成后才离开作用域；持完整唤醒锁避免睡眠改脚，主机时钟超时由底层处理。
  */
static int spi2test_run(const struct shell *sh, bool send_pattern)
{
    static const struct spi_config config = {                                     /* 固定配置对象供同步 SPI 请求使用。 */
        .frequency = 812500,                                                      /* 请求配置为 812.5 kHz；从机实际移位由外部时钟控制。 */
        .operation = SPI_OP_MODE_SLAVE | SPI_MODE_CPOL | SPI_MODE_CPHA |          /* 选择从机、CPOL=1、CPHA=1，即 SPI Mode 3。 */
                     SPI_WORD_SET(8),                                             /* 每个 SPI 数据字为 8 位。 */
    };
    const struct device *dev = device_get_binding(CONFIG_SPI_2_NAME);             /* 按板级名称取得 SPI2 设备实例。 */
    uint8_t tx[16] = {0}, rx[16] = {0};                                           /* 准备 16 字节发送/接收缓冲区；默认发送全零。 */
    struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };                     /* 描述单块发送缓冲区及其字节数。 */
    struct spi_buf rx_buf = { .buf = rx, .len = sizeof(rx) };                     /* 描述单块接收缓冲区及其字节数。 */
    const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };         /* 将发送缓冲区包装成 Zephyr 所需的集合。 */
    const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };         /* 将接收缓冲区包装成 Zephyr 所需的集合。 */
    int ret;                                                                      /* 保存底层传输返回值，非零或负值按调用接口判错。 */

    if (!dev) {                                                                   /* 设备未注册时提前退出，禁止解引用空设备指针。 */
        shell_error(sh, "SPI_2 is unavailable");                                  /* 告知用户 SPI2 没有绑定到有效设备。 */
        return -ENODEV;                                                           /* 返回设备不存在错误。 */
    }
    if (send_pattern) {                                                           /* 旧版图案测试选择递增字节序列。 */
        for (size_t i = 0; i < sizeof(tx); i++) {                                 /* 逐字节填充固定长度的测试发送缓冲区。 */
            tx[i] = 0xb0 + i;                                                     /* 构造 B0、B1、…、BF 的固定图案。 */
        }
    }

    shell_print(sh, "SPI2 diag-v2 Mode 3 TX=%s waiting for UMA503 (1 second)",    /* 提示单次测试的发送图案和等待时限。 */
                send_pattern ? "B0..BF" : "16x00");                               /* 根据参数选择图案说明文本。 */
    sys_wake_lock(FULL_WAKE_LOCK);                                                /* 锁定完整唤醒状态，覆盖整个测试过程。 */
    ret = spi_transceive(dev, &config, &tx_set, &rx_set);                         /* 预装从机发送数据，并等待主机时钟完成同步收发。 */
    sys_wake_unlock(FULL_WAKE_LOCK);                                              /* 成对释放本次测试持有的唤醒锁。 */
    if (ret < 0) {                                                                /* 底层同步传输失败时不继续按有效帧处理。 */
        shell_error(sh, "SPI2 transfer failed: %d", ret);                         /* 输出具体驱动错误码，便于区分超时与设备错误。 */
        return ret;                                                               /* 将底层错误返回调用者。 */
    }

    shell_hexdump(sh, rx, sizeof(rx));                                            /* 打印完整接收帧，定位字节错位或数据损坏。 */
    for (size_t i = 0; i < sizeof(rx); i++) {                                     /* 逐字节检查旧版主机发送的固定图案。 */
        if (rx[i] != 0xa0 + i) {                                                  /* 旧版配套主机应发送 A0～AF。 */
            shell_error(sh, "SPI2 mismatch at byte %u", (unsigned int)i);         /* 输出第一个不匹配字节的位置。 */
            return -EIO;                                                          /* 以 I/O 错误表示数据检查失败。 */
        }
    }
    shell_print(sh, "SPI2 PASS: received A0..AF");                                /* 固定图案逐字节核对成功。 */
    return 0;                                                                     /* 当前函数或检查路径成功完成。 */
}

/**
  * @brief  执行16字节全零发送的旧版SPI2单帧测试。
  * @param  sh：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 0：固定图案核对成功；-ENODEV：无SPI2设备；驱动负错误或-EIO：收发失败。
  * @note   argc/argv未使用；同步占用Shell线程，旧主机应发送A0～AF。
  */
static int shell_spi2test0(const struct shell *sh, size_t argc, char *argv[])
{
    return spi2test_run(sh, false);    /* 复用单次收发函数，选择全零发送图案。 */
}

/**
  * @brief  执行B0～BF发送的旧版SPI2单帧测试。
  * @param  sh：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 0：固定图案核对成功；-ENODEV：无设备；其他负值：传输或图案错误。
  * @note   argc/argv未使用；只能配合旧固定图案主机。
  */
static int shell_spi2test1(const struct shell *sh, size_t argc, char *argv[])
{
    return spi2test_run(sh, true);    /* 复用单次收发函数，选择 B0～BF 图案。 */
}

/* SRAM已接近容量上限，连续测试在Shell线程中有界运行，不再创建额外任务和栈。 */

/**
  * @brief  执行SPI2从机连续帧测试并重复发送最终统计。
  * @param  sh：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 0：本地完成目标帧、无坏帧/缺口/IO错误且至少一次END发送成功；-EINVAL：参数错误；-ENODEV：无设备；-EIO：测试判据失败。
  * @note   argv[1]可选十进制帧数1～1000，默认100；持唤醒锁，SPI驱动同步轮询。PASS只证明本地接收，还须核对UMA结果。
  */
static int shell_spi2stress(const struct shell *sh, size_t argc, char *argv[])
{
    static uint16_t session;                                                                                    /* 跨多次 Shell 命令保留递增会话编号。 */
    static const struct spi_config config = {                                                                   /* 固定配置对象供同步 SPI 请求使用。 */
        .frequency = 812500,                                                                                    /* 请求配置为 812.5 kHz；从机实际移位由外部时钟控制。 */
        .operation = SPI_OP_MODE_SLAVE | SPI_MODE_CPOL | SPI_MODE_CPHA |                                        /* 选择从机、CPOL=1、CPHA=1，即 SPI Mode 3。 */
                     SPI_WORD_SET(8),                                                                           /* 每个 SPI 数据字为 8 位。 */
    };
    const struct device *dev = device_get_binding(CONFIG_SPI_2_NAME);                                           /* 按板级名称取得 SPI2 设备实例。 */
    unsigned long requested = 100;                                                                              /* 未提供数量参数时默认测试 100 帧。 */
    char *end;                                                                                                  /* 保存十进制转换结束位置，用于拒绝尾随非法字符。 */
    uint8_t tx[LINK_FRAME_SIZE], rx[LINK_FRAME_SIZE];                                                           /* 一帧的发送和接收空间，协议固定为 16 字节。 */
    struct spi_buf tx_buf = { .buf = tx, .len = sizeof(tx) };                                                   /* 描述单块发送缓冲区及其字节数。 */
    struct spi_buf rx_buf = { .buf = rx, .len = sizeof(rx) };                                                   /* 描述单块接收缓冲区及其字节数。 */
    const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };                                       /* 将发送缓冲区包装成 Zephyr 所需的集合。 */
    const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };                                       /* 将接收缓冲区包装成 Zephyr 所需的集合。 */
    unsigned int completed = 0, bad = 0, gaps = 0, io_errors = 0, tail_ok = 0;                                  /* 累计完成数、坏帧数、序号缺口、传输错误及 END 成功次数。 */
    uint16_t previous = 0;                                                                                      /* 记录上一个有效主机帧的序号。 */
    bool have_previous = false;                                                                                 /* 首帧没有比较基准，首次有效接收后置位。 */
    int ret = 0;                                                                                                /* 保存底层传输返回值，非零或负值按调用接口判错。 */

    if (argc > 2) {                                                                                             /* 命令最多接受一个可选的帧数参数。 */
        shell_error(sh, "usage: app spi2stress [1..1000] (default 100)");                                       /* 提示正确命令格式。 */
        return -EINVAL;                                                                                         /* 参数或缓冲区格式不满足要求，拒绝执行。 */
    }
    if (argc == 2) {                                                                                            /* 用户提供了自定义帧数。 */
        requested = strtoul(argv[1], &end, 10);                                                                 /* 按十进制解析帧数并保留结束指针。 */
        if (!argv[1][0] || *end || requested < 1 || requested > LINK_MAX_FRAMES) {                              /* 拒绝空串、非法尾字符及超出 1～1000 的数量。 */
            shell_error(sh, "count must be 1..1000");                                                           /* 告知有效测试帧数范围。 */
            return -EINVAL;                                                                                     /* 参数或缓冲区格式不满足要求，拒绝执行。 */
        }
    }
    if (!dev) return -ENODEV;                                                                                   /* 设备未注册时提前退出，禁止解引用空设备指针。 */
    if (++session == 0) ++session;                                                                              /* 递增会话并跳过保留编号 0。 */
    shell_print(sh, "SPI STRESS v7 start session=%u target=%u; UMA503 must run stress-v6",                      /* 打印本端版本及要求的配套主机协议。 */
                (unsigned int)session, (unsigned int)requested);                                                /* 输出本轮会话编号和目标帧数。 */
    sys_wake_lock(FULL_WAKE_LOCK);                                                                              /* 锁定完整唤醒状态，覆盖整个测试过程。 */
    for (unsigned int i = 0; i < requested; ++i) {                                                              /* 执行用户请求数量的 DATA 传输。 */
        link_frame(tx, LINK_SLAVE_MAGIC, LINK_DATA, session, (uint16_t)i,                                       /* 构造带本地会话和从机序号的 DATA 帧。 */
                   (uint16_t)bad, (uint16_t)requested, (uint16_t)gaps);                                         /* 携带截至上一帧的接收错误、目标数量和序号缺口。 */
        memset(rx, 0xa5, sizeof(rx));                                                                           /* 先填入 A5，便于发现接收缓冲区未被有效写入。 */
        ret = spi_transceive(dev, &config, &tx_set, &rx_set);                                                   /* 预装从机发送数据，并等待主机时钟完成同步收发。 */
        if (ret < 0) {                                                                                          /* 底层同步传输失败时不继续按有效帧处理。 */
            ++io_errors;                                                                                        /* 累计底层传输失败次数。 */
            shell_error(sh, "SPI STRESS IO_ERROR frame=%u ret=%d", i, ret);                                     /* 记录失败发生的帧号和驱动返回码。 */
            break;                                                                                              /* 结束当前循环或消息分支，不继续处理后续项。 */
        }
        ++completed;                                                                                            /* 仅在底层传输成功后增加完成数量。 */
        if (link_check(rx, LINK_MASTER_MAGIC) || rx[1] != LINK_DATA ||                                          /* 校验主机帧 CRC、方向、测试数据及 DATA 类型。 */
            link_u16(rx + 2) || link_u16(rx + 6) ||                                                             /* 主机帧的会话和从机错误字段必须为 0。 */
            link_u16(rx + 8) || link_u16(rx + 10)) {                                                            /* 主机帧的从机目标数和缺口字段必须为 0。 */
            ++bad;                                                                                              /* 累计本地接收到的错误帧。 */
            if (bad <= 3) {                                                                                     /* 限制坏帧详情输出，避免持续错误刷屏。 */
                shell_error(sh, "SPI STRESS BAD_RX frame=%u", i);                                               /* 指出当前坏帧位置，随后输出原始字节。 */
                shell_hexdump(sh, rx, sizeof(rx));                                                              /* 打印完整接收帧，定位字节错位或数据损坏。 */
            }
        } else {                                                                                                /* 进入另一处理分支。 */
            uint16_t seq = link_u16(rx + 4);                                                                    /* 读取当前有效帧的 16 位序号。 */
            if (have_previous) {                                                                                /* 已有有效前帧时才检查序号差值。 */
                uint16_t delta = (uint16_t)(seq - previous);                                                    /* 按 16 位差值计算，允许主机序号从 FFFF 回绕到 0000。 */
                if (delta == 0 || delta >= 0x8000u) {                                                           /* 差值为零表示重复；过大的反向差值按倒序处理。 */
                    ++bad;                                                                                      /* 重复或倒序的主机序号也计为坏帧。 */
                } else if (delta > 1) {                                                                         /* 正向差值大于 1，说明中间未收到部分主机序号。 */
                    unsigned int total = gaps + delta - 1u;                                                     /* 把本次序号缺口累加到统计值。 */
                    gaps = total > 65535u ? 65535u : total;                                                     /* 缺口字段只有 16 位，采用饱和计数避免回绕。 */
                }
            }
            previous = seq;                                                                                     /* 保存当前序号作为下一帧比较基准。 */
            have_previous = true;                                                                               /* 从此以后启用相邻有效帧序号检查。 */
        }
        if (completed % 25 == 0) {                                                                              /* 每累计 25 帧打印一次进度。 */
            shell_print(sh, "SPI STRESS progress %u/%u bad=%u master_gap=%u",                                   /* 输出从机当前完成数量和错误概况。 */
                        completed, (unsigned int)requested, bad, gaps);                                         /* 输出完成数、目标数、坏帧数及主机序号缺口。 */
        }
        k_msleep(2);                                                                                            /* 请求休眠 2 ms 后准备下一帧；UMA 主机当前每轮休眠 1 tick，约 5 ms，实际间隔还含传输和调度。 */
    }
    /* 最后一个DATA接收已检查完，才汇总最终计数。重复发送END，降低末次响应丢失却误判成功的概率。 */
    link_frame(tx, LINK_SLAVE_MAGIC, LINK_END, session, (uint16_t)completed,                                    /* 最后一个 DATA 已校验，构造包含最终计数的 END。 */
               (uint16_t)(bad + io_errors), (uint16_t)requested, (uint16_t)gaps);                               /* END 同时上报数据错误、传输错误和序号缺口。 */
    for (unsigned int i = 0; i < 3; ++i) {                                                                      /* 最多重复发送 3 个 END，提高结束汇总被接收的机会。 */
        ret = spi_transceive(dev, &config, &tx_set, &rx_set);                                                   /* 预装从机发送数据，并等待主机时钟完成同步收发。 */
        if (ret < 0) break;                                                                                     /* END 底层传输失败后停止重试。 */
        ++tail_ok;                                                                                              /* 记录已完成的 END 发送次数。 */
        k_msleep(2);                                                                                            /* 请求休眠 2 ms 后准备下一帧；UMA 主机当前每轮休眠 1 tick，约 5 ms，实际间隔还含传输和调度。 */
    }
    sys_wake_unlock(FULL_WAKE_LOCK);                                                                            /* 成对释放本次测试持有的唤醒锁。 */
    shell_print(sh, "SPI STRESS DONE session=%u local=%s good=%u/%u bad=%u master_gap=%u io=%u end_sent=%u",    /* 输出本地最终统计；对端仍需独立给出 PASS。 */
                (unsigned int)session,                                                                          /* 输出当前会话编号。 */
                completed == requested && !bad && !gaps && !io_errors && tail_ok ? "PASS" : "FAIL",             /* 全部目标帧完成、各错误为零且 END 发出，才判本地 PASS。 */
                completed - bad, (unsigned int)requested, bad, gaps, io_errors, tail_ok);                       /* 输出有效帧、目标数和各类错误及结束发送统计。 */
    /* 本地PASS只核实MOSI接收；UMA503的最终DONE还需核实全部MISO数据及从机统计。 */
    return completed == requested && !bad && !gaps && !io_errors && tail_ok ? 0 : -EIO;                         /* 将本地测试判定作为 Shell 命令返回值。 */
}

#endif    /* 结束当前条件编译分支。 */

/**
  * @brief  把数值键事件送入系统输入事件流程。
  * @param  shell：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 固定0；未将事件投递状态返回。
  * @note   argv[1]存在时按基数0解析事件值；argc/shell未使用，依赖Shell提供安全的参数数组。
  */
static int shell_input_key_event(const struct shell *shell, size_t argc, char *argv[])
{
	if (argv[1] != NULL) {                                      /* 首个参数指针存在时才解析，但原实现没有先检查argc。 */
		uint32_t key_event;                                 /* 保存用户输入的按键事件位编码。 */
		key_event = strtoul(argv[1], (char **) NULL, 0);    /* 按0基数自动识别十进制或前缀进制，未检查转换错误。 */
		sys_event_report_input(key_event);                  /* 把事件交给系统输入分发流程。 */
	}
	return 0;                                                   /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}
#ifdef CONFIG_MONKEY_TEST

/**
  * @brief  启动或停止随机按键测试。
  * @param  shell：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 固定0。
  * @note   argv[1]须存在，start可带argv[2]数值参数，未给时使用100；使用输入长度前缀比较，不严格验证完整单词。
  */
static int shell_monkey_event(const struct shell *shell, size_t argc, char *argv[])
{
	int len = strlen(argv[1]);                                                        /* 按用户输入长度做前缀匹配，空串/短前缀也可能匹配。 */

	if (!strncmp(argv[1], "start", len)) {                                            /* 按输入长度比较start前缀，符合时启动随机测试。 */
		if (argv[2] != NULL) {                                                    /* 第二参数存在时采用用户提供的测试配置值。 */
			system_monkey_test_start(strtoul(argv[2], (char **) NULL, 0));    /* 解析配置数值后交给随机测试模块。 */
		} else {
			system_monkey_test_start(100);                                    /* 未给配置时使用原默认值100。 */
		}
	} else if(!strncmp(argv[1], "stop", len)){                                        /* 按输入长度匹配stop前缀。 */
		system_monkey_test_stop();                                                /* 停止随机按键测试。 */
	}

	return 0;                                                                         /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}
#endif

/**
  * @brief  写入属性配置并尝试刷新持久存储。
  * @param  shell：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 固定0，即使property_set失败也不返回错误。
  * @note   原参数数量判断存在疑点：argc<2仍引用argv[1]，否则引用argv[2]；本次仅记录，不修正。
  */
static int shell_set_config(const struct shell *shell, size_t argc, char *argv[])
{
#ifdef CONFIG_PROPERTY
	int ret = 0;                                                      /* 保存属性操作的局部结果；函数末尾仍固定返回0。 */

	if (argc < 2) {                                                   /* 缺少必需字符串参数时显示用法或进入原配置处理分支。 */
		ret = property_set(argv[1], argv[1], 0);                  /* 原少参数分支以0长度写属性；argv[1]安全性未充分检查。 */
	} else {
		ret = property_set(argv[1], argv[2], strlen(argv[2]));    /* 以第二参数长度写属性值，不包含终止符。 */
	}

	if (ret < 0) {                                                    /* 属性写入返回负错误时不执行刷新。 */
		ret = -1;                                                 /* 本地归一错误值，但未传回Shell调用者。 */
	} else {
		property_flush(NULL);                                     /* 成功后请求刷新属性存储。 */
	}
#endif

	SYS_LOG_INF("set config %s : %s ok\n", argv[1], argv[2]);         /* 输出原有配置操作日志；该日志不保证属性写入成功。 */
	return 0;                                                         /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}

/**
  * @brief  输出蓝牙管理器诊断信息。
  * @param  shell：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 固定0。
  * @note   仅CONFIG_BT_MANAGER分支执行输出；参数均未参与处理。
  */
static int shell_dump_bt_info(const struct shell *shell, size_t argc, char *argv[])
{
#ifdef CONFIG_BT_MANAGER
	bt_manager_dump_info();    /* 由蓝牙管理模块输出当前诊断状态。 */
#endif
	return 0;                  /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}

#ifdef CONFIG_RECORD_APP

/**
  * @brief  声明录音模块采样率设置入口。
  * @param  sample_rate_kh：[输入] 采样率，按原接口以kHz编码，范围由录音模块限定。
  * @retval 无。
  */
extern void recorder_sample_rate_set(uint8_t sample_rate_kh);

/**
  * @brief  根据字符串参数设置录音采样率。
  * @param  shell：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 固定0。
  * @note   仅CONFIG_RECORD_APP分支编译；atoi结果传入uint8_t采样率接口，未做范围校验。
  */
static int shell_set_record_sample_rate(const struct shell *shell, size_t argc, char *argv[])
{
	if (argv[1] != NULL) {                              /* 首个参数指针存在时才解析，但原实现没有先检查argc。 */
		recorder_sample_rate_set(atoi(argv[1]));    /* 将字符串转换结果传给kHz编码接口，未验证合法范围。 */
	}
	return 0;                                           /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}
#endif

#ifdef CONFIG_UI_MANAGER

/**
  * @brief  打印应用UI栈信息。
  * @param  shell：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval 固定0。
  * @note   仅CONFIG_UI_MANAGER分支编译，命令参数未使用。
  */
static int shell_dump_uistack(const struct shell *shell, size_t argc, char *argv[])
{
	app_ui_stack_dump();    /* 输出当前UI栈以定位页面导航状态。 */
	return 0;               /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}
#endif    /*CONFIG_UI_MANAGER*/

#ifdef CONFIG_UART_2
/*
 * UART2 用于外接 4G 模组（UMA503-V7）的 AT 指令口。
 * 本段提供一个 shell 命令，做最小收发验证。
 */



/*
 * 函数功能：扫描已收到的 RX 字节，判断模组是否已经把最终结果码送完了
 * 执行流程：
 *   1. 只看缓冲区最后16个字节，假定最终结果码靠近尾部；并不保证完整语法识别
 *   2. 找连续的 "OK" 两字符，找到即认为本条指令应答结束
 * 说明：不能用"末尾是换行就收尾"来判，因为模组应答以 "\r\nOK\r\n" 开头，
 *       "\r\n" 末尾就是换行，那样会在 "OK" 还没到时提前退出
 * 返回：1 = 已收到 OK，0 = 还没
 */

/**
  * @brief  检查接收尾部16字节中是否存在连续OK字符。
  * @param  buf：[输入] 非空可读接收区，至少len字节，不要求零结尾。
  * @param  len：[输入] 已接收字节数，须非负。
  * @retval 1：找到连续OK；0：未找到。
  * @note   不是完整AT结果码语法校验，正文中出现OK也会命中。
  */
static int at2_rx_has_ok(const unsigned char *buf, int len)
{
	int i;                                               /* 当前字节遍历或命令发送索引。 */
	int from = (len > 16) ? (len - 16) : 0;              /* 只扫描末尾最多16字节，短缓冲从0开始。 */

	for (i = from; i + 1 < len; i++) {                   /* 每次保证还剩两个字节再检查OK。 */
		if (buf[i] == 'O' && buf[i + 1] == 'K') {    /* 检测连续O、K，不验证前后CR/LF。 */
			return 1;                            /* 本函数满足命中或容量上限判据，返回其约定的1。 */
		}
	}
	return 0;                                            /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}

/*
 * 函数功能：在已收到的数据里查找任意关键字（区分大小写）
 * 执行流程：
 *   1. 算出关键字长度，确定倒扫的最靠右起点
 *   2. 每个起点逐字符比对，全中即认为命中
 * 说明：at2_rx_has_ok 只会找 "OK"，这里要认 "CONNECT"、"+HTTPGET:"
 *       这类更长的标记，所以单独做一个通用版
 * 返回：0 = 找到了，-1 = 没有
 */

/**
  * @brief  从接收区末尾向前搜索区分大小写的关键字。
  * @param  buf：[输入] 非空可读接收区，至少len字节。
  * @param  len：[输入] 有效接收长度，单位字节，非负。
  * @param  tok：[输入] 非空、零结尾的可读关键字字符串。
  * @retval 0：找到；-1：关键字空、接收长度不足或未找到。
  * @note   按字节比较，不依赖buf零终止，也不解析响应行边界。
  */
static int at2_rx_has_tok(const unsigned char *buf, int len, const char *tok)
{
	int toklen = (int)strlen(tok);                                  /* 计算关键字字节数，不包括零终止。 */
	int pos;                                                        /* 关键字候选起点，从最右可匹配位置向前移动。 */

	if (toklen <= 0 || len < toklen) {                              /* 空关键字或缓冲过短时不尝试比较。 */
		return -1;                                              /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
	}

	for (pos = len - toklen; pos >= 0; pos--) {                     /* 从最后可能起点倒序搜索，不访问有效长度之外。 */
		int k;                                                  /* 当前关键字匹配或数值参数解析索引。 */
		int hit = 1;                                            /* 该候选起点先假设匹配，遇首个不同字节即清零。 */

		for (k = 0; k < toklen; k++) {                          /* 检查关键字每个字节，全部相等才命中。 */
			if (buf[pos + k] != (unsigned char)tok[k]) {    /* 当前字节不同，拒绝该候选起点。 */
				hit = 0;                                /* 标记本候选不匹配。 */
				break;                                  /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}
		}
		if (hit) {                                              /* 所有字节均一致，结束搜索。 */
			return 0;                                       /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
		}
	}

	return -1;                                                      /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
}

/*
 * 函数功能：把 UART2 上收到的字节持续攒进 rx，直到出现关键字、缓冲满或超时
 * 执行流程：
 *   1. 记下起点毫秒数
 *   2. 每轮先看 FIFO 有没有数据，有就一次读走追加到 *lenp
 *   3. 没数据时按序判断：关键字出现没 / 缓冲满没 / 空转 100us 再轮询
 *   4. 出循环后再复核一次关键字，防止最后一刻才到
 * 参数：tok 传 NULL 表示不等关键字，纯收一个窗口
 * 返回：0 = 关键字已出现，1 = 缓冲满，-1 = 超时
 */

/**
  * @brief  在给定毫秒窗口中把UART FIFO数据追加到接收缓冲。
  * @param  dev：[输入] 已初始化的UART设备对象，不可为空。
  * @param  rx：[输出] 可写接收缓冲，容量至少cap字节，不在此补零终止。
  * @param  cap：[输入] 缓冲容量，单位字节，至少3。
  * @param  lenp：[输入输出] 非空可写长度对象，初值范围0～cap-2，追加后更新。
  * @param  tok：[输入] 可选零结尾关键字；为空时只收满窗口。
  * @param  ms：[输入] 等待窗口，单位毫秒，须在有符号差值安全范围内。
  * @retval 0：检测到关键字；1：缓冲达到保留2字节后的上限；-1：窗口结束且未检测到关键字。
  * @note   读取采用RX就绪/FIFO接口；空闲忙等100微秒，不主动让出线程；tok为空时正常窗口结束也返回-1。
  */
static int at2_pump(const struct device *dev, unsigned char *rx, int cap,
		    int *lenp, const char *tok, int ms)
{
	unsigned int start = k_uptime_get_32();                                           /* 记录开机毫秒计数作为超时基准。 */

	while ((int)(k_uptime_get_32() - start) < ms) {                                   /* 按毫秒差持续采集，直到窗口到期；调用者须限制ms范围。 */
		/*
		 * 缓冲留两个字节余量就提前退出，别再进 FIFO 读。
		 * 否则读满后 cap - *lenp - 1 变成 0，uart_fifo_read 读 0 字节，
		 * 一路空转到超时，显示 TIMEOUT 其实是已经收满了。
		 */
		if (*lenp >= cap - 2) {                                                   /* 保留两字节空间，避免后续零长度FIFO读空转。 */
			return 1;                                                         /* 本函数满足命中或容量上限判据，返回其约定的1。 */
		}
		if (uart_irq_rx_ready(dev)) {                                             /* 仅在硬件报告RX可读时从FIFO批量取字节。 */
			int n = uart_fifo_read(dev, &rx[*lenp], cap - *lenp - 2);         /* 将可用FIFO字节追加到尾部，最多写剩余容量减2。 */

			if (n > 0) {                                                      /* 实际读到正数字节才推进有效长度。 */
				*lenp += n;    /* 累计接收区已填充字节数。 */
			}
		} else {
			if (tok && *lenp > 0 && at2_rx_has_tok(rx, *lenp, tok) == 0) {    /* 指定关键字且已收数据时检查是否满足提前结束条件。 */
				return 0;                                                 /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
			}
			k_busy_wait(100);                                                 /* 空闲时忙等100微秒，避免无间隔读取状态；不等同于线程休眠。 */
		}
	}

	if (tok && *lenp > 0 && at2_rx_has_tok(rx, *lenp, tok) == 0) {                    /* 指定关键字且已收数据时检查是否满足提前结束条件。 */
		return 0;                                                                 /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
	}

	return -1;                                                                        /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
}



/*
 * 函数功能：把 shell 输入的任意字符串发给 UART2，并把模组回复原样打印出来
 * 执行流程：
 *   1. 取 UART_2 设备句柄，失败直接报错返回
 *   2. 发送前先清掉 RX FIFO 里可能残留的旧字节，避免把上一条指令的尾巴算进来
 *   3. 把 argv 用空格拼成一条命令，末尾补 '\r'，逐字节 uart_poll_out 送出
 *   4. 开 500ms 接收窗口：轮询 uart_irq_rx_ready，有数据就 fifo_read 下来
 *      不用 uart_poll_in，acts 驱动里它是死等，无数据会把 shell 线程卡住
 *   5. 按 hex + ASCII 双列打印每个字节，模组回显和 OK / ERROR 都能看全
 * 用途：接上 UMA503-V7 之后直接敲 app at2tx AT / AT+CSQ / AT+CGATT? 等真指令
 * 注意：本轮不做内部回环，也不假设 53/54 短接，是专门对模组的透传口
 */

/**
  * @brief  把Shell参数拼为AT命令并轮询收取UART2应答。
  * @param  sh：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval -1：UART2绑定失败；0：显示用法或完成收发，不代表模组命令成功。
  * @note   argv[1..]为空格拼接，#替换为?；静态缓冲不可并发复用，最长等待500 ms，OK匹配只是连续字符检测。
  */
static int shell_at2tx(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;                                                                                        /* 保存UART2设备对象，绑定失败则不可继续操作。 */
	static unsigned char txbuf[200];                                                                                 /* 命令拼接空间，静态生命周期但不支持同命令并发。 */
	static unsigned char rxbuf[512];                                                                                 /* 单次AT应答缓冲，不动态分配。 */
	int txlen = 0;                                                                                                   /* 当前拼接的发送字节数，从空命令开始。 */
	int rxlen = 0;                                                                                                   /* 当前有效接收字节数，初始为空。 */
	int i;                                                                                                           /* 当前字节遍历或命令发送索引。 */
	unsigned int start;                                                                                              /* 保存500 ms窗口起始毫秒计数。 */

	dev = device_get_binding("UART_2");                                                                              /* 按实际注册名获取UART2对象，后续通过驱动API访问。 */
	if (!dev) {                                                                                                      /* UART2未注册或不可用时终止命令。 */
		shell_error(sh, "device UART_2 not found");                                                              /* 报告设备绑定失败，避免继续操作空设备对象。 */
		return -1;                                                                                               /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
	}

	if (argc < 2) {                                                                                                  /* 缺少必需字符串参数时显示用法或进入原配置处理分支。 */
		shell_print(sh, "usage: app at2tx <at-command>   e.g. app at2tx AT+CSQ");                                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "       shell 会把 '?' 当通配符吃掉，查询命令改用 '#' 代写：");                          /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "       app at2tx AT+CPIN#   ->  实际发送 AT+CPIN?");                                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		return 0;                                                                                                /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
	}

	/* 清掉上一条指令可能残留的尾巴 */
	(void)uart_fifo_read(dev, rxbuf, sizeof(rxbuf));                                                                 /* 丢弃FIFO里本次命令之前残留的应答。 */
	(void)uart_err_check(dev);                                                                                       /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */

	/* 拼命令：参数之间用空格，末尾补 CR */
	for (i = 1; i < argc; i++) {                                                                                     /* 逐项拼接命令参数，参数之间补一个空格。 */
		int n = strlen(argv[i]);                                                                                 /* 取得当前参数的字节长度，用于容量检查。 */

		if (i > 1) {                                                                                             /* 后续参数前插入空格，第一个参数不加前导空格。 */
			if (txlen >= (int)sizeof(txbuf) - 2) {                                                           /* 剩余空间不足以追加分隔符及命令终止时停止拼接。 */
				break;                                                                                   /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}
			txbuf[txlen++] = ' ';                                                                            /* 追加参数分隔空格并增加发送长度。 */
		}
		if (txlen + n >= (int)sizeof(txbuf) - 2) {                                                               /* 当前参数放不下则停止，保留回车所需空间。 */
			break;                                                                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
		}
		memcpy(&txbuf[txlen], argv[i], n);                                                                       /* 按长度复制参数，不复制其零终止符。 */
		/*
		 * Zephyr shell 把 '?' 当 glob 通配符吞掉（引号也拦不住），
		 * AT 查询命令一律改用 '#' 代写，这里转回 '?' 再发给模组。
		 */
		{
			int j;                                                                                           /* 数字字段扫描位置或替换字符索引。 */

			for (j = 0; j < n; j++) {                                                                        /* 只扫描刚复制的当前参数。 */
				if (txbuf[txlen + j] == '#') {                                                           /* 找到Shell问号代写字符。 */
					txbuf[txlen + j] = '?';                                                          /* 恢复模组AT查询需要的问号。 */
				}
			}
		}
		txlen += n;                                                                                              /* 将本参数字节数计入待发送总长。 */
	}
	txbuf[txlen++] = '\r';                                                                                           /* 附加AT命令结束回车，并计入发送长度。 */

	shell_print(sh, "TX %d byte(s) -> UART2", txlen);                                                                /* 打印即将发出的命令长度。 */
	for (i = 0; i < txlen; i++) {                                                                                    /* 按原顺序轮询发送命令中的每个字节。 */
		uart_poll_out(dev, txbuf[i]);                                                                            /* 同步交给UART驱动发送当前命令字节。 */
	}

	/* 500ms 接收窗口：模组回显 + 结果码通常几十 ms 内就回来 */
	rxlen = 0;                                                                                                       /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
	start = k_uptime_get_32();                                                                                       /* 记录本接收阶段的开机毫秒基准。 */
	while ((k_uptime_get_32() - start) < 500) {                                                                      /* 以无符号差计算500 ms上限，允许短时间计数回绕。 */
		if (uart_irq_rx_ready(dev)) {                                                                            /* 仅在硬件报告RX可读时从FIFO批量取字节。 */
			int n = uart_fifo_read(dev, &rxbuf[rxlen], (int)sizeof(rxbuf) - rxlen - 1);                      /* 最多读取剩余容量减1；此路径未专门处理满缓冲后0长度读。 */

			if (n > 0) {                                                                                     /* 实际读到正数字节才推进有效长度。 */
				rxlen += n;                                                                              /* 累计本次实际读取的字节数。 */
			}
		} else {
			/* 收到 OK 说明本轮应答已完整，可以提前收尾；否则继续等到 500ms */
			if (rxlen > 0 && at2_rx_has_ok(rxbuf, rxlen)) {                                                  /* RX暂时无新字节且尾部已有OK时结束此接收窗口。 */
				break;                                                                                   /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}
			k_busy_wait(100);                                                                                /* 空闲时忙等100微秒，避免无间隔读取状态；不等同于线程休眠。 */
		}
	}

	shell_print(sh, "RX %d byte(s):", rxlen);                                                                        /* 报告本次接收总长度。 */
	for (i = 0; i < rxlen; i++) {                                                                                    /* 按有效长度逐字节输出接收结果，不依赖零结尾。 */
		shell_print(sh, "  [%03d] 0x%02X  %c", i, rxbuf[i],                                                      /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rxbuf[i] >= 0x20 && rxbuf[i] < 0x7F) ? rxbuf[i] : '.');
	}
	if (rxlen == 0) {                                                                                                /* 窗口内没有收到字节时输出接线排查提示。 */
		shell_print(sh, "-> nothing received. check: module powered up?");                                       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "   53(TX) -> module UART_RXD(pin17), 54(RX) <- module UART_TXD(pin18), GND common");    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
	}

	return 0;                                                                                                        /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}

/*
 * 函数功能：用 AT 指令完整跑一次 HTTP GET，把服务器响应头打出来
 * 执行流程：
 *   1. 拼 URL：argv 里 '#' 换回 '?'（shell 会把 '?' 当通配符吃掉）
 *   2. 清掉残留，发 AT+HTTPCFG="requestheader",0      等 OK
 *   3. 发 AT+HTTPCFG="rspout/auto",0                   等 OK
 *      （手册印的是 rsppout/auto，实测模组认 rspout/auto，以实测为准）
 *   4. 发 AT+HTTPURL=<URL字节数>,60                    等 CONNECT
 *   5. 把 URL 原文原样灌进去，末尾补 0x1A（Ctrl+Z）     等 OK
 *   6. 发 AT+HTTPGET=10                                等 +HTTPGET:
 *   7. 发 AT+HTTPREAD=10                               等 CONNECT 后收响应体
 *      缓冲只有 600 字节，收满即停（记为 BUF-FULL），要收全文须补 RX DMA
 * 关键约束：第 4~5 步和第 7 步是"透传"，模组回 CONNECT 后不再认 AT，
 *          直到收到 0x1A 才切回命令模式
 * 用法：app at2http http://www.baidu.com
 * 判读：+HTTPGET 报 "0,200,<len>" 且第 7 步收到正文，即 MCU 真的从服务器拿到了数据
 */

/**
  * @brief  经UART2配置HTTP会话并执行一次GET及正文读取。
  * @param  sh：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval -1：设备、关键步骤或等待失败；0：显示用法或流程结束，不保证HTTP状态成功。
  * @note   argv[1..]组成URL；阻塞多个秒级窗口，接收缓冲600字节，HTTP状态在日志中报告。
  */
static int shell_at2http(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;                                                                          /* 保存UART2设备对象，绑定失败则不可继续操作。 */
	static unsigned char url[200];                                                                     /* URL拼接空间，须为后续终止符或控制字符预留容量。 */
	static unsigned char rx[600];                                                                      /* HTTP接收工作区，包含应答头、正文及可能的尾部结果码。 */
	int url_len = 0;                                                                                   /* 当前URL字节数，不包括结尾零字符。 */
	int txlen;                                                                                         /* 保存当前AT命令的发送字节数。 */
	int rxlen;                                                                                         /* 保存当前接收窗口累计字节数。 */
	int rc;                                                                                            /* 保存at2_pump结果，区分关键字命中、满缓冲和超时。 */
	int i;                                                                                             /* 当前字节遍历或命令发送索引。 */
    int http_err = -1;                                                                                     /* +HTTPGET 第 1 个字段：错误码，0 = 无错误      */
    int http_code = -1;                                                                                    /* +HTTPGET 第 2 个字段：HTTP 状态码，200 = 成功 */
    int http_blen = -1;                                                                                    /* +HTTPGET 第 3 个字段：服务器声明的正文长度    */

	if (argc < 2) {                                                                                    /* 缺少必需字符串参数时显示用法或进入原配置处理分支。 */
		shell_print(sh, "usage: app at2http <url>   e.g. app at2http http://www.baidu.com");       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "       URL 里的 '?' 请写成 '#'，shell 会吃掉问号");                       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		return 0;                                                                                  /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
	}

	dev = device_get_binding("UART_2");                                                                /* 按实际注册名获取UART2对象，后续通过驱动API访问。 */
	if (!dev) {                                                                                        /* UART2未注册或不可用时终止命令。 */
		shell_error(sh, "device UART_2 not found");                                                /* 报告设备绑定失败，避免继续操作空设备对象。 */
		return -1;                                                                                 /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
	}

	/* 拼 URL：多个 argv 用空格接回，'#' 还原成 '?' */
	for (i = 1; i < argc && url_len < (int)sizeof(url) - 2; i++) {                                     /* 拼接多个URL参数，保持两字节余量。 */
		int n = (int)strlen(argv[i]);                                                              /* 取得本段URL参数长度，用于容量检查。 */
		int k;                                                                                     /* 当前关键字匹配或数值参数解析索引。 */

		if (i > 1) {                                                                               /* 后续参数前插入空格，第一个参数不加前导空格。 */
			url[url_len++] = ' ';                                                              /* URL后续参数之间插入空格。 */
		}
		if (url_len + n >= (int)sizeof(url) - 1) {                                                 /* 当前参数会占用终止符空间时停止拼接。 */
			break;                                                                             /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
		}
		memcpy(&url[url_len], argv[i], n);                                                         /* 复制URL参数原始字节，随后恢复问号。 */
		for (k = 0; k < n; k++) {                                                                  /* 逐个检查本URL参数中的代写字符。 */
			if (url[url_len + k] == '#') {                                                     /* 定位URL中的问号代写字符。 */
				url[url_len + k] = '?';                                                    /* 恢复实际查询串问号。 */
			}
		}
		url_len += n;                                                                              /* 累计已拼接URL字节数。 */
	}
	url[url_len] = '\0';                                                                               /* 补零终止，保证后续字符串打印可控。 */

	shell_print(sh, "-> URL %d byte(s): %s", url_len, url);                                            /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */

/* 局部步骤宏：先清RX残留与错误，重置rxlen，按strlen逐字节发送cmd_str，调用at2_pump等待wait_tok最多wait_ms毫秒；依赖当前函数局部变量。 */
#define AT2HTTP_STEP(cmd_str, wait_tok, wait_ms, tag_str)                          \
	do {                                                                       \
		(void)uart_fifo_read(dev, rx, sizeof(rx));                         \
		(void)uart_err_check(dev);                                         \
		rxlen = 0;                                                         \
		txlen = (int)strlen(cmd_str);                                      \
		for (i = 0; i < txlen; i++) {                                      \
			uart_poll_out(dev, (unsigned char)(cmd_str)[i]);           \
		}                                                                  \
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, wait_tok, wait_ms); \
		shell_print(sh, "  %-17s TX%3d RX%3d  %s", tag_str, txlen, rxlen,   \
			    (rc == 0) ? "GOT" : (rc == 1) ? "BUF-FULL" : "TIMEOUT"); \
	} while (0)

	AT2HTTP_STEP("AT+HTTPCFG=\"requestheader\",0\r", "OK", 2000, "1/6 requestheader");                 /* 复用当前命令的清残留、发送和按毫秒窗口等待流程，参数中的字符串不作翻译。 */
	if (rc == -1) {                                                                                    /* 此阶段仅超时判失败，缓冲满是否接受沿用原逻辑。 */
		shell_print(sh, "-> 第 1 步就超时，先跑 app at2tx AT+HTTPCFG# 确认模组在");                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		return -1;                                                                                 /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
	}

	AT2HTTP_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, "2/6 rspout/auto");                     /* 复用当前命令的清残留、发送和按毫秒窗口等待流程，参数中的字符串不作翻译。 */

	/* 第 3 步要报 URL 字节数，命令是动态的，单独拼 */
	{
		char buf[64];                                                                              /* 保存包含URL字节数的动态AT命令。 */

		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                 /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                 /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                 /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		snprintk(buf, sizeof(buf), "AT+HTTPURL=%d,60\r", url_len);                                 /* 编码URL长度及模组URL输入等待参数，命令末尾含回车。 */
		txlen = (int)strlen(buf);                                                                  /* 计算动态命令实际发送字节数。 */
		for (i = 0; i < txlen; i++) {                                                              /* 按原顺序轮询发送命令中的每个字节。 */
			uart_poll_out(dev, (unsigned char)buf[i]);                                         /* 发送动态URL长度命令的当前字节。 */
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "CONNECT", 3000);                          /* 等待最多3000 ms，确认模组进入URL透传接收。 */
		shell_print(sh, "  3/6 url-length    TX%3d RX%3d  %s", txlen, rxlen,                       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "CONNECT" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc != 0) {                                                                             /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
			shell_print(sh, "-> 没等到 CONNECT，URL 长度不对或参数格式不对");                  /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			return -1;                                                                         /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
		}
	}

	/* 第 4 步：URL 原文灌进去 + 0x1A 收尾 */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                 /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                 /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                 /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		for (i = 0; i < url_len; i++) {                                                            /* 按报告给模组的精确字节数发送URL。 */
			uart_poll_out(dev, url[i]);                                                        /* 发送URL原文当前字节。 */
		}
		uart_poll_out(dev, 0x1A);                                                                  /* 发送Ctrl+Z结束透传输入，返回AT命令阶段。 */
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "OK", 4000);                               /* 最多等待4000 ms收取URL提交结果。 */
		shell_print(sh, "  4/6 url-body      TX%3d RX%3d  %s", url_len + 1, rxlen,                 /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "OK" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc == -1) {                                                                            /* 此阶段仅超时判失败，缓冲满是否接受沿用原逻辑。 */
			shell_print(sh, "-> URL 灌进去后没回 OK，多半是长度和实际字节数对不上");           /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			return -1;                                                                         /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
		}
	}

	/* 第 5 步：发 GET。模组先回 OK，稍后再报 +HTTPGET: */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                 /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                 /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                 /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		txlen = (int)strlen("AT+HTTPGET=10\r");                                                    /* 取得GET命令字节数，10为模组命令参数。 */
		for (i = 0; i < txlen; i++) {                                                              /* 按原顺序轮询发送命令中的每个字节。 */
			uart_poll_out(dev, "AT+HTTPGET=10\r"[i]);                                          /* 发送GET命令当前字节。 */
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "+HTTPGET:", 12000);                       /* 最多等待12000 ms，寻找异步HTTP结果前缀。 */
		/* 关键字只说明结果码开始上报，再多收 300ms 把 0,200,xxxx 收全 */
		if (rc == 0) {                                                                             /* 结果前缀已出现，继续采集余下字段。 */
			(void)at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 300);                       /* 追加300 ms接收窗口以收全结果字段，忽略无关键字时的-1。 */
		}
		shell_print(sh, "  5/6 http-get      TX%3d RX%3d  %s", txlen, rxlen,                       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "GOT +HTTPGET" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		for (i = 0; i < rxlen; i++) {                                                              /* 按有效长度逐字节输出接收结果，不依赖零结尾。 */
			shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],                                   /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
		}
		if (rc != 0) {                                                                             /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
			shell_print(sh, "-> 没等到 +HTTPGET，先确认 AT+CGPADDR 已经有 IP");                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			return -1;                                                                         /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
		}

		/*
		 * 解析 +HTTPGET: <err>,<httpcode>[,<body_len>]
		 * 注意 responseheader 默认 0，模组不会回 "HTTP/1.1 200 OK" 这种响应头行，
		 * 状态码只在这条 +HTTPGET 上报里出现，所以必须把它抠出来，不能靠找头行判断。
		 * 手工解析不用 sscanf，Zephyr 的 libc 不一定带浮点/%d 的完整实现。
		 */
		{
			int k;                                                                             /* 当前关键字匹配或数值参数解析索引。 */

			for (k = 0; k + 9 <= rxlen; k++) {                                                 /* 在有效接收区中查找9字节HTTPGET前缀。 */
				int j;                                                                     /* 数字字段扫描位置或替换字符索引。 */
				int idx = 0;                                                               /* 当前正在解析的字段序号，最多三个。 */
				int v[3] = {0, 0, 0};                                                      /* 三个十进制字段的累加值，初始0。 */
				int had[3] = {0, 0, 0};                                                    /* 每字段是否至少读到一个数字，区分缺失与数值0。 */

				/* 定位 "+HTTPGET:" 这个 9 字节标记 */
				/* 逐字节匹配HTTP结果标记；未命中即换下一个候选起点。 */
				if (!(rx[k] == '+' && rx[k + 1] == 'H' && rx[k + 2] == 'T' &&
				      rx[k + 3] == 'T' && rx[k + 4] == 'P' &&
				      rx[k + 5] == 'G' && rx[k + 6] == 'E' &&
				      rx[k + 7] == 'T' && rx[k + 8] == ':')) {
					continue;                                                          /* 当前候选不是完整结果标记，继续搜索下一个位置。 */
				}

				/* 标记后面按 数字 , 数字 , 数字 的方式读，遇到 \r\n 就停 */
				j = k + 9;                                                                 /* 跳过9字节HTTPGET标记，开始解析字段。 */
				while (j < rxlen && idx < 3) {                                             /* 在接收范围内读取最多三个字段，非数字/分隔符时停止。 */
					unsigned char c = rx[j];                                           /* 读取当前响应解析字节，避免字符串函数越界。 */

					if (c >= '0' && c <= '9') {                                        /* 十进制数字累加到当前字段。 */
						v[idx] = v[idx] * 10 + (int)(c - '0');                     /* 按十进制扩展字段值，原实现未检查整数溢出。 */
						had[idx] = 1;                                              /* 标记该字段已有数值内容。 */
						j++;                                                       /* 消费当前响应字符，推进扫描位置。 */
					} else if (c == ',') {                                             /* 逗号结束当前字段，切换到下一项。 */
						idx++;                                                     /* 推进错误码、HTTP状态码、长度三字段索引。 */
						j++;                                                       /* 消费当前响应字符，推进扫描位置。 */
					} else {
						break;                                                     /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
					}
				}

				http_err  = had[0] ? v[0] : -1;                                            /* 缺少错误码时保留-1，否则保存解析值。 */
				http_code = had[1] ? v[1] : -1;                                            /* 缺少HTTP状态码时用-1表示未知。 */
				http_blen = had[2] ? v[2] : -1;                                            /* 保存模组报告的正文长度，未出现则为-1。 */
				break;                                                                     /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}

			if (http_code < 0) {                                                               /* 结果前缀存在但状态字段缺失，报告可能截断。 */
				shell_print(sh, "-> 收到 +HTTPGET 但没解析出状态码，报文可能被截断");      /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			} else {
				shell_print(sh, "  => err=%d  http=%d  body=%d byte(s)",                   /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
					    http_err, http_code, http_blen);
			}
		}
	}

	/* 第 6 步：读正文。CONNECT 之后是裸数据流，收满缓冲就停 */
	{
		char text[220];                                                                            /* 正文可打印摘要空间，最多219个字符。 */
		int tlen = 0;                                                                              /* 摘要当前有效字符数。 */

		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                 /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                 /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                 /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		txlen = (int)strlen("AT+HTTPREAD=10\r");                                                   /* 取得读取正文命令的字节数。 */
		for (i = 0; i < txlen; i++) {                                                              /* 按原顺序轮询发送命令中的每个字节。 */
			uart_poll_out(dev, "AT+HTTPREAD=10\r"[i]);                                         /* 发送读取正文命令当前字节。 */
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 4000);                               /* 接收最长4000 ms或直到缓冲满，不按正文内容匹配关键字。 */
		shell_print(sh, "  6/6 http-read     TX%3d RX%3d  %s", txlen, rxlen,                       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "END" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");

		for (i = 0; i < rxlen; i++) {                                                              /* 按有效长度逐字节输出接收结果，不依赖零结尾。 */
			shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],                                   /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
		}

		/* 把可读字节连成一行，肉眼扫一眼就能看到正文开头是什么 */
		for (i = 0; i < rxlen && tlen < (int)sizeof(text) - 1; i++) {                              /* 只从有效接收区抽取摘要，始终预留零终止。 */
			unsigned char c = rx[i];                                                           /* 读取当前正文或应答字符。 */

			if (c >= 0x20 && c < 0x7F) {                                                       /* ASCII可打印字符原样加入摘要。 */
				text[tlen++] = (char)c;                                                    /* 追加可显示字符并推进摘要长度。 */
			} else if (c == '\r' || c == '\n') {                                               /* 换行符在单行摘要中转换为空格。 */
				text[tlen++] = ' ';                                                        /* 用空格保留原换行的大致分隔。 */
			}
		}
		text[tlen] = '\0';                                                                         /* 补零终止后可用%s打印。 */
		shell_print(sh, "  TEXT: %s", text);                                                       /* 显示过滤后的正文开头摘要。 */

		/*
		 * 判据用第 5 步抠出来的状态码，不再找 HTTP/1.x。
		 * responseheader=0 时模组 CONNECT 后直接吐 body，压根没有响应头行，
		 * 以前按"有没有 HTTP/1."判断永远为假，会把成功当成失败。
		 */
		if (http_code == 200 && rxlen > 0) {                                                       /* HTTP状态200且收到字节时报告本地成功判据，并未精确剥离正文。 */
			shell_print(sh, "-> HTTP 200 通过：服务器返回 %d 字节正文，本次收下 %d 字节",      /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    http_blen, rxlen);
			shell_print(sh, "-> 缓冲只有 600 字节，收满即停；要收全文需补 UART2 RX DMA");      /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		} else if (http_code >= 0) {                                                               /* 已有非200状态码时报告服务器返回状态。 */
			shell_print(sh, "-> 服务器回的是 HTTP %d（err=%d），不是 200，查 URL 或源站",      /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    http_code, http_err);
		} else if (rxlen > 0) {                                                                    /* 收到了内容但未解析出状态码，保留诊断信息。 */
			shell_print(sh, "-> 收到 %d 字节但没解析到状态码，先看上面 5/6 的 hex", rxlen);    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		} else {
			shell_print(sh, "-> 一个字节都没收到，检查 AT+HTTPREAD 的 rsptime");               /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		}
	}

#undef AT2HTTP_STEP

	return 0;                                                                                          /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}

/*
 * 函数功能：用 AT+HTTPGETEX 做一次范围 GET，判定服务器是否支持 Range 分段
 * 执行流程：
 *   1. 拼 URL（'#' 换回 '?'，Zephyr shell 会把问号当 glob 通配符吃掉）
 *   2. 发 AT+HTTPCFG="requestheader",0        等 OK
 *   3. 发 AT+HTTPCFG="rspout/auto",0          等 OK
 *   4. 发 AT+HTTPURL=<URL字节数>,60           等 CONNECT
 *   5. 灌 URL 原文 + 0x1A                     等 OK
 *   6. 立刻发 AT+HTTPGETEX=10,<start>,<len>   等 +HTTPGETEX:
 *      ← 必须紧跟第 5 步。实测：先跑一遍 AT+HTTPGET + AT+HTTPREAD 再发 GETEX，
 *        模组回 "+HTTPGETEX: -1,0,0"（手册第 5 章：-1 = 客户端错误），
 *        随后 AT+HTTPREAD 直接回 ERROR。说明 HTTP 客户端被上一次请求周期消费掉了。
 *   7. 补收 300ms 把三字段收全，手工解析（err 可能是负数 -1，必须支持前导 '-'）
 *   8. 发 AT+HTTPREAD=10 收数据，单独识别 ERROR 应答（它只有 9 字节，别当数据）
 *   9. 按 httpcode 给结论：206=分段可用 / 200=服务器忽略 Range / 416=start 越界
 * 用法：app at2gex <url> [start] [len]
 *   例：app at2gex http://www.baidu.com/ 0 600
 *       app at2gex http://www.baidu.com/ 600 600
 * 前提：requestheader 必须是 0，本命令自己会配
 */

/**
  * @brief  执行一次HTTP范围请求以诊断服务器的Range支持。
  * @param  sh：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval -1：设备或前置等待失败；0：显示用法或报告请求结果。
  * @note   argv[1]为URL，argv[2]/[3]可选起点/长度，单位字节，长度默认598并限缓冲范围；数值解析未检查溢出或尾随字符。
  */
static int shell_at2gex(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;                                                                                    /* 保存UART2设备对象，绑定失败则不可继续操作。 */
	static unsigned char url[200];                                                                               /* URL拼接空间，须为后续终止符或控制字符预留容量。 */
	static unsigned char rx[600];                                                                                /* HTTP接收工作区，包含应答头、正文及可能的尾部结果码。 */
	int url_len = 0;                                                                                             /* 当前URL字节数，不包括结尾零字符。 */
	int txlen;                                                                                                   /* 保存当前AT命令的发送字节数。 */
	int rxlen;                                                                                                   /* 保存当前接收窗口累计字节数。 */
	int rc;                                                                                                      /* 保存at2_pump结果，区分关键字命中、满缓冲和超时。 */
	int i;                                                                                                       /* 当前字节遍历或命令发送索引。 */
    int start_pos = 0;                                                                                               /* 从正文第几字节开始读，默认 0                    */
    int read_len = 598;                                                                                              /* 本次要读多少字节，默认填满缓冲                   */
    int gx_err = 99;                                                                                                 /* +HTTPGETEX 第 1 字段：错误码，0 = 成功           */
    int gx_code = -1;                                                                                                /* +HTTPGETEX 第 2 字段：HTTP 状态码                */
    int gx_len = -1;                                                                                                 /* +HTTPGETEX 第 3 字段：服务器声明总长度            */
    int rd_len = 0;                                                                                                  /* READ 实际收到多少字节                            */
    int rd_error = 0;                                                                                                /* READ 回的是 ERROR 而不是数据                     */

	if (argc < 2) {                                                                                              /* 缺少必需字符串参数时显示用法或进入原配置处理分支。 */
		shell_print(sh, "usage: app at2gex <url> [start] [len]");                                            /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "   e.g. app at2gex http://www.baidu.com/ 0 600");                                   /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "        app at2gex http://www.baidu.com/ 600 600");                                 /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "       URL 里的 '?' 请写成 '#'");                                                   /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		return 0;                                                                                            /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
	}

	dev = device_get_binding("UART_2");                                                                          /* 按实际注册名获取UART2对象，后续通过驱动API访问。 */
	if (!dev) {                                                                                                  /* UART2未注册或不可用时终止命令。 */
		shell_error(sh, "device UART_2 not found");                                                          /* 报告设备绑定失败，避免继续操作空设备对象。 */
		return -1;                                                                                           /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
	}

	/* 拼 URL：'#' 还原成 '?' */
	for (i = 0; argv[1][i] != '\0' && url_len < (int)sizeof(url) - 2; i++) {                                     /* 从单个URL参数逐字节复制，保留尾部余量。 */
		url[url_len] = argv[1][i];                                                                           /* 复制当前URL字符。 */
		if (url[url_len] == '#') {                                                                           /* 识别Shell查询字符代写。 */
			url[url_len] = '?';                                                                          /* 恢复URL实际问号。 */
		}
		url_len++;                                                                                           /* 推进URL有效字节数。 */
	}
	url[url_len] = '\0';                                                                                         /* 补零终止，保证后续字符串打印可控。 */

	/* 转 start / len 两个整数参数：只认 0~9 */
	if (argc >= 3) {                                                                                             /* 提供了可选起点或分段长度参数才解析。 */
		int v = 0;                                                                                           /* 十进制参数累加值，初始0。 */
		int k;                                                                                               /* 当前关键字匹配或数值参数解析索引。 */

		for (k = 0; argv[2][k] >= '0' && argv[2][k] <= '9'; k++) {                                           /* 仅接受第二参数开头的连续数字，遇非数字停止。 */
			v = v * 10 + (argv[2][k] - '0');                                                             /* 累加十进制参数，未检查溢出或尾部字符。 */
		}
		start_pos = v;                                                                                       /* 记录Range起始字节偏移。 */
	}
	if (argc >= 4) {                                                                                             /* 仅在提供长度参数时覆盖默认请求长度。 */
		int v = 0;                                                                                           /* 十进制参数累加值，初始0。 */
		int k;                                                                                               /* 当前关键字匹配或数值参数解析索引。 */

		for (k = 0; argv[3][k] >= '0' && argv[3][k] <= '9'; k++) {                                           /* 解析长度参数的十进制前缀。 */
			v = v * 10 + (argv[3][k] - '0');                                                             /* 累加请求字节数，未检查溢出。 */
		}
		read_len = v;                                                                                        /* 采用输入的请求分段字节数。 */
	}
	if (read_len <= 0 || read_len > (int)sizeof(rx) - 2) {                                                       /* 非法或过大长度改为接收缓冲可用上限。 */
		read_len = (int)sizeof(rx) - 2;                                                                      /* 请求上限为598字节，仍可能受到响应头占用影响。 */
	}

	shell_print(sh, "-> URL %d byte(s): %s   start=%d  len=%d",                                                  /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		    url_len, url, start_pos, read_len);

	/*
	 * 第 2~5 步：配 HTTPCFG 和 URL。
	 * 用局部宏把"清残留 → 发命令 → 等关键字"三件事压成一行。
	 */
/* 局部步骤宏：先清RX残留与错误，重置rxlen，按strlen逐字节发送cmd_str，调用at2_pump等待wait_tok最多wait_ms毫秒；依赖当前函数局部变量。 */
#define AT2GEX_STEP(cmd_str, wait_tok, wait_ms, tag_str)                           \
	do {                                                                       \
		(void)uart_fifo_read(dev, rx, sizeof(rx));                         \
		(void)uart_err_check(dev);                                         \
		rxlen = 0;                                                         \
		txlen = (int)strlen(cmd_str);                                      \
		for (i = 0; i < txlen; i++) {                                      \
			uart_poll_out(dev, (unsigned char)(cmd_str)[i]);           \
		}                                                                  \
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, wait_tok, wait_ms); \
		shell_print(sh, "  %-14s TX%3d RX%3d  %s", tag_str, txlen, rxlen,   \
			    (rc == 0) ? "GOT" : (rc == 1) ? "BUF-FULL" : "TIMEOUT"); \
	} while (0)

	AT2GEX_STEP("AT+HTTPCFG=\"requestheader\",0\r", "OK", 2000, "1/6 cfg-hdr");                                  /* 复用当前命令的清残留、发送和按毫秒窗口等待流程，参数中的字符串不作翻译。 */
	if (rc != 0) {                                                                                               /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
		shell_print(sh, "-> 第 1 步失败，先跑 app at2tx AT 确认模组在线");                                   /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		return -1;                                                                                           /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
	}

	AT2GEX_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, "2/6 cfg-rsp");                                    /* 复用当前命令的清残留、发送和按毫秒窗口等待流程，参数中的字符串不作翻译。 */

	/* 第 4 步：URL 长度是变量，单独拼 */
	{
		char buf[64];                                                                                        /* 保存包含URL字节数的动态AT命令。 */

		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                           /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                           /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                           /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		snprintk(buf, sizeof(buf), "AT+HTTPURL=%d,60\r", url_len);                                           /* 编码URL长度及模组URL输入等待参数，命令末尾含回车。 */
		txlen = (int)strlen(buf);                                                                            /* 计算动态命令实际发送字节数。 */
		for (i = 0; i < txlen; i++) {                                                                        /* 按原顺序轮询发送命令中的每个字节。 */
			uart_poll_out(dev, (unsigned char)buf[i]);                                                   /* 发送动态URL长度命令的当前字节。 */
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "CONNECT", 3000);                                    /* 等待最多3000 ms，确认模组进入URL透传接收。 */
		shell_print(sh, "  3/6 url-length TX%3d RX%3d  %s", txlen, rxlen,                                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "CONNECT" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc != 0) {                                                                                       /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
			shell_print(sh, "-> 没等到 CONNECT，URL 长度与实际字节数可能差一个");                        /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			return -1;                                                                                   /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
		}
	}

	/* 第 5 步：灌 URL 原文 + 0x1A 收尾 */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                           /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                           /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                           /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		for (i = 0; i < url_len; i++) {                                                                      /* 按报告给模组的精确字节数发送URL。 */
			uart_poll_out(dev, url[i]);                                                                  /* 发送URL原文当前字节。 */
		}
		uart_poll_out(dev, 0x1A);                                                                            /* 发送Ctrl+Z结束透传输入，返回AT命令阶段。 */
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "OK", 4000);                                         /* 最多等待4000 ms收取URL提交结果。 */
		shell_print(sh, "  4/6 url-body   TX%3d RX%3d  %s", url_len + 1, rxlen,                              /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "OK" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc == -1) {                                                                                      /* 此阶段仅超时判失败，缓冲满是否接受沿用原逻辑。 */
			shell_print(sh, "-> URL 灌完没回 OK，长度可能和实际字节数对不上");                           /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			return -1;                                                                                   /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
		}
	}

	/*
	 * 第 6 步：紧跟 GETEX，中间绝不能夹 GET 或 READ。
	 * 实测教训：先 AT+HTTPGET + AT+HTTPREAD 再发 GETEX，
	 * 模组回 "+HTTPGETEX: -1,0,0"（-1 = 客户端错误），READ 直接 ERROR。
	 */
	{
		char cmd[48];                                                                                        /* 保存包含起点和段长的GETEX命令。 */

		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                           /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                           /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                           /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		snprintk(cmd, sizeof(cmd), "AT+HTTPGETEX=10,%d,%d\r", start_pos, read_len);                          /* 将范围起点及请求长度编码进GETEX命令。 */
		txlen = (int)strlen(cmd);                                                                            /* 取得动态范围命令实际长度。 */
		for (i = 0; i < txlen; i++) {                                                                        /* 按原顺序轮询发送命令中的每个字节。 */
			uart_poll_out(dev, (unsigned char)cmd[i]);                                                   /* 发送GETEX动态命令当前字节。 */
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "+HTTPGETEX:", 12000);                               /* 最多等待12秒寻找范围请求异步结果。 */
		if (rc == 0) {                                                                                       /* 结果前缀已出现，继续采集余下字段。 */
			/* 关键字只说明上报开始，再补 300ms 把 0,206,xxxx 收全 */
			(void)at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 300);                                 /* 追加300 ms接收窗口以收全结果字段，忽略无关键字时的-1。 */
		}
		shell_print(sh, "  5/6 getex      TX%3d RX%3d  %s", txlen, rxlen,                                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "GOT +HTTPGETEX" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		for (i = 0; i < rxlen; i++) {                                                                        /* 按有效长度逐字节输出接收结果，不依赖零结尾。 */
			shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],                                             /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
		}

		if (rc != 0) {                                                                                       /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
			shell_print(sh, "-> 没等到 +HTTPGETEX 上报");                                                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			return -1;                                                                                   /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
		}

		/*
		 * 解析 +HTTPGETEX: <err>,<httpcode>,<content_length>
		 * 标记 11 字节（比 +HTTPGET: 多 "EX" 两个字符，别照抄那个 9 字节匹配）。
		 * err 可能是负数（-1 = 客户端错误），必须支持前导 '-'，
		 * 否则 -1 会被当成"没解析出来"。
		 */
		{
			int k;                                                                                       /* 当前关键字匹配或数值参数解析索引。 */

			for (k = 0; k + 11 <= rxlen; k++) {                                                          /* 保证11字节标记可完整访问后逐位置匹配。 */
				int j;                                                                               /* 数字字段扫描位置或替换字符索引。 */
				int idx = 0;                                                                         /* 当前正在解析的字段序号，最多三个。 */
				int v[3] = {0, 0, 0};                                                                /* 三个十进制字段的累加值，初始0。 */
				int neg[3] = {0, 0, 0};                                                              /* 每字段负号标志，用于GETEX错误码解析。 */
				int had[3] = {0, 0, 0};                                                              /* 每字段是否至少读到一个数字，区分缺失与数值0。 */

				/* 逐字节匹配HTTP结果标记；未命中即换下一个候选起点。 */
				if (!(rx[k] == '+' && rx[k + 1] == 'H' && rx[k + 2] == 'T' &&
				      rx[k + 3] == 'T' && rx[k + 4] == 'P' &&
				      rx[k + 5] == 'G' && rx[k + 6] == 'E' &&
				      rx[k + 7] == 'T' && rx[k + 8] == 'E' &&
				      rx[k + 9] == 'X' && rx[k + 10] == ':')) {
					continue;                                                                    /* 当前候选不是完整结果标记，继续搜索下一个位置。 */
				}

				j = k + 11;                                                                          /* 跳过11字节HTTPGETEX标记，开始解析字段。 */
				while (j < rxlen && idx < 3) {                                                       /* 在接收范围内读取最多三个字段，非数字/分隔符时停止。 */
					unsigned char c = rx[j];                                                     /* 读取当前响应解析字节，避免字符串函数越界。 */

					if (c == '-') {                                                              /* 识别当前字段的负号，主要供客户端错误码使用。 */
						neg[idx] = 1;                                                        /* 记录当前字段需要取负值。 */
						j++;                                                                 /* 消费当前响应字符，推进扫描位置。 */
					} else if (c >= '0' && c <= '9') {                                           /* 负号检查之后处理十进制数字。 */
						v[idx] = v[idx] * 10 + (int)(c - '0');                               /* 按十进制扩展字段值，原实现未检查整数溢出。 */
						had[idx] = 1;                                                        /* 标记该字段已有数值内容。 */
						j++;                                                                 /* 消费当前响应字符，推进扫描位置。 */
					} else if (c == ',') {                                                       /* 逗号结束当前字段，切换到下一项。 */
						idx++;                                                               /* 推进错误码、HTTP状态码、长度三字段索引。 */
						j++;                                                                 /* 消费当前响应字符，推进扫描位置。 */
					} else {
						break;                                                               /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
					}
				}

				gx_err  = had[0] ? (neg[0] ? -v[0] : v[0]) : 99;                                     /* 恢复错误码符号，缺失时保留哨兵99。 */
				gx_code = had[1] ? v[1] : -1;                                                        /* 保存HTTP状态码，缺失时为-1。 */
				gx_len  = had[2] ? v[2] : -1;                                                        /* 保存结果长度，未报告时为-1。 */
				break;                                                                               /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}

			shell_print(sh, "  => err=%d  http=%d  len=%d", gx_err, gx_code, gx_len);                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		}
	}

	/*
	 * 第 7 步：读数据。三种应答要分清：
	 *   CONNECT + 一堆字节  → 正常吐数据
	 *   \r\nERROR\r\n       → 模组拒绝（会话已失效或无可读内容）
	 *   什么都没有          → 超时
	 */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));                                                           /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
		(void)uart_err_check(dev);                                                                           /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
		rxlen = 0;                                                                                           /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
		txlen = (int)strlen("AT+HTTPREAD=10\r");                                                             /* 取得读取正文命令的字节数。 */
		for (i = 0; i < txlen; i++) {                                                                        /* 按原顺序轮询发送命令中的每个字节。 */
			uart_poll_out(dev, "AT+HTTPREAD=10\r"[i]);                                                   /* 发送读取正文命令当前字节。 */
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 4000);                                         /* 接收最长4000 ms或直到缓冲满，不按正文内容匹配关键字。 */
		rd_len = rxlen;                                                                                      /* 先把当前接收总量作为读取长度。 */
		/* ERROR 应答只有 9 字节，加上长度上限才敢判，避免正文里正好有 ERROR 字样 */
		if (rxlen > 0 && rxlen <= 16 && at2_rx_has_tok(rx, rxlen, "ERROR") == 0) {                           /* 短响应含ERROR时按模组拒绝读取处理；避免大正文中的同名字符串误判。 */
			rd_error = 1;                                                                                /* 记录读取阶段返回了ERROR。 */
			rd_len = 0;                                                                                  /* 模组错误响应不计作正文。 */
		}
		shell_print(sh, "  6/6 http-read  TX%3d RX%3d  %s", txlen, rxlen,                                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    rd_error ? "ERROR" :
			    (rc == 0) ? "END" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (!rd_error) {                                                                                     /* 正常接收才逐字节输出数据。 */
			for (i = 0; i < rxlen; i++) {                                                                /* 按有效长度逐字节输出接收结果，不依赖零结尾。 */
				shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],                                     /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
					    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
			}
		}
	}

	/* 第 8 步：给结论 */
	{
		shell_print(sh, "");                                                                                 /* 在阶段或结果之间输出空行。 */
		if (gx_err == -1) {                                                                                  /* 客户端错误优先报告会话不可用。 */
			shell_print(sh, "-> err=-1（客户端错误）：这条 HTTP 客户端已经不可用了");                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			shell_print(sh, "   本命令已改成 URL 之后紧跟 GETEX、中间不夹任何请求；");                   /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			shell_print(sh, "   若仍报 -1，说明 GETEX 对同一 URL 只能配合一次请求周期");                 /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			shell_print(sh, "   VERDICT: 分段这条路不通，回去做 UART2 RX DMA");                          /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		} else if (gx_code == 206) {                                                                         /* 206表示服务器按Range返回部分内容。 */
			shell_print(sh, "-> 206 Partial Content：服务器认 Range，分段可行");                         /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			shell_print(sh, "   本次 READ 吐出 %d 字节（缓冲上限 %d）", rd_len, (int)sizeof(rx) - 2);    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			if (gx_len > 0) {                                                                            /* 长度有效时估算分段次数。 */
				shell_print(sh, "   声明总长 %d，按 %d 一段循环约 %d 次",                            /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
					    gx_len, read_len, (gx_len + read_len - 1) / read_len);
			}
			shell_print(sh, "   VERDICT: 分段可用，不必补 UART2 RX DMA");                                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		} else if (gx_code == 200) {                                                                         /* 200表示服务器返回完整资源，本诊断据此判断Range被忽略。 */
			shell_print(sh, "-> 200 OK：服务器忽略了 Range，仍回全文");                                  /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			shell_print(sh, "   本次 READ 吐出 %d 字节（被缓冲截断），声明总长 %d",                      /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    rd_len, gx_len);
			shell_print(sh, "   VERDICT: 分段不可用，只能补 UART2 RX DMA");                              /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		} else if (gx_code == 416) {                                                                         /* 416表示请求范围超出可用资源。 */
			shell_print(sh, "-> 416 Range Not Satisfiable：start=%d 超出资源总长 %d",                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    start_pos, gx_len);
			shell_print(sh, "   VERDICT: Range 已生效，只是 start 越界，调小重试");                      /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		} else if (rd_error) {                                                                               /* 没有前述状态时，读取ERROR作为失败依据。 */
			shell_print(sh, "-> GETEX 报 err=%d http=%d，随后 READ 回 ERROR", gx_err, gx_code);          /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			shell_print(sh, "   VERDICT: 请求没成立，换 URL 或换接口再试");                              /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		} else {
			shell_print(sh, "-> GETEX err=%d http=%d len=%d，READ 收到 %d 字节",                         /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    gx_err, gx_code, gx_len, rd_len);
			shell_print(sh, "   VERDICT: 数据不明确，看上面 hex 自己判");                                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		}
	}

#undef AT2GEX_STEP

	return 0;                                                                                                    /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}

/*
 * 函数功能：循环调用 AT+HTTPGETEX 分段拉取整个资源，逐段从串口吐出
 * 执行流程：
 *   1. 外层 for 循环：start 从 0 起，每轮 += seg_len
 *   2. 每轮都完整重配一次会话（这是硬前提，不能省）：
 *        2.1 AT+HTTPCFG="requestheader",0   等 OK
 *        2.2 AT+HTTPCFG="rspout/auto",0     等 OK
 *        2.3 AT+HTTPURL=<URL字节数>,60       等 CONNECT
 *        2.4 灌 URL 原文 + 0x1A             等 OK
 *        2.5 立刻 AT+HTTPGETEX=10,start,seg 等 +HTTPGETEX:（中间不夹任何请求）
 *   3. 解析 +HTTPGETEX 的三字段 err,httpcode,content_length
 *   4. 按 httpcode 分支：
 *        206 → 继续读；416 → 读完，跳出；其它 → 报错并跳出
 *   5. AT+HTTPREAD=10 取数据，剥离 "CONNECT" 应答头，只留正文
 *   6. 正文按 16 字节一行打 hex（带全局偏移），可校验、可还原
 *   7. 每段结束打印进度；短段、416或异常等条件均可能结束循环，末尾打印已处理统计
 * 用法：app at2get <url> [seg_len]
 *   例：app at2get http://www.baidu.com/
 *       app at2get http://www.baidu.com/ 598
 * 注意：
 *   - URL 里的 '?' 请写成 '#'（Zephyr shell 会把问号当 glob 通配符吞掉）
 *   - seg_len上限584（预留应答头等16字节）；不给则默认580
 *   - 输出是 hex，不是原文。用配套的 Python 脚本还原成文件
 *   - 全程约 60~90 秒（每段都要重新建一次连接）
 */

/**
  * @brief  循环重建HTTP会话并分段输出资源十六进制数据。
  * @param  sh：[输入] Shell框架提供的有效实例，用于同步输出。
  * @param  argc：[输入] 命令参数个数，包含当前命令名。
  * @param  argv：[输入] Shell参数字符串数组；处理期间有效，由框架持有，不释放。
  * @retval -1：UART2不存在；0：显示用法或循环退出，包括中途错误。
  * @note   argv[1]为URL，argv[2]可选段长；当前默认580字节、最大584，最多200段。每段接收与打印同步阻塞，返回0不是全文完整性保证。
  */
static int shell_at2get(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;                                                                        /* 保存UART2设备对象，绑定失败则不可继续操作。 */
	static unsigned char url[200];                                                                   /* URL拼接空间，须为后续终止符或控制字符预留容量。 */
	static unsigned char rx[600];                                                                    /* HTTP接收工作区，包含应答头、正文及可能的尾部结果码。 */
	int url_len = 0;                                                                                 /* 当前URL字节数，不包括结尾零字符。 */
    int seg_len = 580;                                                                                   /* 每段读多少。不能取 598，因为 READ 回来的 */
	                                /* 数据前面有 "\r\n\r\nCONNECT\r\n" 共 13 字节 */
	                                /* 应答头，正文上限 = rx[600] - 13 - 余量。     */
	                                /* 取 580 时回 593 字节，留 7 字节余量。        */
    int start_pos = 0;                                                                                   /* 当前段的起始偏移                       */
    int total = 0;                                                                                       /* 已累计吐出的正文字节数                 */
    int seg_idx = 0;                                                                                     /* for 循环变量（从 0 开始）              */
    int done_seg = 0;                                                                                    /* 真正处理完并吐出数据的段数             */
	int txlen;                                                                                       /* 保存当前AT命令的发送字节数。 */
	int rxlen;                                                                                       /* 保存当前接收窗口累计字节数。 */
	int rc;                                                                                          /* 保存at2_pump结果，区分关键字命中、满缓冲和超时。 */
	int i;                                                                                           /* 当前字节遍历或命令发送索引。 */
    int gx_err = 99;                                                                                     /* +HTTPGETEX 第 1 字段：错误码，0 = 成功 */
    int gx_code = -1;                                                                                    /* +HTTPGETEX 第 2 字段：HTTP 状态码      */
    int gx_len = -1;                                                                                     /* +HTTPGETEX 第 3 字段：本段响应体长度    */
    int body_off = 0;                                                                                    /* READ 数据里正文的起始偏移              */
    int body_len = 0;                                                                                    /* 本段正文实际字节数                     */
    int rd_error = 0;                                                                                    /* READ 回的是 ERROR 而不是数据           */

	if (argc < 2) {                                                                                  /* 缺少必需字符串参数时显示用法或进入原配置处理分支。 */
		shell_print(sh, "usage: app at2get <url> [seg_len]");                                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "   e.g. app at2get http://www.baidu.com/");                             /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "        app at2get http://www.baidu.com/ 580");                         /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "       URL 里的 '?' 请写成 '#'");                                       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "       seg_len 默认 580，上限 584（READ 的 13 字节应答头占位置）");     /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		shell_print(sh, "       输出是 hex，用 restore_at2get.py 转回文件");                     /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		return 0;                                                                                /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
	}

	dev = device_get_binding("UART_2");                                                              /* 按实际注册名获取UART2对象，后续通过驱动API访问。 */
	if (!dev) {                                                                                      /* UART2未注册或不可用时终止命令。 */
		shell_error(sh, "device UART_2 not found");                                              /* 报告设备绑定失败，避免继续操作空设备对象。 */
		return -1;                                                                               /* 本接口的失败、未找到或窗口到期结果，含义见函数头。 */
	}

	/* 拼 URL：'#' 还原成 '?' */
	for (i = 0; argv[1][i] != '\0' && url_len < (int)sizeof(url) - 2; i++) {                         /* 从单个URL参数逐字节复制，保留尾部余量。 */
		url[url_len] = argv[1][i];                                                               /* 复制当前URL字符。 */
		if (url[url_len] == '#') {                                                               /* 识别Shell查询字符代写。 */
			url[url_len] = '?';                                                              /* 恢复URL实际问号。 */
		}
		url_len++;                                                                               /* 推进URL有效字节数。 */
	}
	url[url_len] = '\0';                                                                             /* 补零终止，保证后续字符串打印可控。 */

	/* 读取seg_len的十进制前缀；无正值保留580，超过上限则钳位584 */
	if (argc >= 3) {                                                                                 /* 提供了可选起点或分段长度参数才解析。 */
		int v = 0;                                                                               /* 十进制参数累加值，初始0。 */
		int k;                                                                                   /* 当前关键字匹配或数值参数解析索引。 */

		for (k = 0; argv[2][k] >= '0' && argv[2][k] <= '9'; k++) {                               /* 仅接受第二参数开头的连续数字，遇非数字停止。 */
			v = v * 10 + (argv[2][k] - '0');                                                 /* 累加十进制参数，未检查溢出或尾部字符。 */
		}
		if (v > 0) {                                                                             /* 正的输入段长才覆盖默认580。 */
			seg_len = v;                                                                     /* 保存用户请求的段长，随后再限制上界。 */
		}
	}
	if (seg_len > (int)sizeof(rx) - 16) {                                                            /* 为应答头和控制字节保留16字节空间。 */
		seg_len = (int)sizeof(rx) - 16;                                                          /* 当前rx600字节，对应最大段长584。 */
	}

	shell_print(sh, "-> URL %d byte(s): %s", url_len, url);                                          /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
	shell_print(sh, "-> seg_len=%d  每段都要重配一次 URL 连接，全程约 60~90 秒",                     /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		    seg_len);

	/*
	 * 第 2~4 步的复用宏：清残留 → 发命令 → 等关键字 → 打印一行进度。
	 * 注意：这个宏在 for 循环里每段都要跑一遍，这是 GETEX 的硬前提。
	 */
/* 局部步骤宏：先清RX残留与错误，重置rxlen，按strlen逐字节发送cmd_str，调用at2_pump等待wait_tok最多wait_ms毫秒；依赖当前函数局部变量。 */
#define AT2GET_STEP(cmd_str, wait_tok, wait_ms, tag_str)                           \
	do {                                                                       \
		(void)uart_fifo_read(dev, rx, sizeof(rx));                         \
		(void)uart_err_check(dev);                                         \
		rxlen = 0;                                                         \
		txlen = (int)strlen(cmd_str);                                      \
		for (i = 0; i < txlen; i++) {                                      \
			uart_poll_out(dev, (unsigned char)(cmd_str)[i]);           \
		}                                                                  \
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, wait_tok, wait_ms); \
	} while (0)

	for (seg_idx = 0; seg_idx < 200; seg_idx++) {                                                    /* 每轮重建HTTP会话，最多执行200段，内部遇短段或错误可提前结束。 */
		shell_print(sh, "");                                                                     /* 在阶段或结果之间输出空行。 */
		shell_print(sh, "=== SEG %d  start=%d ===", seg_idx + 1, start_pos);                     /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */

		/* 2.1 requestheader 必须 0，这是 GETEX 能用的前提 */
		AT2GET_STEP("AT+HTTPCFG=\"requestheader\",0\r", "OK", 2000, NULL);                       /* 复用当前命令的清残留、发送和按毫秒窗口等待流程，参数中的字符串不作翻译。 */
		shell_print(sh, "  1/6 cfg-hdr    TX%3d RX%3d  %s", txlen, rxlen,                        /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "GOT" : "FAIL");
		if (rc != 0) {                                                                           /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
			shell_print(sh, "-> 第 1 步失败，先跑 app at2tx AT 确认模组在线");               /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			break;                                                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
		}

		/* 2.2 rspout/auto 也配 0，让模组直接把正文吐出来 */
		AT2GET_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, NULL);                         /* 复用当前命令的清残留、发送和按毫秒窗口等待流程，参数中的字符串不作翻译。 */
		shell_print(sh, "  2/6 cfg-rsp    TX%3d RX%3d  %s", txlen, rxlen,                        /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			    (rc == 0) ? "GOT" : "FAIL");

		/* 2.3 URL 长度是变量，单独拼 */
		{
			char buf[64];                                                                    /* 保存包含URL字节数的动态AT命令。 */

			(void)uart_fifo_read(dev, rx, sizeof(rx));                                       /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
			(void)uart_err_check(dev);                                                       /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
			rxlen = 0;                                                                       /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
			snprintk(buf, sizeof(buf), "AT+HTTPURL=%d,60\r", url_len);                       /* 编码URL长度及模组URL输入等待参数，命令末尾含回车。 */
			txlen = (int)strlen(buf);                                                        /* 计算动态命令实际发送字节数。 */
			for (i = 0; i < txlen; i++) {                                                    /* 按原顺序轮询发送命令中的每个字节。 */
				uart_poll_out(dev, (unsigned char)buf[i]);                               /* 发送动态URL长度命令的当前字节。 */
			}
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "CONNECT", 3000);                /* 等待最多3000 ms，确认模组进入URL透传接收。 */
			shell_print(sh, "  3/6 url-length TX%3d RX%3d  %s", txlen, rxlen,                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    (rc == 0) ? "CONNECT" : "FAIL");
			if (rc != 0) {                                                                   /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
				shell_print(sh, "-> 没等到 CONNECT，URL 长度与实际字节数可能差一个");    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				break;                                                                   /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}
		}

		/* 2.4 灌 URL 原文 + 0x1A 收尾 */
		{
			(void)uart_fifo_read(dev, rx, sizeof(rx));                                       /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
			(void)uart_err_check(dev);                                                       /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
			rxlen = 0;                                                                       /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
			for (i = 0; i < url_len; i++) {                                                  /* 按报告给模组的精确字节数发送URL。 */
				uart_poll_out(dev, url[i]);                                              /* 发送URL原文当前字节。 */
			}
			uart_poll_out(dev, 0x1A);                                                        /* 发送Ctrl+Z结束透传输入，返回AT命令阶段。 */
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "OK", 4000);                     /* 最多等待4000 ms收取URL提交结果。 */
			shell_print(sh, "  4/6 url-body   TX%3d RX%3d  %s", url_len + 1, rxlen,          /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    (rc == 0) ? "OK" : "FAIL");
			if (rc == -1) {                                                                  /* 此阶段仅超时判失败，缓冲满是否接受沿用原逻辑。 */
				shell_print(sh, "-> URL 灌完没回 OK，长度可能和实际字节数对不上");       /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				break;                                                                   /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}
		}

		/* 2.5 紧跟 GETEX，中间绝不能夹 GET 或 READ */
		{
			char cmd[48];                                                                    /* 保存包含起点和段长的GETEX命令。 */

			(void)uart_fifo_read(dev, rx, sizeof(rx));                                       /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
			(void)uart_err_check(dev);                                                       /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
			rxlen = 0;                                                                       /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
			snprintk(cmd, sizeof(cmd), "AT+HTTPGETEX=10,%d,%d\r",                            /* 把当前Range起点和段长写入有界命令缓冲。 */
				 start_pos, seg_len);
			txlen = (int)strlen(cmd);                                                        /* 取得动态范围命令实际长度。 */
			for (i = 0; i < txlen; i++) {                                                    /* 按原顺序轮询发送命令中的每个字节。 */
				uart_poll_out(dev, (unsigned char)cmd[i]);                               /* 发送GETEX动态命令当前字节。 */
			}
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "+HTTPGETEX:", 12000);           /* 最多等待12秒寻找范围请求异步结果。 */
			if (rc == 0) {                                                                   /* 结果前缀已出现，继续采集余下字段。 */
				/* 关键字只说明上报开始，再补 300ms 把三字段收全 */
				(void)at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 300);             /* 追加300 ms接收窗口以收全结果字段，忽略无关键字时的-1。 */
			}
			shell_print(sh, "  5/6 getex      TX%3d RX%3d  %s", txlen, rxlen,                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    (rc == 0) ? "GOT" : "TIMEOUT");
			if (rc != 0) {                                                                   /* 关键字未命中或缓冲已满时按本阶段失败处理。 */
				shell_print(sh, "-> 没等到 +HTTPGETEX 上报");                            /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				break;                                                                   /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}

			/*
			 * 解析 +HTTPGETEX: <err>,<httpcode>,<len>
			 * 标记 11 字节；err 可能是负数（-1 = 客户端错误），支持前导 '-'
			 */
			gx_err = 99;                                                                     /* 新段先把模组错误码标为尚未解析。 */
			gx_code = -1;                                                                    /* 清除上一段HTTP状态码，防止误用旧结果。 */
			gx_len = -1;                                                                     /* 清除上一段长度，未知时不据其截断。 */
			{
				int k;                                                                   /* 当前关键字匹配或数值参数解析索引。 */

				for (k = 0; k + 11 <= rxlen; k++) {                                      /* 保证11字节标记可完整访问后逐位置匹配。 */
					int j;                                                           /* 数字字段扫描位置或替换字符索引。 */
					int idx = 0;                                                     /* 当前正在解析的字段序号，最多三个。 */
					int v[3] = {0, 0, 0};                                            /* 三个十进制字段的累加值，初始0。 */
					int neg[3] = {0, 0, 0};                                          /* 每字段负号标志，用于GETEX错误码解析。 */
					int had[3] = {0, 0, 0};                                          /* 每字段是否至少读到一个数字，区分缺失与数值0。 */

					/* 逐字节匹配HTTP结果标记；未命中即换下一个候选起点。 */
					if (!(rx[k] == '+' && rx[k + 1] == 'H' && rx[k + 2] == 'T' &&
					      rx[k + 3] == 'T' && rx[k + 4] == 'P' &&
					      rx[k + 5] == 'G' && rx[k + 6] == 'E' &&
					      rx[k + 7] == 'T' && rx[k + 8] == 'E' &&
					      rx[k + 9] == 'X' && rx[k + 10] == ':')) {
						continue;                                                /* 当前候选不是完整结果标记，继续搜索下一个位置。 */
					}

					j = k + 11;                                                      /* 跳过11字节HTTPGETEX标记，开始解析字段。 */
					while (j < rxlen && idx < 3) {                                   /* 在接收范围内读取最多三个字段，非数字/分隔符时停止。 */
						unsigned char c = rx[j];                                 /* 读取当前响应解析字节，避免字符串函数越界。 */

						if (c == '-') {                                          /* 识别当前字段的负号，主要供客户端错误码使用。 */
							neg[idx] = 1;                                    /* 记录当前字段需要取负值。 */
							j++;                                             /* 消费当前响应字符，推进扫描位置。 */
						} else if (c >= '0' && c <= '9') {                       /* 负号检查之后处理十进制数字。 */
							v[idx] = v[idx] * 10 + (int)(c - '0');           /* 按十进制扩展字段值，原实现未检查整数溢出。 */
							had[idx] = 1;                                    /* 标记该字段已有数值内容。 */
							j++;                                             /* 消费当前响应字符，推进扫描位置。 */
						} else if (c == ',') {                                   /* 逗号结束当前字段，切换到下一项。 */
							idx++;                                           /* 推进错误码、HTTP状态码、长度三字段索引。 */
							j++;                                             /* 消费当前响应字符，推进扫描位置。 */
						} else {
							break;                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
						}
					}

					gx_err  = had[0] ? (neg[0] ? -v[0] : v[0]) : 99;                 /* 恢复错误码符号，缺失时保留哨兵99。 */
					gx_code = had[1] ? v[1] : -1;                                    /* 保存HTTP状态码，缺失时为-1。 */
					gx_len  = had[2] ? v[2] : -1;                                    /* 保存结果长度，未报告时为-1。 */
					break;                                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
				}
			}
			shell_print(sh, "  => err=%d  http=%d  len=%d", gx_err, gx_code, gx_len);        /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		}

		/* 416表示请求偏移越界，按本命令判据结束；后续短段、错误及200段上限也会终止 */
		if (gx_code == 416) {                                                                    /* Range起点越界，按本命令规则认为资源结束。 */
			shell_print(sh, "-> 416：start=%d 超出资源末尾，全文已取完", start_pos);         /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			break;                                                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
		}
		/* 只接受 206。200 说明服务器忽略了 Range，继续循环会重复取全文 */
		if (gx_code != 206) {                                                                    /* 仅接受部分内容206，其他状态停止分段以免重复下载全文。 */
			shell_print(sh, "-> http=%d 不是 206，服务器没按 Range 返回，停止",              /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    gx_code);
			shell_print(sh, "   若这里是 200，说明该服务器不支持 Range");                    /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			break;                                                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
		}

		/* 2.6 取数据 */
		{
			(void)uart_fifo_read(dev, rx, sizeof(rx));                                       /* 清理上一阶段残留RX字节，避免误匹配旧关键字。 */
			(void)uart_err_check(dev);                                                       /* 读取并处理驱动暴露的UART错误状态，原流程忽略返回值。 */
			rxlen = 0;                                                                       /* 开始新接收阶段，旧缓冲内容不再计为有效。 */
			txlen = (int)strlen("AT+HTTPREAD=10\r");                                         /* 取得读取正文命令的字节数。 */
			for (i = 0; i < txlen; i++) {                                                    /* 按原顺序轮询发送命令中的每个字节。 */
				uart_poll_out(dev, "AT+HTTPREAD=10\r"[i]);                               /* 发送读取正文命令当前字节。 */
			}
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 4000);                     /* 接收最长4000 ms或直到缓冲满，不按正文内容匹配关键字。 */
			rd_error = 0;                                                                    /* 每段读正文前清除上次ERROR标志。 */
			if (rxlen > 0 && rxlen <= 16 && at2_rx_has_tok(rx, rxlen, "ERROR") == 0) {       /* 短响应含ERROR时按模组拒绝读取处理；避免大正文中的同名字符串误判。 */
				rd_error = 1;                                                            /* 记录读取阶段返回了ERROR。 */
			}
			shell_print(sh, "  6/6 http-read  TX%3d RX%3d  %s", txlen, rxlen,                /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    rd_error ? "ERROR" :
				    (rc == 0) ? "END" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
			if (rd_error || rxlen <= 0) {                                                    /* 模组拒绝或没有数据时放弃本段。 */
				shell_print(sh, "-> READ 没拿到数据，本段放弃");                         /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				break;                                                                   /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}
		}

		/*
		 * 剥离应答头：READ 的数据形如
		 *   \r\n\r\nCONNECT\r\n<正文>
		 * 找 "CONNECT" 七字节，再跳过它后面的 \r 和 \n，剩下就是正文。
		 * 正文里也可能出现 CONNECT，所以只取第一个。
		 */
		body_off = 0;                                                                            /* 默认从接收区起点算正文，找到CONNECT后再后移。 */
		for (i = 0; i + 7 <= rxlen; i++) {                                                       /* 搜索首个完整7字节CONNECT标记。 */
			if (rx[i] == 'C' && rx[i + 1] == 'O' && rx[i + 2] == 'N' &&
			    rx[i + 3] == 'N' && rx[i + 4] == 'E' && rx[i + 5] == 'C' &&
			    rx[i + 6] == 'T') {
				body_off = i + 7;                                                        /* 正文候选起点先跳过CONNECT七字节。 */
				break;                                                                   /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
			}
		}
		while (body_off < rxlen && (rx[body_off] == '\r' || rx[body_off] == '\n')) {             /* 跳过候选起点处连续CR/LF，也可能跳过正文开头同类字节。 */
			body_off++;                                                                      /* 推进正文起点，始终不超过已接收长度。 */
		}
		body_len = rxlen - body_off;                                                             /* 剩余字节暂计为正文，后续剥离长度之外的应答尾。 */

		/*
		 * 用服务器声明的 gx_len 截断正文 —— 这是最权威的长度。
		 *
		 * 实测：请求 read_len=580，模组实际回 RX=598，剥掉 13 字节 CONNECT 头
		 * 后 body_len=585，多出来的 5 字节是模组追加的应答残片 "\r\nOK\r"
		 * （完整时是 6 字节 "\r\nOK\r\n"，被 rx[600] 截断时只剩 5 字节）。
		 * 而 gx_len 稳定报 580，正是真实正文字节数。
		 *
		 * 必须切掉：否则 start_pos 会按 585 推进，每段跳过 5 字节真实内容，
		 * 30 段累计丢约 142 字节（已实测确认）。
		 */
		if (gx_len >= 0 && gx_len < body_len) {                                                  /* 报告长度更短时丢弃多收的响应尾部。 */
			shell_print(sh, "   (按服务器声明截断 %d -> %d，切掉应答残片)",                  /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    body_len, gx_len);
			body_len = gx_len;                                                               /* 将正文限制到模组报告的本段长度。 */
		}

		/* 随后仍检查尾部模式（并非只在gx_len不可用时）：剥除完整的 "\r\nOK\r\n" */
		/* 若尾部六字节正好是结果码结束串则剥除；正文同样字节也可能被误判。 */
		if (body_len >= 6 &&
		    rx[body_off + body_len - 6] == '\r' &&
		    rx[body_off + body_len - 5] == '\n' &&
		    rx[body_off + body_len - 4] == 'O' &&
		    rx[body_off + body_len - 3] == 'K' &&
		    rx[body_off + body_len - 2] == '\r' &&
		    rx[body_off + body_len - 1] == '\n') {
			body_len -= 6;                                                                   /* 匹配完整CR/LF OK CR/LF后剥除这6字节；当前代码即使gx_len有效也执行。 */
			shell_print(sh, "   (剥掉尾部 6 字节 \\r\\nOK\\r\\n)");                          /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
		}

		if (body_len <= 0) {                                                                     /* 剥头尾后无正文则终止循环。 */
			shell_print(sh, "-> 剥完应答头没有正文，本段为空，停止");                        /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
			break;                                                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
		}

		/*
		 * 打 hex：每行 16 字节，前缀是全局偏移。
		 * 用 hex 而不是原文，是为了丢字节时能一眼看出来（偏移会跳）。
		 */
		shell_print(sh, "--- DATA start=%d len=%d ---", start_pos, body_len);                    /* 输出段起点和本段有效字节数作为还原边界。 */
		{
			int p;                                                                           /* 当前段内十六进制输出行起点。 */

			for (p = 0; p < body_len; p += 16) {                                             /* 每行最多16字节，段尾不足16字节也输出。 */
				char line[120];                                                          /* 构造单行十六进制文本，包含偏移与完整性标记。 */
				int n = 0;                                                               /* 当前文本行已写入长度，供后续有界追加。 */
				int q;                                                                   /* 当前输出行中的字节索引。 */

				n += snprintk(line + n, sizeof(line) - n, "[%06d]", total + p);          /* 输出相对已接收正文总量的全局偏移。 */
				for (q = 0; q < 16 && p + q < body_len; q++) {                           /* 限制一行16字节且不越过本段正文。 */
					n += snprintk(line + n, sizeof(line) - n, " %02X",               /* 追加当前正文字节的两位十六进制编码，并更新行长度。 */
						      rx[body_off + p + q]);
				}
				/* 行尾加 '|' 作完整性标记：串口丢字节会把 '|' 一起丢掉，
				 * 还原脚本看到没有 '|' 结尾就知道这一行不完整。
				 * 实测 SEG 14 丢 2 字节、SEG 18 丢 1 字节，偏移却没跳，
				 * 靠偏移查不出来。 */
				n += snprintk(line + n, sizeof(line) - n, " |");                         /* 追加行尾标记，供脚本检查串口日志是否完整。 */
				shell_print(sh, "%s", line);                                             /* 提交完整十六进制行。 */
			}
		}
		shell_print(sh, "--- END start=%d len=%d ---", start_pos, body_len);                     /* 输出段结束标记，与段头配对。 */

		total += body_len;                                                                       /* 累计已打印正文字节数，不包含剥除的应答头尾。 */
		done_seg++;                                                                              /* 仅已处理并输出正文的段计为完成。 */

		/*
		 * 判"到末尾"必须看服务器声明的段长度 gx_len，不能看本地收到的
		 * body_len。原因：body_len = rxlen - body_off，而 rxlen 被 rx[600]
		 * 截断，那 13 字节应答头也占位置，所以 body_len 天然比 seg_len 少
		 * 13 左右。用 body_len 判断会把每一段都当成末尾（已实测踩过）。
		 */
		if (gx_len >= 0 && gx_len < seg_len) {                                                   /* 模组报告本段短于请求时判断最后一段。 */
			shell_print(sh, "-> 服务器只给了 %d 字节（请求 %d），已到资源末尾",              /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
				    gx_len, seg_len);
			start_pos += body_len;                                                           /* 按实际保留正文长度推进下一次Range偏移，丢字节时无法自动补齐。 */
			break;                                                                           /* 本阶段已完成或出现失败，退出当前最内层循环，后续行为由外层流程决定。 */
		}

		start_pos += body_len;                                                                   /* 按实际保留正文长度推进下一次Range偏移，丢字节时无法自动补齐。 */

		/* 段间留一点时间给 printk 的 DMA 排空，防止 2048 字节缓冲丢数据 */
		k_busy_wait(8000);                                                                       /* 忙等8000微秒给串口输出排空时间，不是8秒。 */
	}

	shell_print(sh, "");                                                                             /* 在阶段或结果之间输出空行。 */
	shell_print(sh, "=== 总计 %d 段，%d 字节 ===", done_seg, total);                                 /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */
	shell_print(sh, "   用还原脚本把这批 hex 转回文件；缺段时偏移会跳变");                           /* 输出本阶段的命令用法、接收结果或判定依据；日志文本保持原值。 */

#undef AT2GET_STEP

	return 0;                                                                                        /* 按本接口约定结束；Shell命令多数不通过此值报告协议成功。 */
}

#endif                                                                                                           /* CONFIG_UART_2 */
SHELL_STATIC_SUBCMD_SET_CREATE(acts_app_cmds,                                                                    /* 创建静态子命令表，SPI、UART及其他诊断共用app根入口。 */
#if defined(CONFIG_SPI_SLAVE) && IS_ENABLED(CONFIG_SPI_2)                                                        /* 只为启用 SPI2 从机的固件编译测试命令。 */
    SHELL_CMD(spi2test, NULL, "send 16 zero bytes (alias of spi2test0)", shell_spi2test0),                       /* 保留旧命令别名，配合固定图案主机使用。 */
    SHELL_CMD(spi2test0, NULL, "send 16 zero bytes over SPI2", shell_spi2test0),                                 /* 注册旧版单帧全零图案命令。 */
    SHELL_CMD(spi2test1, NULL, "send B0 through BF over SPI2", shell_spi2test1),                                 /* 注册旧版单帧 B0～BF 图案命令。 */
    SHELL_CMD(spi2stress, NULL, "CRC16 continuous test [1..1000], default 100", shell_spi2stress),               /* 注册当前连续 CRC/序号测试命令。 */
#endif                                                                                                           /* 结束当前条件编译分支。 */
	SHELL_CMD(set_config, NULL, "set system config", shell_set_config),                                      /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
	SHELL_CMD(input, NULL, "input key event", shell_input_key_event),                                        /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
#ifdef CONFIG_MONKEY_TEST
	SHELL_CMD(monkey, NULL, "monkey test ", shell_monkey_event),                                             /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
#endif
	SHELL_CMD(btinfo, NULL, "dump bt info", shell_dump_bt_info),                                             /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
#ifdef CONFIG_UART_2
	SHELL_CMD(at2tx, NULL, "send any AT command to the module over UART2", shell_at2tx),                     /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
	SHELL_CMD(at2http, NULL, "run a full HTTP GET over UART2 and show the response head", shell_at2http),    /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
	SHELL_CMD(at2gex, NULL, "range GET over UART2, judge if server supports HTTP Range", shell_at2gex),      /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
	SHELL_CMD(at2get, NULL, "loop-generic range GET, dump hex to console", shell_at2get),                    /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
#endif
#ifdef CONFIG_UI_MANAGER
	SHELL_CMD(uistack, NULL, "dump ui stack", shell_dump_uistack),                                           /* 将此子命令绑定到右侧处理函数；仅在所在CONFIG分支启用时注册。 */
#endif
	SHELL_SUBCMD_SET_END
);

/**
  * @brief  处理app根命令的帮助或未识别子命令。
  * @param  shell：[输入] 有效Shell实例，用于帮助和错误输出。
  * @param  argc：[输入] 参数数量，至少1；只有根命令时为1。
  * @param  argv：[输入] 至少argc个有效字符串指针，框架持有；未知参数时读取argv[1]。
  * @retval SHELL_CMD_HELP_PRINTED：已打印帮助；-EINVAL：未知子命令。
  */
static int cmd_acts_app(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1) {                                                     /* 只有app根命令时进入帮助路径。 */
		shell_help(shell);                                           /* 显示根命令帮助及可用子命令。 */
		return SHELL_CMD_HELP_PRINTED;                               /* 通知Shell框架帮助已经输出。 */
	}

	shell_error(shell, "%s unknown parameter: %s", argv[0], argv[1]);    /* 报告未匹配的子命令。 */

	return -EINVAL;                                                      /* 根命令收到未知参数。 */
}

SHELL_CMD_REGISTER(app, &acts_app_cmds, "Application shell commands", cmd_acts_app);    /* 注册app根命令，框架按子命令表分派到对应函数。 */
#endif
