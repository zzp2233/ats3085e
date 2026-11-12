/*
 * Copyright (c) 2019 Actions Semi Co., Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief system app shell.
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
static int shell_input_key_event(const struct shell *shell, size_t argc, char *argv[])
{
	if (argv[1] != NULL) {
		uint32_t key_event;
		key_event = strtoul(argv[1], (char **) NULL, 0);
		sys_event_report_input(key_event);
	}
	return 0;
}
#ifdef CONFIG_MONKEY_TEST
static int shell_monkey_event(const struct shell *shell, size_t argc, char *argv[])
{
	int len = strlen(argv[1]);

	if (!strncmp(argv[1], "start", len)) {
		if (argv[2] != NULL) {
			system_monkey_test_start(strtoul(argv[2], (char **) NULL, 0));
		} else {
			system_monkey_test_start(100);
		}
	} else if(!strncmp(argv[1], "stop", len)){
		system_monkey_test_stop();
	}

	return 0;
}
#endif
static int shell_set_config(const struct shell *shell, size_t argc, char *argv[])
{
#ifdef CONFIG_PROPERTY
	int ret = 0;

	if (argc < 2) {
		ret = property_set(argv[1], argv[1], 0);
	} else {
		ret = property_set(argv[1], argv[2], strlen(argv[2]));
	}

	if (ret < 0) {
		ret = -1;
	} else {
		property_flush(NULL);
	}
#endif

	SYS_LOG_INF("set config %s : %s ok\n", argv[1], argv[2]);
	return 0;
}

static int shell_dump_bt_info(const struct shell *shell, size_t argc, char *argv[])
{
#ifdef CONFIG_BT_MANAGER
	bt_manager_dump_info();
#endif
	return 0;
}

#ifdef CONFIG_RECORD_APP
extern void recorder_sample_rate_set(uint8_t sample_rate_kh);
static int shell_set_record_sample_rate(const struct shell *shell, size_t argc, char *argv[])
{
	if (argv[1] != NULL) {
		recorder_sample_rate_set(atoi(argv[1]));
	}
	return 0;
}
#endif

#ifdef CONFIG_UI_MANAGER
static int shell_dump_uistack(const struct shell *shell, size_t argc, char *argv[])
{
	app_ui_stack_dump();
	return 0;
}
#endif /*CONFIG_UI_MANAGER*/

#ifdef CONFIG_UART_2
/*
 * UART2 用于外接 4G 模组（UMA503-V7）的 AT 指令口。
 * 本段提供一个 shell 命令，做最小收发验证。
 */



/*
 * 函数功能：扫描已收到的 RX 字节，判断模组是否已经把最终结果码送完了
 * 执行流程：
 *   1. 只看缓冲区最后 16 个字节，结果码一定跟在数据的尾部
 *   2. 找连续的 "OK" 两字符，找到即认为本条指令应答结束
 * 说明：不能用"末尾是换行就收尾"来判，因为模组应答以 "\r\nOK\r\n" 开头，
 *       "\r\n" 末尾就是换行，那样会在 "OK" 还没到时提前退出
 * 返回：1 = 已收到 OK，0 = 还没
 */
static int at2_rx_has_ok(const unsigned char *buf, int len)
{
	int i;
	int from = (len > 16) ? (len - 16) : 0;

	for (i = from; i + 1 < len; i++) {
		if (buf[i] == 'O' && buf[i + 1] == 'K') {
			return 1;
		}
	}
	return 0;
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
static int at2_rx_has_tok(const unsigned char *buf, int len, const char *tok)
{
	int toklen = (int)strlen(tok);
	int pos;

	if (toklen <= 0 || len < toklen) {
		return -1;
	}

	for (pos = len - toklen; pos >= 0; pos--) {
		int k;
		int hit = 1;

		for (k = 0; k < toklen; k++) {
			if (buf[pos + k] != (unsigned char)tok[k]) {
				hit = 0;
				break;
			}
		}
		if (hit) {
			return 0;
		}
	}

	return -1;
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
static int at2_pump(const struct device *dev, unsigned char *rx, int cap,
		    int *lenp, const char *tok, int ms)
{
	unsigned int start = k_uptime_get_32();

	while ((int)(k_uptime_get_32() - start) < ms) {
		/*
		 * 缓冲留两个字节余量就提前退出，别再进 FIFO 读。
		 * 否则读满后 cap - *lenp - 1 变成 0，uart_fifo_read 读 0 字节，
		 * 一路空转到超时，显示 TIMEOUT 其实是已经收满了。
		 */
		if (*lenp >= cap - 2) {
			return 1;
		}
		if (uart_irq_rx_ready(dev)) {
			int n = uart_fifo_read(dev, &rx[*lenp], cap - *lenp - 2);

			if (n > 0) {
				*lenp += n;
			}
		} else {
			if (tok && *lenp > 0 && at2_rx_has_tok(rx, *lenp, tok) == 0) {
				return 0;
			}
			k_busy_wait(100);
		}
	}

	if (tok && *lenp > 0 && at2_rx_has_tok(rx, *lenp, tok) == 0) {
		return 0;
	}

	return -1;
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
static int shell_at2tx(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	static unsigned char txbuf[200];
	static unsigned char rxbuf[512];
	int txlen = 0;
	int rxlen = 0;
	int i;
	unsigned int start;

	dev = device_get_binding("UART_2");
	if (!dev) {
		shell_error(sh, "device UART_2 not found");
		return -1;
	}

	if (argc < 2) {
		shell_print(sh, "usage: app at2tx <at-command>   e.g. app at2tx AT+CSQ");
		shell_print(sh, "       shell 会把 '?' 当通配符吃掉，查询命令改用 '#' 代写：");
		shell_print(sh, "       app at2tx AT+CPIN#   ->  实际发送 AT+CPIN?");
		return 0;
	}

	/* 清掉上一条指令可能残留的尾巴 */
	(void)uart_fifo_read(dev, rxbuf, sizeof(rxbuf));
	(void)uart_err_check(dev);

	/* 拼命令：参数之间用空格，末尾补 CR */
	for (i = 1; i < argc; i++) {
		int n = strlen(argv[i]);

		if (i > 1) {
			if (txlen >= (int)sizeof(txbuf) - 2) {
				break;
			}
			txbuf[txlen++] = ' ';
		}
		if (txlen + n >= (int)sizeof(txbuf) - 2) {
			break;
		}
		memcpy(&txbuf[txlen], argv[i], n);
		/*
		 * Zephyr shell 把 '?' 当 glob 通配符吞掉（引号也拦不住），
		 * AT 查询命令一律改用 '#' 代写，这里转回 '?' 再发给模组。
		 */
		{
			int j;

			for (j = 0; j < n; j++) {
				if (txbuf[txlen + j] == '#') {
					txbuf[txlen + j] = '?';
				}
			}
		}
		txlen += n;
	}
	txbuf[txlen++] = '\r';

	shell_print(sh, "TX %d byte(s) -> UART2", txlen);
	for (i = 0; i < txlen; i++) {
		uart_poll_out(dev, txbuf[i]);
	}

	/* 500ms 接收窗口：模组回显 + 结果码通常几十 ms 内就回来 */
	rxlen = 0;
	start = k_uptime_get_32();
	while ((k_uptime_get_32() - start) < 500) {
		if (uart_irq_rx_ready(dev)) {
			int n = uart_fifo_read(dev, &rxbuf[rxlen], (int)sizeof(rxbuf) - rxlen - 1);

			if (n > 0) {
				rxlen += n;
			}
		} else {
			/* 收到 OK 说明本轮应答已完整，可以提前收尾；否则继续等到 500ms */
			if (rxlen > 0 && at2_rx_has_ok(rxbuf, rxlen)) {
				break;
			}
			k_busy_wait(100);
		}
	}

	shell_print(sh, "RX %d byte(s):", rxlen);
	for (i = 0; i < rxlen; i++) {
		shell_print(sh, "  [%03d] 0x%02X  %c", i, rxbuf[i],
			    (rxbuf[i] >= 0x20 && rxbuf[i] < 0x7F) ? rxbuf[i] : '.');
	}
	if (rxlen == 0) {
		shell_print(sh, "-> nothing received. check: module powered up?");
		shell_print(sh, "   53(TX) -> module UART_RXD(pin17), 54(RX) <- module UART_TXD(pin18), GND common");
	}

	return 0;
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
static int shell_at2http(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	static unsigned char url[200];
	static unsigned char rx[600];
	int url_len = 0;
	int txlen;
	int rxlen;
	int rc;
	int i;
	int http_err = -1;      /* +HTTPGET 第 1 个字段：错误码，0 = 无错误      */
	int http_code = -1;     /* +HTTPGET 第 2 个字段：HTTP 状态码，200 = 成功 */
	int http_blen = -1;     /* +HTTPGET 第 3 个字段：服务器声明的正文长度    */

	if (argc < 2) {
		shell_print(sh, "usage: app at2http <url>   e.g. app at2http http://www.baidu.com");
		shell_print(sh, "       URL 里的 '?' 请写成 '#'，shell 会吃掉问号");
		return 0;
	}

	dev = device_get_binding("UART_2");
	if (!dev) {
		shell_error(sh, "device UART_2 not found");
		return -1;
	}

	/* 拼 URL：多个 argv 用空格接回，'#' 还原成 '?' */
	for (i = 1; i < argc && url_len < (int)sizeof(url) - 2; i++) {
		int n = (int)strlen(argv[i]);
		int k;

		if (i > 1) {
			url[url_len++] = ' ';
		}
		if (url_len + n >= (int)sizeof(url) - 1) {
			break;
		}
		memcpy(&url[url_len], argv[i], n);
		for (k = 0; k < n; k++) {
			if (url[url_len + k] == '#') {
				url[url_len + k] = '?';
			}
		}
		url_len += n;
	}
	url[url_len] = '\0';

	shell_print(sh, "-> URL %d byte(s): %s", url_len, url);

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

	AT2HTTP_STEP("AT+HTTPCFG=\"requestheader\",0\r", "OK", 2000, "1/6 requestheader");
	if (rc == -1) {
		shell_print(sh, "-> 第 1 步就超时，先跑 app at2tx AT+HTTPCFG# 确认模组在");
		return -1;
	}

	AT2HTTP_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, "2/6 rspout/auto");

	/* 第 3 步要报 URL 字节数，命令是动态的，单独拼 */
	{
		char buf[64];

		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		snprintk(buf, sizeof(buf), "AT+HTTPURL=%d,60\r", url_len);
		txlen = (int)strlen(buf);
		for (i = 0; i < txlen; i++) {
			uart_poll_out(dev, (unsigned char)buf[i]);
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "CONNECT", 3000);
		shell_print(sh, "  3/6 url-length    TX%3d RX%3d  %s", txlen, rxlen,
			    (rc == 0) ? "CONNECT" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc != 0) {
			shell_print(sh, "-> 没等到 CONNECT，URL 长度不对或参数格式不对");
			return -1;
		}
	}

	/* 第 4 步：URL 原文灌进去 + 0x1A 收尾 */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		for (i = 0; i < url_len; i++) {
			uart_poll_out(dev, url[i]);
		}
		uart_poll_out(dev, 0x1A);
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "OK", 4000);
		shell_print(sh, "  4/6 url-body      TX%3d RX%3d  %s", url_len + 1, rxlen,
			    (rc == 0) ? "OK" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc == -1) {
			shell_print(sh, "-> URL 灌进去后没回 OK，多半是长度和实际字节数对不上");
			return -1;
		}
	}

	/* 第 5 步：发 GET。模组先回 OK，稍后再报 +HTTPGET: */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		txlen = (int)strlen("AT+HTTPGET=10\r");
		for (i = 0; i < txlen; i++) {
			uart_poll_out(dev, "AT+HTTPGET=10\r"[i]);
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "+HTTPGET:", 12000);
		/* 关键字只说明结果码开始上报，再多收 300ms 把 0,200,xxxx 收全 */
		if (rc == 0) {
			(void)at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 300);
		}
		shell_print(sh, "  5/6 http-get      TX%3d RX%3d  %s", txlen, rxlen,
			    (rc == 0) ? "GOT +HTTPGET" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		for (i = 0; i < rxlen; i++) {
			shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],
				    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
		}
		if (rc != 0) {
			shell_print(sh, "-> 没等到 +HTTPGET，先确认 AT+CGPADDR 已经有 IP");
			return -1;
		}

		/*
		 * 解析 +HTTPGET: <err>,<httpcode>[,<body_len>]
		 * 注意 responseheader 默认 0，模组不会回 "HTTP/1.1 200 OK" 这种响应头行，
		 * 状态码只在这条 +HTTPGET 上报里出现，所以必须把它抠出来，不能靠找头行判断。
		 * 手工解析不用 sscanf，Zephyr 的 libc 不一定带浮点/%d 的完整实现。
		 */
		{
			int k;

			for (k = 0; k + 9 <= rxlen; k++) {
				int j;
				int idx = 0;
				int v[3] = {0, 0, 0};
				int had[3] = {0, 0, 0};

				/* 定位 "+HTTPGET:" 这个 9 字节标记 */
				if (!(rx[k] == '+' && rx[k + 1] == 'H' && rx[k + 2] == 'T' &&
				      rx[k + 3] == 'T' && rx[k + 4] == 'P' &&
				      rx[k + 5] == 'G' && rx[k + 6] == 'E' &&
				      rx[k + 7] == 'T' && rx[k + 8] == ':')) {
					continue;
				}

				/* 标记后面按 数字 , 数字 , 数字 的方式读，遇到 \r\n 就停 */
				j = k + 9;
				while (j < rxlen && idx < 3) {
					unsigned char c = rx[j];

					if (c >= '0' && c <= '9') {
						v[idx] = v[idx] * 10 + (int)(c - '0');
						had[idx] = 1;
						j++;
					} else if (c == ',') {
						idx++;
						j++;
					} else {
						break;
					}
				}

				http_err  = had[0] ? v[0] : -1;
				http_code = had[1] ? v[1] : -1;
				http_blen = had[2] ? v[2] : -1;
				break;
			}

			if (http_code < 0) {
				shell_print(sh, "-> 收到 +HTTPGET 但没解析出状态码，报文可能被截断");
			} else {
				shell_print(sh, "  => err=%d  http=%d  body=%d byte(s)",
					    http_err, http_code, http_blen);
			}
		}
	}

	/* 第 6 步：读正文。CONNECT 之后是裸数据流，收满缓冲就停 */
	{
		char text[220];
		int tlen = 0;

		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		txlen = (int)strlen("AT+HTTPREAD=10\r");
		for (i = 0; i < txlen; i++) {
			uart_poll_out(dev, "AT+HTTPREAD=10\r"[i]);
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 4000);
		shell_print(sh, "  6/6 http-read     TX%3d RX%3d  %s", txlen, rxlen,
			    (rc == 0) ? "END" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");

		for (i = 0; i < rxlen; i++) {
			shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],
				    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
		}

		/* 把可读字节连成一行，肉眼扫一眼就能看到正文开头是什么 */
		for (i = 0; i < rxlen && tlen < (int)sizeof(text) - 1; i++) {
			unsigned char c = rx[i];

			if (c >= 0x20 && c < 0x7F) {
				text[tlen++] = (char)c;
			} else if (c == '\r' || c == '\n') {
				text[tlen++] = ' ';
			}
		}
		text[tlen] = '\0';
		shell_print(sh, "  TEXT: %s", text);

		/*
		 * 判据用第 5 步抠出来的状态码，不再找 HTTP/1.x。
		 * responseheader=0 时模组 CONNECT 后直接吐 body，压根没有响应头行，
		 * 以前按"有没有 HTTP/1."判断永远为假，会把成功当成失败。
		 */
		if (http_code == 200 && rxlen > 0) {
			shell_print(sh, "-> HTTP 200 通过：服务器返回 %d 字节正文，本次收下 %d 字节",
				    http_blen, rxlen);
			shell_print(sh, "-> 缓冲只有 600 字节，收满即停；要收全文需补 UART2 RX DMA");
		} else if (http_code >= 0) {
			shell_print(sh, "-> 服务器回的是 HTTP %d（err=%d），不是 200，查 URL 或源站",
				    http_code, http_err);
		} else if (rxlen > 0) {
			shell_print(sh, "-> 收到 %d 字节但没解析到状态码，先看上面 5/6 的 hex", rxlen);
		} else {
			shell_print(sh, "-> 一个字节都没收到，检查 AT+HTTPREAD 的 rsptime");
		}
	}

#undef AT2HTTP_STEP

	return 0;
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
static int shell_at2gex(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	static unsigned char url[200];
	static unsigned char rx[600];
	int url_len = 0;
	int txlen;
	int rxlen;
	int rc;
	int i;
	int start_pos = 0;      /* 从正文第几字节开始读，默认 0                    */
	int read_len = 598;     /* 本次要读多少字节，默认填满缓冲                   */
	int gx_err = 99;        /* +HTTPGETEX 第 1 字段：错误码，0 = 成功           */
	int gx_code = -1;       /* +HTTPGETEX 第 2 字段：HTTP 状态码                */
	int gx_len = -1;        /* +HTTPGETEX 第 3 字段：服务器声明总长度            */
	int rd_len = 0;         /* READ 实际收到多少字节                            */
	int rd_error = 0;       /* READ 回的是 ERROR 而不是数据                     */

	if (argc < 2) {
		shell_print(sh, "usage: app at2gex <url> [start] [len]");
		shell_print(sh, "   e.g. app at2gex http://www.baidu.com/ 0 600");
		shell_print(sh, "        app at2gex http://www.baidu.com/ 600 600");
		shell_print(sh, "       URL 里的 '?' 请写成 '#'");
		return 0;
	}

	dev = device_get_binding("UART_2");
	if (!dev) {
		shell_error(sh, "device UART_2 not found");
		return -1;
	}

	/* 拼 URL：'#' 还原成 '?' */
	for (i = 0; argv[1][i] != '\0' && url_len < (int)sizeof(url) - 2; i++) {
		url[url_len] = argv[1][i];
		if (url[url_len] == '#') {
			url[url_len] = '?';
		}
		url_len++;
	}
	url[url_len] = '\0';

	/* 转 start / len 两个整数参数：只认 0~9 */
	if (argc >= 3) {
		int v = 0;
		int k;

		for (k = 0; argv[2][k] >= '0' && argv[2][k] <= '9'; k++) {
			v = v * 10 + (argv[2][k] - '0');
		}
		start_pos = v;
	}
	if (argc >= 4) {
		int v = 0;
		int k;

		for (k = 0; argv[3][k] >= '0' && argv[3][k] <= '9'; k++) {
			v = v * 10 + (argv[3][k] - '0');
		}
		read_len = v;
	}
	if (read_len <= 0 || read_len > (int)sizeof(rx) - 2) {
		read_len = (int)sizeof(rx) - 2;
	}

	shell_print(sh, "-> URL %d byte(s): %s   start=%d  len=%d",
		    url_len, url, start_pos, read_len);

	/*
	 * 第 2~5 步：配 HTTPCFG 和 URL。
	 * 用局部宏把"清残留 → 发命令 → 等关键字"三件事压成一行。
	 */
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

	AT2GEX_STEP("AT+HTTPCFG=\"requestheader\",0\r", "OK", 2000, "1/6 cfg-hdr");
	if (rc != 0) {
		shell_print(sh, "-> 第 1 步失败，先跑 app at2tx AT 确认模组在线");
		return -1;
	}

	AT2GEX_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, "2/6 cfg-rsp");

	/* 第 4 步：URL 长度是变量，单独拼 */
	{
		char buf[64];

		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		snprintk(buf, sizeof(buf), "AT+HTTPURL=%d,60\r", url_len);
		txlen = (int)strlen(buf);
		for (i = 0; i < txlen; i++) {
			uart_poll_out(dev, (unsigned char)buf[i]);
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "CONNECT", 3000);
		shell_print(sh, "  3/6 url-length TX%3d RX%3d  %s", txlen, rxlen,
			    (rc == 0) ? "CONNECT" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc != 0) {
			shell_print(sh, "-> 没等到 CONNECT，URL 长度与实际字节数可能差一个");
			return -1;
		}
	}

	/* 第 5 步：灌 URL 原文 + 0x1A 收尾 */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		for (i = 0; i < url_len; i++) {
			uart_poll_out(dev, url[i]);
		}
		uart_poll_out(dev, 0x1A);
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "OK", 4000);
		shell_print(sh, "  4/6 url-body   TX%3d RX%3d  %s", url_len + 1, rxlen,
			    (rc == 0) ? "OK" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (rc == -1) {
			shell_print(sh, "-> URL 灌完没回 OK，长度可能和实际字节数对不上");
			return -1;
		}
	}

	/*
	 * 第 6 步：紧跟 GETEX，中间绝不能夹 GET 或 READ。
	 * 实测教训：先 AT+HTTPGET + AT+HTTPREAD 再发 GETEX，
	 * 模组回 "+HTTPGETEX: -1,0,0"（-1 = 客户端错误），READ 直接 ERROR。
	 */
	{
		char cmd[48];

		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		snprintk(cmd, sizeof(cmd), "AT+HTTPGETEX=10,%d,%d\r", start_pos, read_len);
		txlen = (int)strlen(cmd);
		for (i = 0; i < txlen; i++) {
			uart_poll_out(dev, (unsigned char)cmd[i]);
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "+HTTPGETEX:", 12000);
		if (rc == 0) {
			/* 关键字只说明上报开始，再补 300ms 把 0,206,xxxx 收全 */
			(void)at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 300);
		}
		shell_print(sh, "  5/6 getex      TX%3d RX%3d  %s", txlen, rxlen,
			    (rc == 0) ? "GOT +HTTPGETEX" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		for (i = 0; i < rxlen; i++) {
			shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],
				    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
		}

		if (rc != 0) {
			shell_print(sh, "-> 没等到 +HTTPGETEX 上报");
			return -1;
		}

		/*
		 * 解析 +HTTPGETEX: <err>,<httpcode>,<content_length>
		 * 标记 11 字节（比 +HTTPGET: 多 "EX" 两个字符，别照抄那个 9 字节匹配）。
		 * err 可能是负数（-1 = 客户端错误），必须支持前导 '-'，
		 * 否则 -1 会被当成"没解析出来"。
		 */
		{
			int k;

			for (k = 0; k + 11 <= rxlen; k++) {
				int j;
				int idx = 0;
				int v[3] = {0, 0, 0};
				int neg[3] = {0, 0, 0};
				int had[3] = {0, 0, 0};

				if (!(rx[k] == '+' && rx[k + 1] == 'H' && rx[k + 2] == 'T' &&
				      rx[k + 3] == 'T' && rx[k + 4] == 'P' &&
				      rx[k + 5] == 'G' && rx[k + 6] == 'E' &&
				      rx[k + 7] == 'T' && rx[k + 8] == 'E' &&
				      rx[k + 9] == 'X' && rx[k + 10] == ':')) {
					continue;
				}

				j = k + 11;
				while (j < rxlen && idx < 3) {
					unsigned char c = rx[j];

					if (c == '-') {
						neg[idx] = 1;
						j++;
					} else if (c >= '0' && c <= '9') {
						v[idx] = v[idx] * 10 + (int)(c - '0');
						had[idx] = 1;
						j++;
					} else if (c == ',') {
						idx++;
						j++;
					} else {
						break;
					}
				}

				gx_err  = had[0] ? (neg[0] ? -v[0] : v[0]) : 99;
				gx_code = had[1] ? v[1] : -1;
				gx_len  = had[2] ? v[2] : -1;
				break;
			}

			shell_print(sh, "  => err=%d  http=%d  len=%d", gx_err, gx_code, gx_len);
		}
	}

	/*
	 * 第 7 步：读数据。三种应答要分清：
	 *   CONNECT + 一堆字节  → 正常吐数据
	 *   \r\nERROR\r\n       → 模组拒绝（会话已失效或无可读内容）
	 *   什么都没有          → 超时
	 */
	{
		(void)uart_fifo_read(dev, rx, sizeof(rx));
		(void)uart_err_check(dev);
		rxlen = 0;
		txlen = (int)strlen("AT+HTTPREAD=10\r");
		for (i = 0; i < txlen; i++) {
			uart_poll_out(dev, "AT+HTTPREAD=10\r"[i]);
		}
		rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 4000);
		rd_len = rxlen;
		/* ERROR 应答只有 9 字节，加上长度上限才敢判，避免正文里正好有 ERROR 字样 */
		if (rxlen > 0 && rxlen <= 16 && at2_rx_has_tok(rx, rxlen, "ERROR") == 0) {
			rd_error = 1;
			rd_len = 0;
		}
		shell_print(sh, "  6/6 http-read  TX%3d RX%3d  %s", txlen, rxlen,
			    rd_error ? "ERROR" :
			    (rc == 0) ? "END" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
		if (!rd_error) {
			for (i = 0; i < rxlen; i++) {
				shell_print(sh, "  [%03d] 0x%02X  %c", i, rx[i],
					    (rx[i] >= 0x20 && rx[i] < 0x7F) ? rx[i] : '.');
			}
		}
	}

	/* 第 8 步：给结论 */
	{
		shell_print(sh, "");
		if (gx_err == -1) {
			shell_print(sh, "-> err=-1（客户端错误）：这条 HTTP 客户端已经不可用了");
			shell_print(sh, "   本命令已改成 URL 之后紧跟 GETEX、中间不夹任何请求；");
			shell_print(sh, "   若仍报 -1，说明 GETEX 对同一 URL 只能配合一次请求周期");
			shell_print(sh, "   VERDICT: 分段这条路不通，回去做 UART2 RX DMA");
		} else if (gx_code == 206) {
			shell_print(sh, "-> 206 Partial Content：服务器认 Range，分段可行");
			shell_print(sh, "   本次 READ 吐出 %d 字节（缓冲上限 %d）", rd_len, (int)sizeof(rx) - 2);
			if (gx_len > 0) {
				shell_print(sh, "   声明总长 %d，按 %d 一段循环约 %d 次",
					    gx_len, read_len, (gx_len + read_len - 1) / read_len);
			}
			shell_print(sh, "   VERDICT: 分段可用，不必补 UART2 RX DMA");
		} else if (gx_code == 200) {
			shell_print(sh, "-> 200 OK：服务器忽略了 Range，仍回全文");
			shell_print(sh, "   本次 READ 吐出 %d 字节（被缓冲截断），声明总长 %d",
				    rd_len, gx_len);
			shell_print(sh, "   VERDICT: 分段不可用，只能补 UART2 RX DMA");
		} else if (gx_code == 416) {
			shell_print(sh, "-> 416 Range Not Satisfiable：start=%d 超出资源总长 %d",
				    start_pos, gx_len);
			shell_print(sh, "   VERDICT: Range 已生效，只是 start 越界，调小重试");
		} else if (rd_error) {
			shell_print(sh, "-> GETEX 报 err=%d http=%d，随后 READ 回 ERROR", gx_err, gx_code);
			shell_print(sh, "   VERDICT: 请求没成立，换 URL 或换接口再试");
		} else {
			shell_print(sh, "-> GETEX err=%d http=%d len=%d，READ 收到 %d 字节",
				    gx_err, gx_code, gx_len, rd_len);
			shell_print(sh, "   VERDICT: 数据不明确，看上面 hex 自己判");
		}
	}

#undef AT2GEX_STEP

	return 0;
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
 *   7. 每段结束打印进度，末段后打印总统计
 * 用法：app at2get <url> [seg_len]
 *   例：app at2get http://www.baidu.com/
 *       app at2get http://www.baidu.com/ 598
 * 注意：
 *   - URL 里的 '?' 请写成 '#'（Zephyr shell 会把问号当 glob 通配符吞掉）
 *   - seg_len 上限 598（受 rx[600] 限制）；不给则默认 598
 *   - 输出是 hex，不是原文。用配套的 Python 脚本还原成文件
 *   - 全程约 60~90 秒（每段都要重新建一次连接）
 */
static int shell_at2get(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	static unsigned char url[200];
	static unsigned char rx[600];
	int url_len = 0;
	int seg_len = 580;              /* 每段读多少。不能取 598，因为 READ 回来的 */
	                                /* 数据前面有 "\r\n\r\nCONNECT\r\n" 共 13 字节 */
	                                /* 应答头，正文上限 = rx[600] - 13 - 余量。     */
	                                /* 取 580 时回 593 字节，留 7 字节余量。        */
	int start_pos = 0;              /* 当前段的起始偏移                       */
	int total = 0;                  /* 已累计吐出的正文字节数                 */
	int seg_idx = 0;                /* for 循环变量（从 0 开始）              */
	int done_seg = 0;               /* 真正处理完并吐出数据的段数             */
	int txlen;
	int rxlen;
	int rc;
	int i;
	int gx_err = 99;                /* +HTTPGETEX 第 1 字段：错误码，0 = 成功 */
	int gx_code = -1;               /* +HTTPGETEX 第 2 字段：HTTP 状态码      */
	int gx_len = -1;                /* +HTTPGETEX 第 3 字段：本段响应体长度    */
	int body_off = 0;               /* READ 数据里正文的起始偏移              */
	int body_len = 0;               /* 本段正文实际字节数                     */
	int rd_error = 0;               /* READ 回的是 ERROR 而不是数据           */

	if (argc < 2) {
		shell_print(sh, "usage: app at2get <url> [seg_len]");
		shell_print(sh, "   e.g. app at2get http://www.baidu.com/");
		shell_print(sh, "        app at2get http://www.baidu.com/ 580");
		shell_print(sh, "       URL 里的 '?' 请写成 '#'");
		shell_print(sh, "       seg_len 默认 580，上限 584（READ 的 13 字节应答头占位置）");
		shell_print(sh, "       输出是 hex，用 restore_at2get.py 转回文件");
		return 0;
	}

	dev = device_get_binding("UART_2");
	if (!dev) {
		shell_error(sh, "device UART_2 not found");
		return -1;
	}

	/* 拼 URL：'#' 还原成 '?' */
	for (i = 0; argv[1][i] != '\0' && url_len < (int)sizeof(url) - 2; i++) {
		url[url_len] = argv[1][i];
		if (url[url_len] == '#') {
			url[url_len] = '?';
		}
		url_len++;
	}
	url[url_len] = '\0';

	/* 读 seg_len：只认 0~9，非法或超界都回落到 598 */
	if (argc >= 3) {
		int v = 0;
		int k;

		for (k = 0; argv[2][k] >= '0' && argv[2][k] <= '9'; k++) {
			v = v * 10 + (argv[2][k] - '0');
		}
		if (v > 0) {
			seg_len = v;
		}
	}
	if (seg_len > (int)sizeof(rx) - 16) {
		seg_len = (int)sizeof(rx) - 16;
	}

	shell_print(sh, "-> URL %d byte(s): %s", url_len, url);
	shell_print(sh, "-> seg_len=%d  每段都要重配一次 URL 连接，全程约 60~90 秒",
		    seg_len);

	/*
	 * 第 2~4 步的复用宏：清残留 → 发命令 → 等关键字 → 打印一行进度。
	 * 注意：这个宏在 for 循环里每段都要跑一遍，这是 GETEX 的硬前提。
	 */
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

	for (seg_idx = 0; seg_idx < 200; seg_idx++) {
		shell_print(sh, "");
		shell_print(sh, "=== SEG %d  start=%d ===", seg_idx + 1, start_pos);

		/* 2.1 requestheader 必须 0，这是 GETEX 能用的前提 */
		AT2GET_STEP("AT+HTTPCFG=\"requestheader\",0\r", "OK", 2000, NULL);
		shell_print(sh, "  1/6 cfg-hdr    TX%3d RX%3d  %s", txlen, rxlen,
			    (rc == 0) ? "GOT" : "FAIL");
		if (rc != 0) {
			shell_print(sh, "-> 第 1 步失败，先跑 app at2tx AT 确认模组在线");
			break;
		}

		/* 2.2 rspout/auto 也配 0，让模组直接把正文吐出来 */
		AT2GET_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, NULL);
		shell_print(sh, "  2/6 cfg-rsp    TX%3d RX%3d  %s", txlen, rxlen,
			    (rc == 0) ? "GOT" : "FAIL");

		/* 2.3 URL 长度是变量，单独拼 */
		{
			char buf[64];

			(void)uart_fifo_read(dev, rx, sizeof(rx));
			(void)uart_err_check(dev);
			rxlen = 0;
			snprintk(buf, sizeof(buf), "AT+HTTPURL=%d,60\r", url_len);
			txlen = (int)strlen(buf);
			for (i = 0; i < txlen; i++) {
				uart_poll_out(dev, (unsigned char)buf[i]);
			}
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "CONNECT", 3000);
			shell_print(sh, "  3/6 url-length TX%3d RX%3d  %s", txlen, rxlen,
				    (rc == 0) ? "CONNECT" : "FAIL");
			if (rc != 0) {
				shell_print(sh, "-> 没等到 CONNECT，URL 长度与实际字节数可能差一个");
				break;
			}
		}

		/* 2.4 灌 URL 原文 + 0x1A 收尾 */
		{
			(void)uart_fifo_read(dev, rx, sizeof(rx));
			(void)uart_err_check(dev);
			rxlen = 0;
			for (i = 0; i < url_len; i++) {
				uart_poll_out(dev, url[i]);
			}
			uart_poll_out(dev, 0x1A);
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "OK", 4000);
			shell_print(sh, "  4/6 url-body   TX%3d RX%3d  %s", url_len + 1, rxlen,
				    (rc == 0) ? "OK" : "FAIL");
			if (rc == -1) {
				shell_print(sh, "-> URL 灌完没回 OK，长度可能和实际字节数对不上");
				break;
			}
		}

		/* 2.5 紧跟 GETEX，中间绝不能夹 GET 或 READ */
		{
			char cmd[48];

			(void)uart_fifo_read(dev, rx, sizeof(rx));
			(void)uart_err_check(dev);
			rxlen = 0;
			snprintk(cmd, sizeof(cmd), "AT+HTTPGETEX=10,%d,%d\r",
				 start_pos, seg_len);
			txlen = (int)strlen(cmd);
			for (i = 0; i < txlen; i++) {
				uart_poll_out(dev, (unsigned char)cmd[i]);
			}
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, "+HTTPGETEX:", 12000);
			if (rc == 0) {
				/* 关键字只说明上报开始，再补 300ms 把三字段收全 */
				(void)at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 300);
			}
			shell_print(sh, "  5/6 getex      TX%3d RX%3d  %s", txlen, rxlen,
				    (rc == 0) ? "GOT" : "TIMEOUT");
			if (rc != 0) {
				shell_print(sh, "-> 没等到 +HTTPGETEX 上报");
				break;
			}

			/*
			 * 解析 +HTTPGETEX: <err>,<httpcode>,<len>
			 * 标记 11 字节；err 可能是负数（-1 = 客户端错误），支持前导 '-'
			 */
			gx_err = 99;
			gx_code = -1;
			gx_len = -1;
			{
				int k;

				for (k = 0; k + 11 <= rxlen; k++) {
					int j;
					int idx = 0;
					int v[3] = {0, 0, 0};
					int neg[3] = {0, 0, 0};
					int had[3] = {0, 0, 0};

					if (!(rx[k] == '+' && rx[k + 1] == 'H' && rx[k + 2] == 'T' &&
					      rx[k + 3] == 'T' && rx[k + 4] == 'P' &&
					      rx[k + 5] == 'G' && rx[k + 6] == 'E' &&
					      rx[k + 7] == 'T' && rx[k + 8] == 'E' &&
					      rx[k + 9] == 'X' && rx[k + 10] == ':')) {
						continue;
					}

					j = k + 11;
					while (j < rxlen && idx < 3) {
						unsigned char c = rx[j];

						if (c == '-') {
							neg[idx] = 1;
							j++;
						} else if (c >= '0' && c <= '9') {
							v[idx] = v[idx] * 10 + (int)(c - '0');
							had[idx] = 1;
							j++;
						} else if (c == ',') {
							idx++;
							j++;
						} else {
							break;
						}
					}

					gx_err  = had[0] ? (neg[0] ? -v[0] : v[0]) : 99;
					gx_code = had[1] ? v[1] : -1;
					gx_len  = had[2] ? v[2] : -1;
					break;
				}
			}
			shell_print(sh, "  => err=%d  http=%d  len=%d", gx_err, gx_code, gx_len);
		}

		/* 越界：416 就是"读完了"，这是唯一的终止信号 */
		if (gx_code == 416) {
			shell_print(sh, "-> 416：start=%d 超出资源末尾，全文已取完", start_pos);
			break;
		}
		/* 只接受 206。200 说明服务器忽略了 Range，继续循环会重复取全文 */
		if (gx_code != 206) {
			shell_print(sh, "-> http=%d 不是 206，服务器没按 Range 返回，停止",
				    gx_code);
			shell_print(sh, "   若这里是 200，说明该服务器不支持 Range");
			break;
		}

		/* 2.6 取数据 */
		{
			(void)uart_fifo_read(dev, rx, sizeof(rx));
			(void)uart_err_check(dev);
			rxlen = 0;
			txlen = (int)strlen("AT+HTTPREAD=10\r");
			for (i = 0; i < txlen; i++) {
				uart_poll_out(dev, "AT+HTTPREAD=10\r"[i]);
			}
			rc = at2_pump(dev, rx, (int)sizeof(rx), &rxlen, NULL, 4000);
			rd_error = 0;
			if (rxlen > 0 && rxlen <= 16 && at2_rx_has_tok(rx, rxlen, "ERROR") == 0) {
				rd_error = 1;
			}
			shell_print(sh, "  6/6 http-read  TX%3d RX%3d  %s", txlen, rxlen,
				    rd_error ? "ERROR" :
				    (rc == 0) ? "END" : (rc == 1) ? "BUF-FULL" : "TIMEOUT");
			if (rd_error || rxlen <= 0) {
				shell_print(sh, "-> READ 没拿到数据，本段放弃");
				break;
			}
		}

		/*
		 * 剥离应答头：READ 的数据形如
		 *   \r\n\r\nCONNECT\r\n<正文>
		 * 找 "CONNECT" 七字节，再跳过它后面的 \r 和 \n，剩下就是正文。
		 * 正文里也可能出现 CONNECT，所以只取第一个。
		 */
		body_off = 0;
		for (i = 0; i + 7 <= rxlen; i++) {
			if (rx[i] == 'C' && rx[i + 1] == 'O' && rx[i + 2] == 'N' &&
			    rx[i + 3] == 'N' && rx[i + 4] == 'E' && rx[i + 5] == 'C' &&
			    rx[i + 6] == 'T') {
				body_off = i + 7;
				break;
			}
		}
		while (body_off < rxlen && (rx[body_off] == '\r' || rx[body_off] == '\n')) {
			body_off++;
		}
		body_len = rxlen - body_off;

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
		if (gx_len >= 0 && gx_len < body_len) {
			shell_print(sh, "   (按服务器声明截断 %d -> %d，切掉应答残片)",
				    body_len, gx_len);
			body_len = gx_len;
		}

		/* gx_len 不可用时退回模式匹配：剥尾部完整的 "\r\nOK\r\n" */
		if (body_len >= 6 &&
		    rx[body_off + body_len - 6] == '\r' &&
		    rx[body_off + body_len - 5] == '\n' &&
		    rx[body_off + body_len - 4] == 'O' &&
		    rx[body_off + body_len - 3] == 'K' &&
		    rx[body_off + body_len - 2] == '\r' &&
		    rx[body_off + body_len - 1] == '\n') {
			body_len -= 6;
			shell_print(sh, "   (剥掉尾部 6 字节 \\r\\nOK\\r\\n)");
		}

		if (body_len <= 0) {
			shell_print(sh, "-> 剥完应答头没有正文，本段为空，停止");
			break;
		}

		/*
		 * 打 hex：每行 16 字节，前缀是全局偏移。
		 * 用 hex 而不是原文，是为了丢字节时能一眼看出来（偏移会跳）。
		 */
		shell_print(sh, "--- DATA start=%d len=%d ---", start_pos, body_len);
		{
			int p;

			for (p = 0; p < body_len; p += 16) {
				char line[120];
				int n = 0;
				int q;

				n += snprintk(line + n, sizeof(line) - n, "[%06d]", total + p);
				for (q = 0; q < 16 && p + q < body_len; q++) {
					n += snprintk(line + n, sizeof(line) - n, " %02X",
						      rx[body_off + p + q]);
				}
				/* 行尾加 '|' 作完整性标记：串口丢字节会把 '|' 一起丢掉，
				 * 还原脚本看到没有 '|' 结尾就知道这一行不完整。
				 * 实测 SEG 14 丢 2 字节、SEG 18 丢 1 字节，偏移却没跳，
				 * 靠偏移查不出来。 */
				n += snprintk(line + n, sizeof(line) - n, " |");
				shell_print(sh, "%s", line);
			}
		}
		shell_print(sh, "--- END start=%d len=%d ---", start_pos, body_len);

		total += body_len;
		done_seg++;

		/*
		 * 判"到末尾"必须看服务器声明的段长度 gx_len，不能看本地收到的
		 * body_len。原因：body_len = rxlen - body_off，而 rxlen 被 rx[600]
		 * 截断，那 13 字节应答头也占位置，所以 body_len 天然比 seg_len 少
		 * 13 左右。用 body_len 判断会把每一段都当成末尾（已实测踩过）。
		 */
		if (gx_len >= 0 && gx_len < seg_len) {
			shell_print(sh, "-> 服务器只给了 %d 字节（请求 %d），已到资源末尾",
				    gx_len, seg_len);
			start_pos += body_len;
			break;
		}

		start_pos += body_len;

		/* 段间留一点时间给 printk 的 DMA 排空，防止 2048 字节缓冲丢数据 */
		k_busy_wait(8000);
	}

	shell_print(sh, "");
	shell_print(sh, "=== 总计 %d 段，%d 字节 ===", done_seg, total);
	shell_print(sh, "   用还原脚本把这批 hex 转回文件；缺段时偏移会跳变");

#undef AT2GET_STEP

	return 0;
}

#endif /* CONFIG_UART_2 */
SHELL_STATIC_SUBCMD_SET_CREATE(acts_app_cmds,
	SHELL_CMD(set_config, NULL, "set system config", shell_set_config),
	SHELL_CMD(input, NULL, "input key event", shell_input_key_event),
#ifdef CONFIG_MONKEY_TEST
	SHELL_CMD(monkey, NULL, "monkey test ", shell_monkey_event),
#endif
	SHELL_CMD(btinfo, NULL, "dump bt info", shell_dump_bt_info),
#ifdef CONFIG_UART_2
	SHELL_CMD(at2tx, NULL, "send any AT command to the module over UART2", shell_at2tx),
	SHELL_CMD(at2http, NULL, "run a full HTTP GET over UART2 and show the response head", shell_at2http),
	SHELL_CMD(at2gex, NULL, "range GET over UART2, judge if server supports HTTP Range", shell_at2gex),
	SHELL_CMD(at2get, NULL, "loop-generic range GET, dump hex to console", shell_at2get),
#endif
#ifdef CONFIG_UI_MANAGER
	SHELL_CMD(uistack, NULL, "dump ui stack", shell_dump_uistack),
#endif
	SHELL_SUBCMD_SET_END
);

static int cmd_acts_app(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(shell);
		return SHELL_CMD_HELP_PRINTED;
	}

	shell_error(shell, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

SHELL_CMD_REGISTER(app, &acts_app_cmds, "Application shell commands", cmd_acts_app);
#endif
