/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Zephyr Shell 的交互核心。ATS 串口后端把字节保存在接收环形缓冲区并发信号；本文件的线程取出字节、拼出命令行、沿命令表选择处理函数，随后等待下一次输入。 */
#include <ctype.h>    /* 提供 ASCII 分类函数，用于空白识别、编辑和十六进制显示。 */
#include <stdlib.h>    /* 提供标准 C 工具声明。 */
#include <sys/atomic.h>    /* 提供 Shell 状态标志依赖的原子操作声明。 */
#include <shell/shell.h>    /* 引入实例、传输接口、命令描述和处理函数类型。 */
/* 仅配置虚拟后端时允许程序调用省略 Shell 实例。 */
#if defined(CONFIG_SHELL_BACKEND_DUMMY)
#include <shell/shell_dummy.h>
#endif
#include "shell_ops.h"    /* 光标、命令行与终端输出的内部操作。 */
#include "shell_help.h"    /* 帮助条目及帮助参数识别。 */
#include "shell_utils.h"    /* 分词、命令表枚举和根节点查找。 */
#include "shell_vt100.h"    /* 终端控制字节及转义序列。 */
#include "shell_wildcard.h"    /* 可选的通配符展开接口。 */

/* 最小缓冲区须容纳一个命令字符及其结尾零。 */
#if (CONFIG_SHELL_CMD_BUFF_SIZE < 2)
	#error too small CONFIG_SHELL_CMD_BUFF_SIZE
#endif

/* 格式化输出缓冲区至少需要一个字节。 */
#if (CONFIG_SHELL_PRINTF_BUFF_SIZE < 1)
	#error too small SHELL_PRINTF_BUFF_SIZE
#endif

/* 这些文字用于向终端报告分派或输出错误，NULL 作为候选列表排版的起始标记。 */
#define SHELL_MSG_CMD_NOT_FOUND		": command not found"
#define SHELL_MSG_BACKEND_NOT_ACTIVE	\
	"WARNING: A print request was detected on not active shell backend.\n"
#define SHELL_MSG_TOO_MANY_ARGS		"Too many arguments in the command.\n"
#define SHELL_INIT_OPTION_PRINTER	(NULL)

/* 编译时决定 Shell 线程的优先级；未指定覆盖值时使用最低应用线程优先级。 */
#define SHELL_THREAD_PRIORITY \
	COND_CODE_1(CONFIG_SHELL_THREAD_PRIORITY_OVERRIDE, \
			(CONFIG_SHELL_THREAD_PRIORITY), (K_LOWEST_APPLICATION_THREAD_PRIO))

/* 线程优先级必须位于内核允许的应用线程范围内。 */
BUILD_ASSERT(SHELL_THREAD_PRIORITY >=
		  K_HIGHEST_APPLICATION_THREAD_PRIO
		&& SHELL_THREAD_PRIORITY <= K_LOWEST_APPLICATION_THREAD_PRIO,
		  "Invalid range for thread priority");

/**
  * @brief  记录下一个字节应采用的终端转义解析状态。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  state：[输入] 下次处理输入字节时使用的接收状态。
  * @retval 无。
  */
static inline void receive_state_change(const struct shell *shell,
					enum shell_receive_state state)
{
	shell->ctx->receive_state = state;    /* 保存解析状态供下一个字节使用，允许转义序列跨多次接收。 */
}

/**
  * @brief  清空可编辑命令行并将光标归位。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
static void cmd_buffer_clear(const struct shell *shell)
{
	shell->ctx->cmd_buff[0] = '\0';    /* 保留旧数据区，仅在开头写零即可表示空字符串。 */
	shell->ctx->cmd_buff_pos = 0;      /* 下一次插入从首字节开始。 */
	shell->ctx->cmd_buff_len = 0;      /* 当前行的有效字符数也归零。 */
}

/**
  * @brief  按当前活动命令打印帮助与子命令清单。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  * @note   CONFIG_SHELL_HELP 关闭时直接返回。
  */
static void shell_internal_help_print(const struct shell *shell)
{
	/* 帮助功能由配置裁剪，关闭时不访问命令帮助信息。 */
	if (!IS_ENABLED(CONFIG_SHELL_HELP)) {
		return;
	}

	z_shell_help_cmd_print(shell, &shell->ctx->active_cmd);      /* 输出活动命令自己的语法、参数和说明。 */
	z_shell_help_subcmd_print(shell, &shell->ctx->active_cmd,    /* 继续列出该条目可枚举的子命令。 */
				  "Subcommands:\n");
}

/**
  * @brief  参数个数不合规则时输出错误，可按配置附带当前命令帮助。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  arg_cnt_ok：[输入] 调用方对必选与可选参数上下限的检查结果。
  * @retval 0 表示数量符合要求，-EINVAL 表示数量不符。
  */
static int cmd_precheck(const struct shell *shell,
			bool arg_cnt_ok)
{
	/* 数量不符时先指明出错命令，按配置再打印帮助。 */
	if (!arg_cnt_ok) {
		z_shell_fprintf(shell, SHELL_ERROR,                             /* 输出本次被选中命令的语法名。 */
				"%s: wrong parameter count\n",
				shell->ctx->active_cmd.syntax);

		if (IS_ENABLED(CONFIG_SHELL_HELP_ON_WRONG_ARGUMENT_COUNT)) {    /* 仅在构建要求时随错误附上帮助。 */
			shell_internal_help_print(shell);
		}

		return -EINVAL;                                                 /* 让上层跳过实际命令处理。 */
	}

	return 0;                                                               /* 数量满足要求，可以分派命令。 */
}

/**
  * @brief  改变实例生命周期状态；进入交互态时恢复一条可输入的新命令行。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  state：[输入] 新的 Shell 实例状态。
  * @retval 无。
  * @note   启用旁路时不清屏、不重印提示符。
  */
static inline void state_set(const struct shell *shell, enum shell_state state)
{
	shell->ctx->state = state;                                /* 保存生命周期状态，供接收与输出路径检查。 */

	/* 进入普通交互态时重建输入行和提示符；旁路接管输入时跳过。 */
	if (state == SHELL_STATE_ACTIVE && !shell->ctx->bypass) {
		cmd_buffer_clear(shell);                          /* 清空上次命令及光标位置。 */
		if (z_flag_print_noinit_get(shell)) {             /* 停用时若有输出请求，在界面恢复后提示一次。 */
			z_shell_fprintf(shell, SHELL_WARNING, "%s",
					SHELL_MSG_BACKEND_NOT_ACTIVE);
			z_flag_print_noinit_set(shell, false);    /* 清除已报告的待提示标志。 */
		}
		z_shell_print_prompt_and_cmd(shell);              /* 显示当前实例的提示符和命令缓冲区。 */
	}
}

/**
  * @brief  读取实例当前的生命周期状态。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 当前 shell_state 枚举值。
  */
static inline enum shell_state state_get(const struct shell *shell)
{
	return shell->ctx->state;    /* 返回当前实例的状态字段。 */
}

/**
  * @brief  取得命令匹配和补全使用的预选根节点。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 预选命令指针，未选定或未启用选择功能时为 NULL。
  */
static inline const struct shell_static_entry *
selected_cmd_get(const struct shell *shell)
{
	/* 显式选择命令功能或预设根命令启用时，查询实例保存的起点。 */
	if (IS_ENABLED(CONFIG_SHELL_CMDS_SELECT)
	    || (CONFIG_SHELL_CMD_ROOT[0] != 0)) {
		return shell->ctx->selected_cmd;    /* 返回已选根节点，尚未选定时仍可能为空。 */
	}

	return NULL;                                /* 让调用方从全局根命令表开始。 */
}

/**
  * @brief  按终端可用宽度把一项补全候选排入当前输出行。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  option：[输入] 候选名称；NULL 重置排版计数。
  * @param  longest_option：[输入] 本次最长候选名称的字节数，用于统一列宽。
  * @retval 无。
  */
static void tab_item_print(const struct shell *shell, const char *option,
			   uint16_t longest_option)
{
	static const char *tab = "  ";                                          /* 候选项之间预留两个字符的列间距。 */
	uint16_t columns;                                                       /* 每个终端输出行可容纳的候选列数。 */
	uint16_t diff;                                                          /* 当前名称离下一列起点还差的字符数。 */

	/* 以 NULL 调用代表新一批候选，首先复位列计数器。 */
	if (option == NULL) {
		shell->ctx->vt100_ctx.printed_cmd = 0;
		return;
	}

	longest_option += z_shell_strlen(tab);                                  /* 将列间空格计入候选显示宽度。 */

	columns = (shell->ctx->vt100_ctx.cons.terminal_wid                      /* 由终端宽度和最长名称推算每行列数。下方用 columns 作取模除数，过窄的终端可能使它为零。 */
			- z_shell_strlen(tab)) / longest_option;
	diff = longest_option - z_shell_strlen(option);                         /* 计算打印本项后还需水平移过多少字符。 */

	if (shell->ctx->vt100_ctx.printed_cmd++ % columns == 0U) {              /* 余数为零表示当前项是新行第一列。 */
		z_shell_fprintf(shell, SHELL_OPTION, "\n%s%s", tab, option);    /* 首列前先换行并留初始缩进。 */
	} else {
		z_shell_fprintf(shell, SHELL_OPTION, "%s", option);             /* 同一行后续列沿用当前光标位置。 */
	}

	z_shell_op_cursor_horiz_move(shell, diff);                              /* 将光标移至下一列起点。 */
}

/**
  * @brief  在启用历史功能的实例上初始化历史记录。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
static void history_init(const struct shell *shell)
{
	/* 关闭历史功能时无需初始化相应存储。 */
	if (!IS_ENABLED(CONFIG_SHELL_HISTORY)) {
		return;
	}

	z_shell_history_init(shell->history);    /* 交给历史模块初始化该实例的记录结构。 */
}

/**
  * @brief  清空实例保存的历史命令。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
static void history_purge(const struct shell *shell)
{
	/* 仅为启用历史记录的构建执行清理。 */
	if (!IS_ENABLED(CONFIG_SHELL_HISTORY)) {
		return;
	}

	z_shell_history_purge(shell->history);    /* 丢弃此 Shell 历史模块保存的记录。 */
}

/**
  * @brief  结束历史浏览并复位延迟退出标志。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
static void history_mode_exit(const struct shell *shell)
{
	/* 关闭历史功能时没有待恢复的浏览状态。 */
	if (!IS_ENABLED(CONFIG_SHELL_HISTORY)) {
		return;
	}

	z_flag_history_exit_set(shell, false);        /* 取消延后结束历史浏览的请求。 */
	z_shell_history_mode_exit(shell->history);    /* 重置历史模块内部的浏览游标。 */
}

/**
  * @brief  把一次输入提交给当前实例的历史缓冲。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  line：[输入] 包含本次命令的可读字节区。
  * @param  length：[输入] line 中命令的字节数。
  * @retval 无。
  */
static void history_put(const struct shell *shell, uint8_t *line, size_t length)
{
	/* 未启用历史记录时，命令可继续执行但不入历史。 */
	if (!IS_ENABLED(CONFIG_SHELL_HISTORY)) {
		return;
	}

	z_shell_history_put(shell->history, line, length);    /* 按显式长度把输入行保存到实例历史区。 */
}

/**
  * @brief  按上下方向浏览历史命令，并在退出浏览时恢复此前编辑的命令行。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  up：[输入] true 向较旧记录移动，false 向较新记录移动。
  * @retval 无。
  * @note   暂存区也用于 Tab 补全；收到退出标志时应先结束旧的浏览状态。
  */
static void history_handle(const struct shell *shell, bool up)
{
	bool history_mode;                                                          /* 标记读取记录后是否仍在浏览历史。 */
	uint16_t len;                                                               /* 用于重画命令行并复位光标的字节数。 */

	/* 历史浏览仅在配置启用时处理。 */
	if (!IS_ENABLED(CONFIG_SHELL_HISTORY)) {
		return;
	}

	/* 先消费上一次编辑或补全请求留下的退出标志。 */
	if (z_flag_history_exit_get(shell)) {
		z_flag_history_exit_set(shell, false);                              /* 取消已处理的请求。 */
		z_shell_history_mode_exit(shell->history);                          /* 丢弃此前的历史选择位置。 */
	}

	/* 初次上翻历史时保存尚未提交的编辑内容，以便稍后恢复。 */
	if (!z_shell_history_active(shell->history)) {
		if (up) {                                                           /* 上翻可以进入历史，下翻只在已经浏览时有意义。 */
			uint16_t cmd_len = z_shell_strlen(shell->ctx->cmd_buff);    /* 取得当前草稿的长度。 */

			if (cmd_len) {                                              /* 区分有内容的草稿与空行。 */
				strcpy(shell->ctx->temp_buff,                       /* 在命令缓冲被历史记录覆盖之前保留原文。 */
				       shell->ctx->cmd_buff);
			} else {
				shell->ctx->temp_buff[0] = '\0';                    /* 为原本空的草稿保存一个空字符串。 */
			}
		} else {
			return;                                                     /* 当前处于新行，向下浏览直接结束。 */
		}
	}

	/* 按照方向从历史模块取记录；该模块同时报告命令的字节数。 */
	history_mode = z_shell_history_get(shell->history, up,
					   shell->ctx->cmd_buff, &len);

	if (!history_mode) {                                                        /* 到达历史末端时，恢复进入浏览前暂存的内容。 */
		strcpy(shell->ctx->cmd_buff, shell->ctx->temp_buff);                /* 将草稿重新放回可编辑行。 */
		len = z_shell_strlen(shell->ctx->cmd_buff);                         /* 按恢复后的文本修正光标应停留的位置。 */
	}

	/* 重新绘制整个输入行并同步字符串长度和光标位置。 */
	z_shell_op_cursor_home_move(shell);
	z_clear_eos(shell);                                                         /* 擦掉屏幕上旧行的剩余内容。 */
	z_shell_print_cmd(shell);                                                   /* 写出所选历史命令或恢复的草稿。 */
	shell->ctx->cmd_buff_pos = len;                                             /* 逻辑光标定位到新内容末尾。 */
	shell->ctx->cmd_buff_len = len;                                             /* 更新编辑缓冲区的有效长度。 */
	z_shell_op_cond_next_line(shell);                                           /* 处理终端行尾自动换行的位置。 */
}

/**
  * @brief  计算当前输入行还可以容纳多少补全字符。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 可写入的字节数，已预留结尾零。
  */
static inline uint16_t completion_space_get(const struct shell *shell)
{
	uint16_t space = (CONFIG_SHELL_CMD_BUFF_SIZE - 1) -    /* 从配置容量中扣掉结尾零和当前占用字符。 */
			shell->ctx->cmd_buff_len;
	return space;                                          /* 把可插入的字节数交给补全器。 */
}

/**
  * @brief  只解析光标左侧的命令前缀，确定补全应在哪一级命令表中查找。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  cmd：[输出] 写入父命令地址；从全局根表开始时可写入 NULL。
  * @param  argv：[输入输出] 指向调用方指针数组，可调整起点以跳过 select。
  * @param  argc：[输出] 分词后的参数个数。
  * @param  complete_arg_idx：[输出] 这次数组中待补全参数的位置。
  * @param  d_entry：[输出] 查找动态子命令时可写的临时条目。
  * @retval true 表示可继续枚举候选，false 表示空间不足、参数过多或前缀没有匹配节点。
  */
static bool tab_prepare(const struct shell *shell,
			const struct shell_static_entry **cmd,
			const char ***argv, size_t *argc,
			size_t *complete_arg_idx,
			struct shell_static_entry *d_entry)
{
	uint16_t compl_space = completion_space_get(shell);                      /* 先计算编辑缓冲能再容纳的字符数。 */
	size_t search_argc;                                                      /* 查找父命令时已有完整名称的参数项数。 */

	/* 无空位时终止本次补全，以免覆盖末尾零。 */
	if (compl_space == 0U) {
		return false;
	}

	/* 只复制光标左侧内容到临时区，分词不会改动正在编辑的原行。 */
	memcpy(shell->ctx->temp_buff, shell->ctx->cmd_buff,
			shell->ctx->cmd_buff_pos);
	shell->ctx->temp_buff[shell->ctx->cmd_buff_pos] = '\0';                  /* 追加临时字符串的结束符。 */

	/* 在临时区就地拆出命令词，并限制可记录的参数总数。 */
	(void)z_shell_make_argv(argc, *argv, shell->ctx->temp_buff,
				CONFIG_SHELL_ARGC_MAX);

	if (*argc > CONFIG_SHELL_ARGC_MAX) {                                     /* 参数过多时不能安全索引本地指针数组。 */
		return false;
	}

	(*argv)[*argc] = NULL;                                                   /* 给解析结果追加空指针终止项。 */

	/* 在非预选模式补全 select 的参数时，忽略 select 自身。 */
	if ((IS_ENABLED(CONFIG_SHELL_CMDS_SELECT) || (CONFIG_SHELL_CMD_ROOT[0] != 0))
	    && (*argc > 0) &&
	    (strcmp("select", (*argv)[0]) == 0) &&
	    !z_shell_in_select_mode(shell)) {
		*argv = *argv + 1;                                               /* 把当前参数数组起点移到待选择的命令。 */
		*argc = *argc - 1;                                               /* 将参数个数与挪动后的起点保持一致。 */
	}

	int space = isspace((int)shell->ctx->cmd_buff[                           /* 读取光标前一字节以判别前缀是否结束。光标在位置 0 时，本表达式仍会访问缓冲区起点之前。 */
						shell->ctx->cmd_buff_pos - 1]);

	/* 空行或仍在键入第一个词时，直接枚举根层的候选。 */
	if ((*argc == 0) || ((space == 0) && (*argc == 1))) {
		*complete_arg_idx = Z_SHELL_CMD_ROOT_LVL;                        /* 根层候选对应第一个命令词。 */
		*cmd = selected_cmd_get(shell);                                  /* 取得全局根层或此实例预选的根命令。 */
		return true;
	}

	search_argc = space ? *argc : *argc - 1;                                 /* 尾词未结束时，查找父节点不包含这个前缀。 */

	*cmd = z_shell_get_last_command(selected_cmd_get(shell), search_argc,    /* 按已完成的命令路径取得补全所需的最后一个父节点。 */
					*argv, complete_arg_idx,	d_entry,
					false);

	if ((*cmd == NULL) && (search_argc != 0)) {                              /* 非空命令路径无法解析，没有可列出的下级候选。 */
		return false;
	}

	return true;                                                             /* 父表及前缀位置已经准备好。 */
}

/**
  * @brief  比较候选命令的开头与用户已输入的前缀。
  * @param  candidate：[输入] 待检查的命令名。
  * @param  str：[输入] 用于比较的前缀。
  * @param  len：[输入] 参与比较的前缀字节数。
  * @retval 前 len 字节相同为 true。
  */
static inline bool is_completion_candidate(const char *candidate,
					   const char *str, size_t len)
{
	return (strncmp(candidate, str, len) == 0) ? true : false;    /* 比较指定字节数，使完整名称也能匹配其更短的输入前缀。 */
}

/**
  * @brief  遍历同一级命令表，统计前缀符合的各项。
  * @param  shell：[输入] 接口保留的 Shell 实例，本函数不读取。
  * @param  cmd：[输入] 父命令地址；NULL 枚举根命令。
  * @param  incompl_cmd：[输入] 待补全的命令前缀。
  * @param  first_idx：[输出] 有匹配时写入第一项的表下标。
  * @param  cnt：[输出] 匹配项数。
  * @param  longest：[输出] 最长匹配名称的字节数。
  * @retval 无。
  */
static void find_completion_candidates(const struct shell *shell,
				       const struct shell_static_entry *cmd,
				       const char *incompl_cmd,
				       size_t *first_idx, size_t *cnt,
				       uint16_t *longest)
{
	const struct shell_static_entry *candidate;                               /* 保存本次枚举到的候选项地址。 */
	struct shell_static_entry dloc;                                           /* 为动态生成的候选提供临时条目存储。 */
	size_t incompl_cmd_len;                                                   /* 用户已输入的命令名前缀字节数。 */
	size_t idx = 0;                                                           /* 从当前命令表的首项开始枚举。 */

	incompl_cmd_len = z_shell_strlen(incompl_cmd);                            /* 固定此前缀长度，供各候选复用。 */
	*longest = 0U;                                                            /* 匹配前将最大显示宽度归零。 */
	*cnt = 0;                                                                 /* 从零累计本次匹配数。 */

	/* 逐项枚举命令，仅统计名称以前缀开头的条目。 */
	while ((candidate = z_shell_cmd_get(cmd, idx, &dloc)) != NULL) {
		bool is_candidate;                                                /* 记录当前表项是否满足名称前缀。 */
		is_candidate = is_completion_candidate(candidate->syntax,         /* 用候选的语法名与用户已输入文本比较。 */
						incompl_cmd, incompl_cmd_len);
		if (is_candidate) {                                               /* 只把命中项计入后续排版与选择。 */
			*longest = Z_MAX(strlen(candidate->syntax), *longest);    /* 更新当前候选中的最长名称。 */
			if (*cnt == 0) {                                          /* 找到第一项时保存其表下标，便于再次枚举。 */
				*first_idx = idx;                                 /* 后续打印从此处开始。 */
			}
			(*cnt)++;                                                 /* 匹配数决定唯一补全还是显示列表。 */
		}

		idx++;                                                            /* 下一次检查同级表的后一项。 */
	}
}

/**
  * @brief  把唯一匹配项的余下名称补入命令行，并把光标定位到下一个参数。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  cmd：[输入] 待查命令集的父项；NULL 从根表枚举。
  * @param  arg：[输入] 已经输入的命令名前缀。
  * @param  subcmd_idx：[输入] 唯一匹配项在该表中的下标。
  * @retval 无。
  */
static void autocomplete(const struct shell *shell,
			 const struct shell_static_entry *cmd,
			 const char *arg,
			 size_t subcmd_idx)
{
	const struct shell_static_entry *match;                  /* 保存确定匹配的命令条目。 */
	uint16_t cmd_len;                                        /* 完整命令名的字节数。 */
	uint16_t arg_len = z_shell_strlen(arg);                  /* 当前已输入的前缀字节数。 */

	/* 此时尚未执行命令，可借用 active_cmd 存放动态返回的条目，节省栈空间。 */
	match = z_shell_cmd_get(cmd, subcmd_idx, &shell->ctx->active_cmd);
	__ASSERT_NO_MSG(match != NULL);                          /* 上一阶段已定位唯一候选，此处应能再次取到。 */
	cmd_len = z_shell_strlen(match->syntax);                 /* 计算还缺哪些字符。 */

	/* 仅列候选而未开启自动补全时，完整匹配仍可插入参数分隔空格。 */
	if (!IS_ENABLED(CONFIG_SHELL_TAB_AUTOCOMPLETION)) {
		if (cmd_len == arg_len) {                        /* 只对已经输入完整名称的情况添加分隔空格。 */
			z_shell_op_char_insert(shell, ' ');      /* 为紧随其后的参数腾出位置。 */
		}
		return;
	}

	/* 将唯一名称中尚未输入的后缀插入编辑缓冲。 */
	if (cmd_len != arg_len) {
		z_shell_op_completion_insert(shell,              /* 从已输入的字节数处开始复制名称。 */
					     match->syntax + arg_len,
					     cmd_len - arg_len);
	}

	/* 若光标处没有空白则插入一个，否则越过已存在的分隔字符。 */
	if (!isspace((int) shell->ctx->cmd_buff[
					shell->ctx->cmd_buff_pos])) {
		if (z_flag_insert_mode_get(shell)) {             /* 临时切换编辑模式，保护空格右侧原有内容。 */
			z_flag_insert_mode_set(shell, false);    /* 使用字符插入操作需要的当前编辑标志。 */
			z_shell_op_char_insert(shell, ' ');      /* 插入命令与下一参数之间的空格。 */
			z_flag_insert_mode_set(shell, true);     /* 恢复用户原先选择的编辑模式。 */
		} else {
			z_shell_op_char_insert(shell, ' ');      /* 直接按当前模式添加分隔空格。 */
		}
	} else {
		z_shell_op_cursor_move(shell, 1);                /* 光标越过已有分隔空格，准备输入下一个参数。 */
	}
}

/**
  * @brief  在给定长度内求两个字符串的相同前缀长度。
  * @param  s1：[输入] 第一段可读字符串。
  * @param  s2：[输入] 第二段可读字符串。
  * @param  n：[输入] 最多比较的字节数。
  * @retval 相同前缀的字节数。
  */
static size_t str_common(const char *s1, const char *s2, size_t n)
{
	size_t common = 0;    /* 从零累计两个名称共同的开头长度。 */

	/* 依次比较字符，在达到上限、字符串结束或遇到首个差异时停止。 */
	while ((n > 0) && (*s1 == *s2) && (*s1 != '\0')) {
		s1++;         /* 前进到第一段文本的下一字节。 */
		s2++;         /* 同步推进第二段文本。 */
		n--;          /* 本轮比较占用一个字节的上限。 */
		common++;     /* 已确认相同的前缀延长一字节。 */
	}

	return common;        /* 返回实际可共同补全的长度。 */
}

/**
  * @brief  分列打印符合前缀的全部候选，再恢复编辑提示符。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  cmd：[输入] 枚举的父项；NULL 指根命令表。
  * @param  str：[输入] 候选前缀，可为 NULL。
  * @param  first：[输入] 第一个匹配项的表下标。
  * @param  cnt：[输入] 要打印的匹配项总数。
  * @param  longest：[输入] 最长匹配项名称的字节数。
  * @retval 无。
  */
static void tab_options_print(const struct shell *shell,
			      const struct shell_static_entry *cmd,
			      const char *str, size_t first, size_t cnt,
			      uint16_t longest)
{
	const struct shell_static_entry *match;                                /* 暂存当前被打印候选的条目。 */
	size_t str_len = z_shell_strlen(str);                                  /* 计算用于筛选候选的前缀长度。 */
	size_t idx = first;                                                    /* 从已确定的第一个命中项开始。 */

	/* 开始枚举前先重置多列输出的计数器。 */
	tab_item_print(shell, SHELL_INIT_OPTION_PRINTER, longest);

	/* 逐项查表，直到打印完此前统计的所有匹配项。 */
	while (cnt) {
		match = z_shell_cmd_get(cmd, idx, &shell->ctx->active_cmd);    /* 补全过程没有正在执行的命令，借用上下文存放动态项以节约栈空间。 */
		__ASSERT_NO_MSG(match != NULL);                                /* 统计阶段已经确认后续还有候选。 */
		idx++;                                                         /* 下一次枚举从后一个表项开始。 */
		if (str && match->syntax &&                                    /* 过滤夹在匹配项之间的其他名称。 */
		    !is_completion_candidate(match->syntax, str, str_len)) {
			continue;                                              /* 不匹配时保留待打印数量，继续查下一项。 */
		}

		tab_item_print(shell, match->syntax, longest);                 /* 按本批最长名称排进当前列。 */
		cnt--;                                                         /* 只对已打印的匹配项减计数。 */
	}

	z_cursor_next_line_move(shell);                                        /* 输出结束后换行。 */
	z_shell_print_prompt_and_cmd(shell);                                   /* 恢复提示符及用户未提交的文本。 */
}

/**
  * @brief  在所有候选名称间求可安全补入的最长公共前缀。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  cmd：[输入] 枚举所在父项，可为 NULL。
  * @param  str：[输出] 写回第一个候选的名称指针，供调用方立即取补全片段。
  * @param  first：[输入] 第一项的下标。
  * @param  cnt：[输入] 匹配项数，须大于 1。
  * @param  arg_len：[输入] 已输入前缀的字节数。
  * @retval 共同前缀的字节数。
  */
static uint16_t common_beginning_find(const struct shell *shell,
				   const struct shell_static_entry *cmd,
				   const char **str,
				   size_t first, size_t cnt, uint16_t arg_len)
{
	struct shell_static_entry dynamic_entry;                                   /* 保存动态枚举到的第一个候选条目。 */
	const struct shell_static_entry *match;                                    /* 当前比较的首个匹配项地址。 */
	uint16_t common = UINT16_MAX;                                              /* 逐步取最短公共长度，初值为可表示的最大值。 */
	size_t idx = first + 1;                                                    /* 从第一个匹配项之后开始查找其他候选。 */

	__ASSERT_NO_MSG(cnt > 1);                                                  /* 此流程用于有多个匹配的情形。 */

	/* 取得首个候选作为后续前缀比较的基准。 */
	match = z_shell_cmd_get(cmd, first, &dynamic_entry);
	__ASSERT_NO_MSG(match);                                                    /* 前面统计过匹配项，首项必须存在。 */
	strncpy(shell->ctx->temp_buff, match->syntax,                              /* 保留基准名称的副本，动态枚举后仍可比较。 */
			sizeof(shell->ctx->temp_buff) - 1);

	*str = match->syntax;                                                      /* 把补全字符的来源返回给调用者。 */

	/* 将之后的匹配名称与基准比较，用最短的相同前缀限制补全长度。 */
	while (cnt > 1) {
		struct shell_static_entry dynamic_entry2;                          /* 临时接收下一项动态命令。 */
		const struct shell_static_entry *match2;                           /* 本次枚举出的候选地址。 */
		int curr_common;                                                   /* 记录与基准名称相同的前缀长度。 */

		match2 = z_shell_cmd_get(cmd, idx++, &dynamic_entry2);             /* 按递增下标取得后续表项。 */
		if (match2 == NULL) {                                              /* 表已枚举完毕时结束比较。 */
			break;
		}

		curr_common = str_common(shell->ctx->temp_buff, match2->syntax,    /* 计算这个候选和基准名称共同的起始字节数。 */
					 UINT16_MAX);
		if ((arg_len == 0U) || (curr_common >= arg_len)) {                 /* 只有满足用户前缀的候选才影响最终结果。 */
			--cnt;                                                     /* 又处理完一个已统计的匹配项。 */
			common = (curr_common < common) ? curr_common : common;    /* 保留所有匹配项都拥有的共同前缀。 */
		}
	}

	return common;                                                             /* 返回能安全插入的前缀长度。 */
}

/**
  * @brief  多个候选共享更长前缀时，只补齐所有候选一致的部分。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  cmd：[输入] 候选所在父项，可为 NULL。
  * @param  arg：[输入] 用户已经输入的前缀。
  * @param  first：[输入] 首个候选下标。
  * @param  cnt：[输入] 候选数，须大于 1。
  * @retval 无。
  */
static void partial_autocomplete(const struct shell *shell,
				 const struct shell_static_entry *cmd,
				 const char *arg,
				 size_t first, size_t cnt)
{
	const char *completion;                                                    /* 接收公共前缀所依附的名称指针。 */
	uint16_t arg_len = z_shell_strlen(arg);                                    /* 后续补全会跳过用户已输入的部分。 */
	uint16_t common = common_beginning_find(shell, cmd, &completion, first,    /* 比较匹配名称，确定共同的字节数。 */
					     cnt, arg_len);

	if (!IS_ENABLED(CONFIG_SHELL_TAB_AUTOCOMPLETION)) {                        /* 只列选项的构建在此结束。 */
		return;
	}

	if (common) {                                                              /* 只有存在共同前缀时才尝试插入。 */
		z_shell_op_completion_insert(shell, &completion[arg_len],          /* 从用户前缀的末尾开始补至各候选的首个分歧处。 */
					     common - arg_len);
	}
}

/**
  * @brief  检查最深匹配命令的参数个数，调用命令表登记的处理函数。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  argc：[输入] 从命令自身开始计数，包含命令名。
  * @param  argv：[输入] 指向以命令名为首项的字符串指针数组，末项后有 NULL。
  * @param  help_entry：[输入] 最近可用于输出帮助的命令项，可为 NULL。
  * @retval 返回处理函数的结果；参数数量不符为 -EINVAL，没有可运行处理函数时为 -ENOEXEC 或 SHELL_CMD_HELP_PRINTED。
  * @note   普通串口输入在 Shell 线程内同步调用处理函数。调用前释放写互斥锁，回调返回后重新获取。
  */
static int exec_cmd(const struct shell *shell, size_t argc, const char **argv,
		    const struct shell_static_entry *help_entry)
{
	int ret_val = 0;                                                          /* 初值零允许没有数量预检的命令直接执行。 */

	/* 选中的层级没有可执行处理函数时，优先按配置显示帮助。 */
	if (shell->ctx->active_cmd.handler == NULL) {
		if ((help_entry != NULL) && IS_ENABLED(CONFIG_SHELL_HELP)) {      /* 启用帮助且有候选条目时才尝试输出。 */
			if (help_entry->help == NULL) {                           /* 帮助字段为空表示该项也不能给出用法。 */
				return -ENOEXEC;                                  /* 通知调用方此输入无可执行动作。 */
			}
			if (help_entry->help != shell->ctx->active_cmd.help) {    /* 必要时用含帮助文本的匹配项取代活动项。 */
				shell->ctx->active_cmd = *help_entry;             /* 复制帮助描述供打印器统一读取。 */
			}
			shell_internal_help_print(shell);                         /* 显示可用参数与子命令。 */
			return SHELL_CMD_HELP_PRINTED;                            /* 把“已显示帮助”作为专用结果交回。 */
		} else {
			z_shell_fprintf(shell, SHELL_ERROR,                       /* 提醒用户继续提供可执行的子命令。 */
					SHELL_MSG_SPECIFY_SUBCOMMAND);
			return -ENOEXEC;                                          /* 结束此次缺少处理函数的分派。 */
		}
	}

	/* 部分宏允许跳过数量预检，mandatory 非零时才使用下列规则。 */
	if (shell->ctx->active_cmd.args.mandatory) {
		uint32_t mand = shell->ctx->active_cmd.args.mandatory;            /* 必需项数包含命令自身。 */
		uint8_t opt8 = shell->ctx->active_cmd.args.optional;              /* 读取条目保存的可选项上限或特殊值。 */
		uint32_t opt = (opt8 == SHELL_OPT_ARG_CHECK_SKIP) ?               /* 允许跳过可选数量上限时以较大值表示。 */
				UINT16_MAX : opt8;
		bool in_range = (argc >= mand) && (argc <= (mand + opt));         /* 检查从处理命令名起算的实际 argc 是否在允许范围。 */

		ret_val = cmd_precheck(shell, in_range);                          /* 记录预检结果，失败时不调用处理函数。 */
	}

	/* 检查通过后才进入命令上下文，调用前交还终端写互斥锁。 */
	if (!ret_val) {
#if CONFIG_SHELL_GETOPT
		z_shell_getopt_init(&shell->ctx->getopt_state);                   /* 启用了 getopt 时，从新命令的第一个选项重新开始解析。 */
#endif

		z_flag_cmd_ctx_set(shell, true);                                  /* 允许公共输出函数识别当前命令回调上下文。 */
		k_mutex_unlock(&shell->ctx->wr_mtx);                              /* 回调可在其他线程借用终端输出，持锁跨越回调可能产生死锁。 */
		ret_val = shell->ctx->active_cmd.handler(shell, argc,             /* 通过已匹配条目保存的 handler 传入本实例和该命令对应的 argv 切片。 */
							 (char **)argv);
		k_mutex_lock(&shell->ctx->wr_mtx, K_FOREVER);                     /* 回调结束后重新取得线程编辑与输出互斥锁。 */
		z_flag_cmd_ctx_set(shell, false);                                 /* 恢复普通 Shell 上下文标志。 */
	}

	return ret_val;                                                           /* 原样传回预检错误或命令处理结果。 */
}

/**
  * @brief  保存当前最深的可执行条目及帮助条目，并处理原样传参模式。
  * @param  entry：[输入] 本级已匹配条目。
  * @param  active_cmd：[输出] 有处理函数时复制完整命令描述。
  * @param  help_entry：[输出] 有帮助文字时复制条目。
  * @param  lvl：[输入输出] 当前命令在 argv 中的层级。
  * @param  handler_lvl：[输出] 最深处理函数所对应的层级。
  * @param  args_left：[输入输出] 还需逐项解析的数量；原样模式将剩余文本交给处理函数。
  * @retval 无。
  */
static void active_cmd_prepare(const struct shell_static_entry *entry,
				struct shell_static_entry *active_cmd,
				struct shell_static_entry *help_entry,
				size_t *lvl, size_t *handler_lvl,
				size_t *args_left)
{
	/* 带 handler 的节点成为迄今最深的可执行项。 */
	if (entry->handler) {
		*handler_lvl = *lvl;                               /* 记住命令名在整行参数数组中的位置。 */
		*active_cmd = *entry;                              /* 复制描述，后续枚举动态条目不会覆盖它。 */
		if ((entry->subcmd == NULL)                        /* 无子命令且要求原样参数时，剩余文本需按 RAW 约定交给处理器。 */
		    && entry->args.optional == SHELL_OPT_ARG_RAW) {
			*args_left = entry->args.mandatory - 1;    /* 扣除命令名后，只按规则解析必要数量的词。 */
			*lvl = *lvl + 1;                           /* 向后移动位置，给剩余原始文本保留参数位置。 */
		}
	}
	if (entry->help) {                                         /* 条目可只提供帮助；单独保存最近一份可显示的说明。 */
		*help_entry = *entry;                              /* 复制整个帮助描述以保留对应语法及子命令集。 */
	}
}

/**
  * @brief  检查通配符展开后是否又遇到带处理函数的节点。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  found：[输入] 此前已成功处理过通配符。
  * @param  entry：[输入] 当前匹配的命令项。
  * @retval 允许继续时为 true，存在冲突则打印原因并返回 false。
  */
static bool wildcard_check_report(const struct shell *shell, bool found,
				  const struct shell_static_entry *entry)
{
	/* 通配符已经展开时，再遇到带处理函数的节点会要求执行多次，拒绝这一组合。 */
	if (IS_ENABLED(CONFIG_SHELL_WILDCARD) && found && entry->handler) {
		z_shell_op_cursor_end_move(shell);     /* 先移到输入行末再输出错误。 */
		z_shell_op_cond_next_line(shell);      /* 必要时换行保持错误消息可辨。 */

		z_shell_fprintf(shell, SHELL_ERROR,    /* 明确告知用户请求的调用有歧义。 */
			"Error: requested multiple function executions\n");
		return false;                          /* 要求分派器停止执行。 */
	}

	return true;                                   /* 普通条目或未展开通配符时允许继续。 */
}

/**
  * @brief  拆分输入行并从根命令逐级匹配，执行最深的可调用命令。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 返回命令处理结果；解析或执行条件不满足为 -ENOEXEC，帮助输出为 SHELL_CMD_HELP_PRINTED，参数预检失败为 -EINVAL。
  * @note   一般参数按引号、转义及空白解析；支持 RAW 的命令可保留尾部原始文本。对 app spi2file 源路径 目标文件名，handler 的 argv 从 spi2file 起算。
  */
static int execute(const struct shell *shell)
{
	struct shell_static_entry dloc;                                             /* 动态查表可临时生成条目，其内容在需要保留时复制到上下文。 */
	const char *argv[CONFIG_SHELL_ARGC_MAX + 1];                                /* 分词后的各指针借用命令缓冲中的字符串，额外一格用于终止指针 NULL。 */
	const struct shell_static_entry *parent = selected_cmd_get(shell);          /* 初始父项为预选命令，通常为 NULL，对应全局根命令表。 */
	const struct shell_static_entry *entry = NULL;                              /* 保存本轮 z_shell_find_cmd 找到的根项或子项。 */
	struct shell_static_entry help_entry;                                       /* 暂存最近一个带帮助文字的条目。 */
	size_t cmd_lvl = 0;                                                         /* 已消费的命令词或参数在整行中的位置。 */
	size_t cmd_with_handler_lvl = 0;                                            /* 记录最深 handler 对应的 argv 起点，剔除上级命令名。 */
	bool wildcard_found = false;                                                /* 是否发生过需要重建参数数组的通配符展开。 */
	size_t argc = 0, args_left = SIZE_MAX;                                      /* argc 是本次拆分的结果项数，args_left 限制后续解析（RAW 模式会收紧）。 */
	char quote;                                                                 /* 记录尚未配对的引号，供输入诊断。 */
	const char **argvp;                                                         /* 指向下一次分词要写入的参数指针位置。 */
	char *cmd_buf = shell->ctx->cmd_buff;                                       /* 输入行属于本实例；分词可在原缓冲中写入字符串结束符。 */
	bool has_last_handler = false;                                              /* 一旦命令路径结束，后续令牌直接用作实参。 */

	/* 把光标移下输入行，清除上次命令信息，再保存本次解析所需的历史和通配符状态。 */
	z_shell_op_cursor_end_move(shell);
	if (!z_shell_cursor_in_empty_line(shell)) {                                 /* 当前屏幕行有内容时，处理结果从下一行输出。 */
		z_cursor_next_line_move(shell);
	}

	memset(&shell->ctx->active_cmd, 0, sizeof(shell->ctx->active_cmd));         /* 清除上一次分派留下的处理函数、语法和帮助指针。 */

	if (IS_ENABLED(CONFIG_SHELL_HISTORY)) {                                     /* 历史保存应在分词改写字符串之前完成。 */
		z_shell_cmd_trim(shell);                                            /* 整理待保存命令的边界空白。 */
		history_put(shell, shell->ctx->cmd_buff,                            /* 按当前有效长度写入历史。 */
			    shell->ctx->cmd_buff_len);
	}

	if (IS_ENABLED(CONFIG_SHELL_WILDCARD)) {                                    /* 为可选的通配符展开保存本次输入的状态。 */
		z_shell_wildcard_prepare(shell);
	}

	/* 已选择命令时，以该命令作隐含前缀；普通模式直接从输入的首词开始。 */
	if (parent != NULL) {
		argv[0] = parent->syntax;                                           /* 把预选根名称放在参数数组首项。 */
		argv[1] = cmd_buf;                                                  /* 真正输入的文本从第二项开始解析。 */
		argvp = &argv[1];                                                   /* 后续分词从预留根名之后写入。 */
		active_cmd_prepare(parent, &shell->ctx->active_cmd, &help_entry,    /* 若预选项自身可执行，也先把它记作候选处理器。 */
				   &cmd_lvl, &cmd_with_handler_lvl, &args_left);
		cmd_lvl++;                                                          /* 跳过已经记录的预选根项。 */
	} else {
		help_entry.help = NULL;                                             /* 普通模式尚未遇到带帮助文字的条目。 */
		argvp = &argv[0];                                                   /* 第一个输入词就占据 argv[0]。 */
	}

	/* 每次最多拆出一个词和待处理的余串，先在根表找 app，再到 app 的子表找 spi2file。 */
	while ((argc != 1) && (cmd_lvl < CONFIG_SHELL_ARGC_MAX)
		&& args_left > 0) {
		quote = z_shell_make_argv(&argc, argvp, cmd_buf, 2);                /* 就地解析引号与转义并分出下一项；限制为 2，以便本级命令决定余串是否继续分词。 */
		cmd_buf = (char *)argvp[1];                                         /* 将余串的起点交给下一次迭代。 */

		if (argc == 0) {                                                    /* 未得到任何参数时没有命令可运行。 */
			return -ENOEXEC;                                            /* 以无法执行结束此次请求。 */
		} else if ((argc == 1) && (quote != 0)) {                           /* 只余最后一项且引号未闭合时提示输入错误。 */
			z_shell_fprintf(shell, SHELL_ERROR,                         /* 将未配对的引号写入诊断。 */
					"not terminated: %c\n", quote);
			return -ENOEXEC;                                            /* 命令行不完整，停止分派。 */
		}

		if (IS_ENABLED(CONFIG_SHELL_HELP) && (cmd_lvl > 0) &&               /* 进入命令层次后才把帮助选项作为终端帮助请求处理。 */
		    z_shell_help_request(argvp[0])) {
			if (help_entry.help) {                                      /* 只对已经记录了用法的命令显示帮助。 */
				shell->ctx->active_cmd = help_entry;                /* 把最近可用的帮助项设为显示目标。 */
				shell_internal_help_print(shell);                   /* 打印该命令与其子项说明。 */
				return SHELL_CMD_HELP_PRINTED;                      /* 向调用方报告帮助已经输出。 */
			}

			z_shell_fprintf(shell, SHELL_ERROR,                         /* 没有帮助条目时请求用户补齐子命令。 */
					SHELL_MSG_SPECIFY_SUBCOMMAND);
			return -ENOEXEC;                                            /* 此帮助请求没有可显示的命令，结束解析。 */
		}

		/* 可选的通配符展开作用在根名之后的参数，结果可能改变后续命令查找。 */
		if (IS_ENABLED(CONFIG_SHELL_WILDCARD) && (cmd_lvl > 0)) {
			enum shell_wildcard_status status;                          /* 保存本参数的通配符匹配状态。 */

			status = z_shell_wildcard_process(shell, entry,             /* 检查本词能否在当前命令范围内展开。 */
							  argvp[0]);
			if (status == SHELL_WILDCARD_CMD_NO_MATCH_FOUND) {          /* 含通配符却无候选时结束逐级解析。 */
				break;
			}

			if (status != SHELL_WILDCARD_NOT_FOUND) {                   /* 已展开的词交给通配符路径处理，未发现通配符则继续普通查表。 */
				++cmd_lvl;                                          /* 为展开结果推进词位置。 */
				wildcard_found = true;                              /* 最终需重新对展开后的缓冲分词。 */
				continue;                                           /* 跳过对当前通配词的普通名称查找。 */
			}
		}

		/* 尚在匹配命令路径时，才调用 shell_utils.c 的逐项名称比较。 */
		if (has_last_handler == false) {
			entry = z_shell_find_cmd(parent, argvp[0], &dloc);          /* parent 为 NULL 时查根表，匹配 app 后从其 acts_app_cmds 中查下一个词。 */
		}

		argvp++;                                                            /* 下一轮把参数指针写到后一个数组元素。 */
		args_left--;                                                        /* 计入本轮已经解析的一个参数。 */
		if (entry) {                                                        /* 找到当前层时更新可执行项、帮助和下一层的父节点。 */
			if (wildcard_check_report(shell, wildcard_found, entry)     /* 检查是否会因通配符组合造成多次执行。 */
				== false) {
				return -ENOEXEC;                                    /* 歧义输入不进入处理器。 */
			}

			active_cmd_prepare(entry, &shell->ctx->active_cmd,          /* 保存此层的 handler 和帮助；普通数据参数不会覆盖此前选中的处理器。 */
					  &help_entry, &cmd_lvl,
					  &cmd_with_handler_lvl, &args_left);
			parent = entry;                                             /* 下一个命令词将在当前条目的子命令表中查找。 */
		} else {
			if (cmd_lvl == 0 &&                                         /* 首词未命中且无预选可执行节点时，向终端报告未知命令。 */
				(!z_shell_in_select_mode(shell) ||
				 shell->ctx->selected_cmd->handler == NULL)) {
				z_shell_fprintf(shell, SHELL_ERROR,                 /* 输出未识别的根命令名称。 */
						"%s%s\n", argv[0],
						SHELL_MSG_CMD_NOT_FOUND);
			}

			has_last_handler = true;                                    /* 后续令牌作为实参保留，无需再查下级命令表。 */
		}

		if (args_left || (argc == 2)) {                                     /* 尚有解析额度或余串时需计入当前词。 */
			cmd_lvl++;                                                  /* 推进本行的参数计数和层级位置。 */
		}

	}

	/* 用“参数数组已满且仍有余串”区分合法的最大参数数与真正的超限输入。 */
	if ((cmd_lvl >= CONFIG_SHELL_ARGC_MAX) && (argc == 2)) {
		z_shell_fprintf(shell, SHELL_ERROR, "%s\n",                         /* 输出超出 CONFIG_SHELL_ARGC_MAX 的诊断。 */
				SHELL_MSG_TOO_MANY_ARGS);
		return -ENOEXEC;                                                    /* 拒绝截断执行参数过多的命令。 */
	}

	/* 有通配符展开时，原 argv 指针可能失效，基于最终缓冲重新建立它们。 */
	if (IS_ENABLED(CONFIG_SHELL_WILDCARD) && wildcard_found) {
		z_shell_wildcard_finalize(shell);                                   /* 把展开后的命令写回当前命令缓冲。 */
		(void)z_shell_make_argv(&cmd_lvl,                                   /* 对展开后的完整输入再次分词；预选根可能占用第零项。 */
					&argv[selected_cmd_get(shell) ? 1 : 0],
					shell->ctx->cmd_buff,
					CONFIG_SHELL_ARGC_MAX);

		if (selected_cmd_get(shell)) {                                      /* 如果实例已有预选根，要将它计入总项数。 */
			cmd_lvl++;                                                  /* 为隐含根名补上一项。 */
		}
	}

	/* 在参数数组尾部写 NULL，再从最深 handler 的位置截取 argc 和 argv。 */
	argv[cmd_lvl] = NULL;
	return exec_cmd(shell, cmd_lvl - cmd_with_handler_lvl,                      /* 例如 app spi2file 源路径 目标名传入 argc=3，argv[0] 为 spi2file，而两个业务实参为 argv[1] 和 argv[2]。 */
			&argv[cmd_with_handler_lvl], &help_entry);
}

/**
  * @brief  响应一次 Tab，根据有效候选数补齐名称或列出所有选择。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
static void tab_handle(const struct shell *shell)
{
	const char *__argv[CONFIG_SHELL_ARGC_MAX + 1];                          /* 容纳分词指针及末尾 NULL。 */
	struct shell_static_entry d_entry;                                      /* 为回调方式产生的动态命令描述提供临时空间。 */
	const struct shell_static_entry *cmd;                                   /* 接收补全所在层级的父命令。 */
	const char **argv = __argv;                                             /* 当前参数视图可由 tab_prepare 向后移动。 */
	size_t first = 0;                                                       /* 第一个符合前缀的条目下标。 */
	size_t arg_idx;                                                         /* 当前词在解析后数组中的位置。 */
	uint16_t longest;                                                       /* 保存候选名称最大宽度供排版。 */
	size_t argc;                                                            /* 准备阶段得到的参数项数。 */
	size_t cnt;                                                             /* 当前词匹配到的候选项数。 */

	/* 先识别当前命令层级和待补全前缀。 */
	bool tab_possible = tab_prepare(shell, &cmd, &argv, &argc, &arg_idx,
					&d_entry);

	if (tab_possible == false) {                                            /* 前缀无效或缓冲已满时，本次 Tab 不修改输入。 */
		return;
	}

	find_completion_candidates(shell, cmd, argv[arg_idx], &first, &cnt,     /* 按现有前缀统计同表所有匹配项。 */
				   &longest);

	/* 唯一候选可完整补齐，多项时先列出名称再补公共部分。 */
	if (cnt == 1) {
		autocomplete(shell, cmd, argv[arg_idx], first);                 /* 按首个候选的实际名称完成输入。 */
	} else if (cnt > 1) {                                                   /* 多候选之间只有共同前缀能自动补入。 */
		tab_options_print(shell, cmd, argv[arg_idx], first, cnt,        /* 输出按列排列的候选名称。 */
				  longest);
		partial_autocomplete(shell, cmd, argv[arg_idx], first, cnt);    /* 在保留所有候选可能性的情况下延长输入。 */
	}
}

/**
  * @brief  处理 ESC 后的 Alt 快捷键，支持跨词移动和恢复根命令。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  data：[输入] ESC 之后收到的键值。
  * @retval 无。
  */
static void alt_metakeys_handle(const struct shell *shell, char data)
{
	/* 仅当配置支持快捷键时才解释 ESC 之后的字节。 */
	if (!IS_ENABLED(CONFIG_SHELL_METAKEYS)) {
		return;
	}
	if (data == SHELL_VT100_ASCII_ALT_B) {                                                      /* Alt+B 使光标回到前一个词。 */
		z_shell_op_cursor_word_move(shell, -1);                                             /* 以负方向移动一个词。 */
	} else if (data == SHELL_VT100_ASCII_ALT_F) {                                               /* Alt+F 使光标向后移动一个词。 */
		z_shell_op_cursor_word_move(shell, 1);                                              /* 以正方向移动一个词。 */
	} else if (data == SHELL_VT100_ASCII_ALT_R &&                                               /* Alt+R 在启用命令选择功能时可恢复默认根。 */
		   IS_ENABLED(CONFIG_SHELL_CMDS_SELECT)) {
		if (selected_cmd_get(shell) != NULL) {                                              /* 已存在预选根才需要重画并重置。 */
			z_shell_cmd_line_erase(shell);                                              /* 擦掉旧的提示符和编辑行。 */
			z_shell_fprintf(shell, SHELL_WARNING,                                       /* 提示默认根命令集合即将恢复。 */
					"Restored default root commands\n");
			if (CONFIG_SHELL_CMD_ROOT[0]) {                                             /* 有配置的根名时重新定位该命令。 */
				shell->ctx->selected_cmd = root_cmd_find(CONFIG_SHELL_CMD_ROOT);    /* 使用编译配置的默认根。 */
			} else {
				shell->ctx->selected_cmd = NULL;                                    /* 没有指定根名时恢复整张全局根表。 */
			}
			z_shell_print_prompt_and_cmd(shell);                                        /* 按恢复后的选择重画提示和当前行。 */
		}
	}
}

/**
  * @brief  把 ASCII 控制字节分派到光标、历史或命令行编辑操作。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  data：[输入] 收到的控制字符。
  * @retval 无。
  */
static void ctrl_metakeys_handle(const struct shell *shell, char data)
{
	/* 关闭控制快捷键时保留普通输入行为。 */
	if (!IS_ENABLED(CONFIG_SHELL_METAKEYS)) {
		return;
	}

	/* 根据控制字节执行行编辑、历史遍历或终端操作；Ctrl+C 清理当前输入。 */
	switch (data) {
	case SHELL_VT100_ASCII_CTRL_A:                                /* Ctrl+A 跳到本行开头。 */
		z_shell_op_cursor_home_move(shell);                   /* 将光标移至提示符后。 */
		break;

	case SHELL_VT100_ASCII_CTRL_B:                                /* Ctrl+B 向左移动一个字符。 */
		z_shell_op_left_arrow(shell);                         /* 向前一个字符移动。 */
		break;

	case SHELL_VT100_ASCII_CTRL_C:                                /* Ctrl+C 丢弃尚未提交的输入。 */
		z_shell_op_cursor_end_move(shell);                    /* 光标先回到原有行末。 */
		if (!z_shell_cursor_in_empty_line(shell)) {           /* 行末非空时在下一行显示新的提示。 */
			z_cursor_next_line_move(shell);               /* 将终端光标移到下一行。 */
		}
		z_flag_history_exit_set(shell, true);                 /* 标记旧的历史浏览应当结束。 */
		state_set(shell, SHELL_STATE_ACTIVE);                 /* 重新进入活动状态，清空输入并显示提示符。 */
		break;

	case SHELL_VT100_ASCII_CTRL_D:                                /* Ctrl+D 向前删除。 */
		z_shell_op_char_delete(shell);                        /* 移除光标当前字符。 */
		break;

	case SHELL_VT100_ASCII_CTRL_E:                                /* Ctrl+E 跳到本行末尾。 */
		z_shell_op_cursor_end_move(shell);                    /* 将光标定位到编辑内容末端。 */
		break;

	case SHELL_VT100_ASCII_CTRL_F:                                /* Ctrl+F 向右移动一个字符。 */
		z_shell_op_right_arrow(shell);                        /* 光标移向下一个字符。 */
		break;

	case SHELL_VT100_ASCII_CTRL_K:                                /* Ctrl+K 移除光标后的内容。 */
		z_shell_op_delete_from_cursor(shell);                 /* 删除从当前位置到行尾的文本。 */
		break;

	case SHELL_VT100_ASCII_CTRL_L:                                /* Ctrl+L 清屏后重画当前输入。 */
		Z_SHELL_VT100_CMD(shell, SHELL_VT100_CURSORHOME);     /* 将终端光标移到屏幕原点。 */
		Z_SHELL_VT100_CMD(shell, SHELL_VT100_CLEARSCREEN);    /* 清除终端屏幕内容。 */
		z_shell_print_prompt_and_cmd(shell);                  /* 保留逻辑输入并重新显示。 */
		break;

	case SHELL_VT100_ASCII_CTRL_N:                                /* Ctrl+N 浏览较新的历史记录。 */
		history_handle(shell, false);                         /* 请求向下取一条历史。 */
		break;

	case SHELL_VT100_ASCII_CTRL_P:                                /* Ctrl+P 浏览较旧的历史记录。 */
		history_handle(shell, true);                          /* 请求向上取一条历史。 */
		break;

	case SHELL_VT100_ASCII_CTRL_U:                                /* Ctrl+U 清空正在编辑的一整行。 */
		z_shell_op_cursor_home_move(shell);                   /* 将光标先移至输入起点。 */
		cmd_buffer_clear(shell);                              /* 清空本地缓冲及长度。 */
		z_flag_history_exit_set(shell, true);                 /* 下一次浏览历史应从新状态开始。 */
		z_clear_eos(shell);                                   /* 擦除屏幕上余下的旧字符。 */
		break;

	case SHELL_VT100_ASCII_CTRL_W:                                /* Ctrl+W 擦除一个词。 */
		z_shell_op_word_remove(shell);                        /* 按词边界删除输入。 */
		z_flag_history_exit_set(shell, true);                 /* 停止沿用此前的历史选择位置。 */
		break;

	default:                                                      /* 未绑定快捷操作的控制字节不改变编辑内容。 */
		break;
	}
}

/**
  * @brief  判定当前 CR 或 LF 是否应提交输入行。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  data：[输入] 本次读到的输入字节。
  * @retval 需要处理一个行结束符时为 true，普通字节或配对中应忽略的字节为 false。
  * @note   连续相同换行字符可分别触发，相邻且不同的 CR/LF 使用上次记号去重。
  */
static bool process_nl(const struct shell *shell, uint8_t data)
{
	/* 普通字节复位上次的换行记录，供下一对 CR/LF 判断。 */
	if ((data != '\r') && (data != '\n')) {
		z_flag_last_nl_set(shell, 0);       /* 清除换行配对状态。 */
		return false;                       /* 当前字节继续走普通或转义键处理。 */
	}

	/* 首次换行或同一种换行再次出现可提交；互补的换行字节不重复提交。 */
	if ((z_flag_last_nl_get(shell) == 0U) ||
	    (data == z_flag_last_nl_get(shell))) {
		z_flag_last_nl_set(shell, data);    /* 记住此次实际处理的 CR 或 LF。 */
		return true;                        /* 允许向上层提交当前输入行。 */
	}

	return false;                               /* 混合的第二个换行字符无需再次执行。 */
}

/* 普通终端编辑只接受七位 ASCII，原始旁路数据不经过此检查。 */
#define SHELL_ASCII_MAX_CHAR (127u)

/**
  * @brief  筛掉普通编辑状态不支持的高位输入字节。
  * @param  data：[输入] 从传输层读到的一个字节。
  * @retval 0 表示七位 ASCII，-EINVAL 表示转换为无符号字节后大于 127。
  */
static inline int ascii_filter(const char data)
{
	return (uint8_t) data > SHELL_ASCII_MAX_CHAR ? -EINVAL : 0;    /* 显式转成无符号再判断，避免 char 的有符号性影响过滤。 */
}

/**
  * @brief  从传输后端读取数据，经终端状态机编辑命令行，行结束时交给 execute。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  * @note   串口中断负责收字节和通知。此处由已获调度的 Shell 线程调用；旁路生效时读到的字节直接交给回调。
  */
static void state_collect(const struct shell *shell)
{
	size_t count = 0;                                                                   /* 接收后端写回的实际字节数，零表示暂时读空。 */
	char data;                                                                          /* 单字节编辑模式从这里取得当前字符。 */

	/* 在当前线程取尽后端缓冲；一个 RXRDY 信号可以对应多个待取字节。 */
	while (true) {
		shell_bypass_cb_t bypass = shell->ctx->bypass;                              /* 本次读取使用当前旁路回调，用户可在回调内撤销它。 */

		/* 有旁路时按块交付未经 ASCII 和命令解析处理的输入。 */
		if (bypass) {
			uint8_t buf[16];                                                    /* 旁路调用可临时接收最多 16 字节。 */

			(void)shell->iface->api->read(shell->iface, buf,                    /* 从同一后端读取原始字节并取得 count。 */
							sizeof(buf), &count);
			if (count) {                                                        /* 至少读到一字节才调用旁路。 */
				bypass(shell, buf, count);                                  /* 将本次实际长度传给回调，使用该栈缓冲仅限本次调用。 */
				if (!(volatile shell_bypass_cb_t *)shell->ctx->bypass) {    /* 旁路回调可能已主动退出，返回后再读实例中的当前设置。 */
					state_set(shell, SHELL_STATE_ACTIVE);               /* 恢复正常编辑状态并重印提示符。 */
				} else {
					continue;                                           /* 旁路仍有效，继续取剩余数据。 */
				}
			}

			return;                                                             /* 本轮旁路读空或已经切回普通模式。 */
		}

		/* 普通模式每次只取一个字节，依次维护转义状态及行缓冲。 */
		(void)shell->iface->api->read(shell->iface, &data,
					      sizeof(data), &count);
		if (count == 0) {                                                           /* 后端已读空时结束本次接收处理。 */
			return;                                                             /* 返回线程主循环，等待新的事件。 */
		}

		if (ascii_filter(data) != 0) {                                              /* 非 ASCII 字节不送入普通编辑器。 */
			continue;                                                           /* 丢弃此字节并继续读取。 */
		}

		switch (shell->ctx->receive_state) {                                        /* 根据上一次保存的 ESC/CSI 状态解释当前字节。 */
		case SHELL_RECEIVE_DEFAULT:                                                 /* 普通输入态可以接受换行、编辑键和可打印字符。 */
			if (process_nl(shell, data)) {                                      /* 遇到有效的行结束符才提交；CR/LF 组合不会执行两遍。 */
				if (!shell->ctx->cmd_buff_len) {                            /* 空行只换行并退出历史浏览。 */
					history_mode_exit(shell);                           /* 回到正常编辑状态。 */
					z_cursor_next_line_move(shell);                     /* 终端换行，准备再次显示提示符。 */
				} else {
					(void)execute(shell);                               /* 在当前 Shell 线程执行完整的已编辑命令，处理器的返回值不阻止下一条输入。 */
				}
				state_set(shell, SHELL_STATE_ACTIVE);                       /* 清理旧命令并打印下一行提示符。 */
				continue;                                                   /* 换行已处理，无需当作普通字符。 */
			}

			/* 非换行字节按键值处理，必要时跟踪后续转义字节。 */
			switch (data) {
			case SHELL_VT100_ASCII_ESC:                                         /* ESC 只是转义序列的首字节。 */
				receive_state_change(shell, SHELL_RECEIVE_ESC);             /* 保存状态，下一字节决定具体按键。 */
				break;

			case '\0':                                                          /* 零字节不作为可编辑文本插入。 */
				break;

			case '\t':                                                          /* Tab 请求补全。 */
				if (z_flag_echo_get(shell) &&                               /* 只有回显和 Tab 功能均开启时才操作编辑行。 */
				    IS_ENABLED(CONFIG_SHELL_TAB)) {
					z_flag_history_exit_set(shell, true);               /* 历史浏览和自动补全共用 temp_buff，在交给补全器前标记退出历史。 */
					tab_handle(shell);                                  /* 依据光标位置解析当前前缀及候选。 */
				}
				break;

			case SHELL_VT100_ASCII_BSPACE:                                      /* 处理终端退格字符。 */
				if (z_flag_echo_get(shell)) {                               /* 交互编辑仅在回显打开时启用此键。 */
					z_flag_history_exit_set(shell, true);               /* 本次编辑使原历史选择失效。 */
					z_shell_op_char_backspace(shell);                   /* 向左退格，删除光标前字节。 */
				}
				break;

			case SHELL_VT100_ASCII_DEL:                                         /* 终端发送 DEL 时，方向受实例的删除模式控制。 */
				if (z_flag_echo_get(shell)) {                               /* 回显开启才对当前行做删除。 */
					z_flag_history_exit_set(shell, true);               /* 标记退出此前历史选择。 */
					if (z_flag_mode_delete_get(shell)) {                /* 检查是否将 DEL 映射为退格。 */
						z_shell_op_char_backspace(shell);           /* 删除光标左侧字节。 */

					} else {
						z_shell_op_char_delete(shell);              /* 删除光标当前位置的字节。 */
					}
				}
				break;

			default:
				if (isprint((int) data)) {                                  /* 可显示字符进入行缓冲，命令名和参数均在此累积。 */
					z_flag_history_exit_set(shell, true);               /* 手工输入使先前历史选择失效。 */
					z_shell_op_char_insert(shell, data);                /* 将该字节插入当前光标位置。 */
				} else if (z_flag_echo_get(shell)) {                        /* 其他控制字节在允许编辑的情况下进入快捷键处理。 */
					ctrl_metakeys_handle(shell, data);                  /* 尝试匹配 Ctrl 组合。 */
				}
				break;
			}
			break;

		/* 上一字节为 ESC，本字节区分 CSI 序列与 Alt 快捷键。 */
		case SHELL_RECEIVE_ESC:
			if (data == '[') {                                                  /* 左方括号开始终端 CSI 键序列。 */
				receive_state_change(shell,                                 /* 记住下一字节为转义序列内容。 */
						SHELL_RECEIVE_ESC_SEQ);
				break;
			} else if (z_flag_echo_get(shell)) {                                /* 其他可编辑 ESC 组合按 Alt 处理。 */
				alt_metakeys_handle(shell, data);                           /* 尝试执行跨词移动或恢复根命令。 */
			}
			receive_state_change(shell, SHELL_RECEIVE_DEFAULT);                 /* 一个 Alt 组合已消费，恢复普通接收。 */
			break;

		/* 解析 ESC [ 后的方向键及各类编辑键。 */
		case SHELL_RECEIVE_ESC_SEQ:
			receive_state_change(shell, SHELL_RECEIVE_DEFAULT);                 /* 默认先恢复普通状态，需要尾部波浪号的键会重新设置状态。 */

			if (!z_flag_echo_get(shell)) {                                      /* 关闭回显时忽略这些光标编辑操作。 */
				continue;
			}

			switch (data) {                                                     /* 按 CSI 序列里的功能字符分派。 */
			case 'A':                                                           /* 上箭头查看较旧的历史记录。 */
				history_handle(shell, true);                                /* 沿历史向上。 */
				break;

			case 'B':                                                           /* 下箭头查看较新的历史记录。 */
				history_handle(shell, false);                               /* 沿历史向下。 */
				break;

			case 'C':                                                           /* 右箭头向后移动一个字符。 */
				z_shell_op_right_arrow(shell);                              /* 向右移动光标。 */
				break;

			case 'D':                                                           /* 左箭头向前移动一个字符。 */
				z_shell_op_left_arrow(shell);                               /* 向左移动光标。 */
				break;

			case '4':                                                           /* ESC [ 4 ~ 的 End 键还需要消耗尾部波浪号。 */
				receive_state_change(shell,                                 /* 记住此次编辑键后还应跳过一个尾部字节。 */
						SHELL_RECEIVE_TILDE_EXP);
				__fallthrough;                                              /* 共用下面 VT100 End 键的动作。 */
			case 'F':                                                           /* VT100 风格的 End 键可立即执行。 */
				z_shell_op_cursor_end_move(shell);                          /* 把光标移到输入末尾。 */
				break;

			case '1':                                                           /* ESC [ 1 ~ 的 Home 键带尾部字节。 */
				receive_state_change(shell,                                 /* 记录尚待消费的尾部字符。 */
						SHELL_RECEIVE_TILDE_EXP);
				__fallthrough;                                              /* 共用下一分支的 Home 动作。 */
			case 'H':                                                           /* VT100 风格的 Home 键。 */
				z_shell_op_cursor_home_move(shell);                         /* 把光标移到输入开头。 */
				break;

			case '2':                                                           /* ESC [ 2 ~ 切换插入模式。 */
				receive_state_change(shell,                                 /* 记住还需读取序列结尾。 */
						SHELL_RECEIVE_TILDE_EXP);
				__fallthrough;                                              /* 与下面的 Insert 编码共用一次状态切换。 */
			case 'L': {                                                         /* VT100 风格的 Insert 键。 */
				bool status = z_flag_insert_mode_get(shell);                /* 读取当前编辑模式。 */
				z_flag_insert_mode_set(shell, !status);                     /* 写入反值，切换插入/覆盖行为。 */
				break;
			}

			case '3':                                                           /* ESC [ 3 ~ 表示一次删除。 */
				receive_state_change(shell,                                 /* 等待并消费尾部终止字符。 */
						SHELL_RECEIVE_TILDE_EXP);
				if (z_flag_echo_get(shell)) {                               /* 回显开启时应用删除操作。 */
					z_shell_op_char_delete(shell);                      /* 删除光标处字符。 */
				}
				break;

			default:                                                            /* 未识别的 CSI 字节直接结束本序列。 */
				break;
			}
			break;

		case SHELL_RECEIVE_TILDE_EXP:                                               /* 先前功能键的动作已执行，此状态只消费其尾部。 */
			receive_state_change(shell, SHELL_RECEIVE_DEFAULT);                 /* 余下字节从普通状态开始解释。 */
			break;

		default:                                                                    /* 未知接收状态回退，避免以后字节持续被误解为转义。 */
			receive_state_change(shell, SHELL_RECEIVE_DEFAULT);                 /* 恢复默认解析状态。 */
			break;
		}
	}

	z_transport_buffer_flush(shell);                                                    /* 循环之后保留的传输缓冲刷新；当前实现读取结束时直接从循环内返回。 */
}

/**
  * @brief  将后端通知转换为内核轮询信号，使等待方知道接收可读或发送可继续。
  * @param  evt_type：[输入] 由传输后端上报的接收或发送事件类型。
  * @param  ctx：[输入] 初始化传输层时传入的有效 Shell 实例指针。
  * @retval 无。
  * @note   回调仅置相应信号，串口接收字节仍由后端缓冲保存；这里不查表，也不执行命令。
  */
static void transport_evt_handler(enum shell_transport_evt evt_type, void *ctx)
{
	struct shell *shell = (struct shell *)ctx;    /* 后端注册时保存的是 Shell 实例，恢复它以选择该实例的信号。 */
	struct k_poll_signal *signal;                 /* 指向此次传输事件对应的内核信号对象。 */

	/* 把 RX_RDY 映射到 RXRDY 信号，其余事件走 TXDONE；通知本身不携带 FIFO 字节。 */
	signal = (evt_type == SHELL_TRANSPORT_EVT_RX_RDY) ?
			&shell->ctx->signals[SHELL_SIGNAL_RXRDY] :
			&shell->ctx->signals[SHELL_SIGNAL_TXDONE];
	k_poll_signal_raise(signal, 0);               /* 使等待者有资格继续运行；实际处理要等对应线程被调度。 */
}

/**
  * @brief  在线程中处理待发日志，兼顾用户正在编辑的命令行和新输入。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
static void shell_log_process(const struct shell *shell)
{
	bool processed = false;                                     /* 保存后端是否仍在处理日志的结果。 */
	int signaled = 0;                                           /* 保存输入就绪信号的置位状态，避免连续日志饿死串口输入。 */
	int result;                                                 /* 接收 k_poll_signal_check 返回的信号结果值。 */

	/* 在日志仍有进展且无接收待办时连续处理，同时给交互输入留出响应机会。 */
	do {
		if (!IS_ENABLED(CONFIG_LOG_IMMEDIATE)) {            /* 非立即日志模式下，这里负责从后端取待显示的消息。 */
			z_shell_cmd_line_erase(shell);              /* 擦除正在编辑的行，为日志腾出位置。 */

			processed = z_shell_log_backend_process(    /* 交由日志后端处理下一条等待消息。 */
					shell->log_backend);
		}

		struct k_poll_signal *signal =                      /* 当前实例的接收就绪信号，日志输出后会检查它。 */
			&shell->ctx->signals[SHELL_SIGNAL_RXRDY];

		z_shell_print_prompt_and_cmd(shell);                /* 将用户的提示符和编辑内容重画回来。 */

		if (shell->ctx->cmd_buff_len) {                     /* 用户正在编辑时给终端留出可读的显示时间。 */
			k_sleep(K_MSEC(15));                        /* 在当前线程休眠 15 毫秒。 */
		}

		k_poll_signal_check(signal, &signaled, &result);    /* 检查是否有输入应交回 Shell 主循环处理。 */

	} while (processed && !signaled);                           /* 接收就绪时优先退出，剩余日志可在后续处理。 */
}

/**
  * @brief  初始化实例的上下文、同步对象及传输后端。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  p_config：[输入] 原样传给指定传输后端的配置指针，内容由后端约定。
  * @param  use_colors：[输入] 调用方是否请求 VT100 颜色。
  * @retval 返回后端 init 的结果，0 时实例进入 INITIALIZED。
  */
static int instance_init(const struct shell *shell, const void *p_config,
			 bool use_colors)
{
	/* 先验证允许的换行标志，再重置运行时上下文。 */
	__ASSERT_NO_MSG((shell->shell_flag == SHELL_FLAG_CRLF_DEFAULT) ||
			(shell->shell_flag == SHELL_FLAG_OLF_CRLF));

	memset(shell->ctx, 0, sizeof(*shell->ctx));                                           /* 清零该 Shell 实例独有的运行状态。 */
	shell->ctx->prompt = shell->default_prompt;                                           /* 初始提示符借用静态描述中的默认字符串。 */
	if (CONFIG_SHELL_CMD_ROOT[0]) {                                                       /* 配置根名称非空时，此实例从预选子树开始匹配。 */
		shell->ctx->selected_cmd = root_cmd_find(CONFIG_SHELL_CMD_ROOT);              /* 用根命令名查找并保存注册表中的条目。 */
	}

	history_init(shell);                                                                  /* 初始化可选的历史命令存储。 */

	k_mutex_init(&shell->ctx->wr_mtx);                                                    /* 创建序列化本终端编辑与输出的互斥锁。 */

	/* 为每一个事件号配对初始化 k_poll_signal 和 k_poll_event。 */
	for (int i = 0; i < SHELL_SIGNALS; i++) {
		k_poll_signal_init(&shell->ctx->signals[i]);                                  /* 让本实例各个信号从未置位状态开始。 */
		k_poll_event_init(&shell->ctx->events[i],                                     /* 设置为只通知的信号事件，信号的复位由处理器完成。 */
				  K_POLL_TYPE_SIGNAL,
				  K_POLL_MODE_NOTIFY_ONLY,
				  &shell->ctx->signals[i]);
	}

	if (IS_ENABLED(CONFIG_SHELL_STATS)) {                                                 /* 开启统计时重新计数丢弃的日志。 */
		shell->stats->log_lost_cnt = 0;                                               /* 启动时没有已计入的丢弃消息。 */
	}

	/* 按编译配置建立终端初始发送、回显、掩码和几何状态。 */
	z_flag_tx_rdy_set(shell, true);
	z_flag_echo_set(shell, IS_ENABLED(CONFIG_SHELL_ECHO_STATUS));                         /* 从配置加载是否回显输入。 */
	z_flag_obscure_set(shell, IS_ENABLED(CONFIG_SHELL_START_OBSCURED));                   /* 根据配置决定是否掩盖用户键入的字符。 */
	z_flag_mode_delete_set(shell,                                                         /* 配置 DEL 对应退格还是前向删除。 */
			     IS_ENABLED(CONFIG_SHELL_BACKSPACE_MODE_DELETE));
	shell->ctx->vt100_ctx.cons.terminal_wid =                                             /* 记录默认终端列数，供换行和补全排列使用。 */
					CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH;
	shell->ctx->vt100_ctx.cons.terminal_hei =                                             /* 记录默认终端行数。 */
					CONFIG_SHELL_DEFAULT_TERMINAL_HEIGHT;
	shell->ctx->vt100_ctx.cons.name_len = z_shell_strlen(shell->ctx->prompt);             /* 提示符长度参与行编辑的光标位置计算。 */
	z_flag_use_colors_set(shell, IS_ENABLED(CONFIG_SHELL_VT100_COLORS) && use_colors);    /* 调用方与编译选项均允许时使用颜色。 */

	/* 向选定传输后端注册 transport_evt_handler，并把该实例传作事件上下文。 */
	int ret = shell->iface->api->init(shell->iface, p_config,
					  transport_evt_handler,
					  (void *)shell);
	if (ret == 0) {                                                                       /* 后端初始化成功才进入可启动的状态。 */
		state_set(shell, SHELL_STATE_INITIALIZED);                                    /* 此时硬件接口已初始化，尚未接收交互命令。 */
	}

	return ret;                                                                           /* 原样返回后端初始化结果。 */
}

/**
  * @brief  卸载实例的后端，清理历史，恢复未初始化状态。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 0 表示完成，正在处理输入返回 -EBUSY，后端关闭失败返回后端错误码。
  */
static int instance_uninit(const struct shell *shell)
{
	__ASSERT_NO_MSG(shell);                                     /* 调用方必须传入有效 Shell 实例。 */
	__ASSERT_NO_MSG(shell->ctx && shell->iface);                /* 实例必须有运行状态和传输接口。 */

	int err;                                                    /* 保存传输接口的卸载结果。 */

	/* 正在收集或执行输入时不能卸载相关状态。 */
	if (z_flag_processing_get(shell)) {
		return -EBUSY;                                      /* 通知调用方实例仍忙。 */
	}

	if (IS_ENABLED(CONFIG_SHELL_LOG_BACKEND)) {                 /* 日志后端启用时先停止向该 Shell 提交输出；本处没有显式清空等待队列。 */
		z_shell_log_backend_disable(shell->log_backend);    /* 停用当前实例的日志输出。 */
	}

	err = shell->iface->api->uninit(shell->iface);              /* 调用所选后端的反初始化操作。 */
	if (err != 0) {                                             /* 后端拒绝卸载时保留其错误结果。 */
		return err;                                         /* 将后端错误原样报告给上层。 */
	}

	history_purge(shell);                                       /* 清除已存储的命令历史。 */
	state_set(shell, SHELL_STATE_UNINITIALIZED);                /* 成功卸载后，禁止按活动实例处理输入。 */

	return 0;                                                   /* 完成全部卸载步骤。 */
}

/* Shell 线程内的信号处理回调，每次处理一个实例的已置位信号，无返回值。 */
typedef void (*shell_signal_handler_t)(const struct shell *shell);

/**
  * @brief  检查一个信号，置位时先复位信号再调用对应处理函数。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  sig_idx：[输入] 待检查的实例信号下标。
  * @param  handler：[输入] 信号置位后在当前线程调用的处理函数。
  * @retval 无。
  */
static void shell_signal_handle(const struct shell *shell,
				enum shell_signal sig_idx,
				shell_signal_handler_t handler)
{
	struct k_poll_signal *signal = &shell->ctx->signals[sig_idx];    /* 选择本轮检查的实例信号。 */
	int set;                                                         /* 接收信号是否置位的状态。 */
	int res;                                                         /* 接收事件发送方附带的结果值；此处不使用其内容。 */

	k_poll_signal_check(signal, &set, &res);                         /* 检查通知状态；不会在这里等待数据。 */

	if (set) {                                                       /* 本轮只消费已置位的信号。 */
		k_poll_signal_reset(signal);                             /* 先复位，以便处理期间到来的新事件可以再次置位。 */
		handler(shell);                                          /* 接收事件使用 shell_process，在当前线程处理后端缓冲中的数据。 */
	}
}

/**
  * @brief  响应线程退出信号，报告卸载结果并终止当前 Shell 线程。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
static void kill_handler(const struct shell *shell)
{
	int err = instance_uninit(shell);             /* 尝试反初始化实例，并保留完成状态传给回调。 */

	if (shell->ctx->uninit_cb) {                  /* 调用方提供了结束回调时报告卸载结果。 */
		shell->ctx->uninit_cb(shell, err);    /* 回调在终止本线程之前运行。 */
	}

	shell->ctx->tid = NULL;                       /* 释放实例上的线程占用标记。 */
	k_thread_abort(k_current_get());              /* 终止正在运行的 Shell 线程。 */
}

/**
  * @brief  启动传输接口并等待内核通知，根据已就绪的信号分派输入、日志或退出。
  * @param  shell_handle：[输入] 创建线程时传递的实例指针。
  * @param  arg_log_backend：[输入] 由初始化入口编码为线程实参的日志后端开关。
  * @param  arg_log_level：[输入] 用指针宽度传递的初始日志级别。
  * @retval 无。
  * @note   k_poll 阻塞至有信号。UART 通知只表示可以读取；实际查表和处理发生在本线程随后调用的 shell_process 中。
  */
void shell_thread(void *shell_handle, void *arg_log_backend,
		  void *arg_log_level)
{
	struct shell *shell = shell_handle;                                       /* 取得启动时传入的实际实例。 */
	bool log_backend = (bool)arg_log_backend;                                 /* 还原是否请求把日志输出接入该 Shell。 */
	uint32_t log_level = POINTER_TO_UINT(arg_log_level);                      /* 从线程参数还原日志过滤等级。 */
	int err;                                                                  /* 保存后端激活、Shell 启动或等待事件的状态码。 */

	/* 从线程上下文启用非阻塞传输，再按配置接入日志后端。 */
	err = shell->iface->api->enable(shell->iface, false);
	if (err != 0) {                                                           /* 传输不能启用时无法正常服务该终端。 */
		return;                                                           /* 结束当前线程入口。 */
	}

	if (IS_ENABLED(CONFIG_SHELL_LOG_BACKEND) && log_backend                   /* 同时满足日志、调用方请求与启动可见状态时才接入日志。 */
	    && !IS_ENABLED(CONFIG_SHELL_START_OBSCURED)) {
		z_shell_log_backend_enable(shell->log_backend, (void *)shell,     /* 把实例作为日志输出上下文传给后端。 */
					   log_level);
	}

	/* 使实例进入活动状态并显示提示符，然后进入事件循环。 */
	err = shell_start(shell);
	if (err != 0) {                                                           /* 启动失败时不再轮询未就绪的控制台。 */
		return;
	}

	/* 持续等候终止、接收和可选日志信号。发送完成由写路径处理。 */
	while (true) {
		err = k_poll(shell->ctx->events, SHELL_SIGNAL_TXDONE,             /* K_FOREVER 使空闲线程阻塞，事件计数取 TXDONE 的序号，因此不含 TXDONE 本身。 */
			     K_FOREVER);

		if (err != 0) {                                                   /* 轮询异常时在写锁保护下向终端报告。 */
			k_mutex_lock(&shell->ctx->wr_mtx, K_FOREVER);             /* 取得终端写锁以串行输出错误。 */
			z_shell_fprintf(shell, SHELL_ERROR,                       /* 显示内核轮询返回的错误号。 */
					"Shell thread error: %d", err);
			k_mutex_unlock(&shell->ctx->wr_mtx);                      /* 离开之前释放写锁。 */
			return;                                                   /* 停止当前 Shell 线程。 */
		}

		/* 取得编辑与输出锁后让后端更新，再按顺序处理各个已置位的信号。 */
		k_mutex_lock(&shell->ctx->wr_mtx, K_FOREVER);

		if (shell->iface->api->update) {                                  /* 传输实现可自行提供额外的更新钩子。 */
			shell->iface->api->update(shell->iface);                  /* 调用当前实例所选后端的更新入口。 */
		}

		shell_signal_handle(shell, SHELL_SIGNAL_KILL, kill_handler);      /* 最先处理关闭请求，可能在此结束线程。 */
		shell_signal_handle(shell, SHELL_SIGNAL_RXRDY, shell_process);    /* RXRDY 已置位时在线程内调用 shell_process；它最终从后端取字节。 */
		if (IS_ENABLED(CONFIG_SHELL_LOG_BACKEND)) {                       /* 只有构建启用 Shell 日志后端时才消费日志信号。 */
			shell_signal_handle(shell, SHELL_SIGNAL_LOG_MSG,          /* 有日志事件时执行上方的日志处理器。 */
					    shell_log_process);
		}

		k_mutex_unlock(&shell->ctx->wr_mtx);                              /* 本轮事件处理结束，让其他输出方可以写终端。 */
	}
}

/**
  * @brief  准备静态分配的 Shell 实例和任务栈，然后创建可立即调度的线程。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  transport_config：[输入] 交给选定后端的配置对象，按后端约定使用。
  * @param  use_colors：[输入] 是否请求彩色终端显示。
  * @param  log_backend：[输入] 是否请求启用 Shell 日志后端。
  * @param  init_log_level：[输入] 日志后端启动时的过滤级别。
  * @retval 0 表示创建完成，已有线程返回 -EALREADY，传输初始化失败则传回其错误码。
  */
int shell_init(const struct shell *shell, const void *transport_config,
	       bool use_colors, bool log_backend, uint32_t init_log_level)
{
	__ASSERT_NO_MSG(shell);                                                  /* 必须传入有效实例。 */
	__ASSERT_NO_MSG(shell->ctx && shell->iface && shell->default_prompt);    /* 检查运行时状态、传输接口和默认提示符均已配置。 */

	if (shell->ctx->tid) {                                                   /* 已存在任务句柄时，不能为同一实例重复创建线程。 */
		return -EALREADY;                                                /* 报告此前已初始化。 */
	}

	int err = instance_init(shell, transport_config, use_colors);            /* 建立实例状态、信号对象与后端事件回调。 */

	if (err != 0) {                                                          /* 实例初始化失败时禁止用半初始化状态开线程。 */
		return err;                                                      /* 将初始化错误原样交回。 */
	}

	/* 用实例自带的线程对象与栈启动 shell_thread，传递日志选项，K_NO_WAIT 表示可立即调度。 */
	k_tid_t tid = k_thread_create(shell->thread,
				  shell->stack, CONFIG_SHELL_STACK_SIZE,
				  shell_thread, (void *)shell, (void *)log_backend,
				  UINT_TO_POINTER(init_log_level),
				  SHELL_THREAD_PRIORITY, 0, K_NO_WAIT);

	shell->ctx->tid = tid;                                                   /* 保存线程标识供防重入、反初始化和上下文检查使用。 */
	k_thread_name_set(tid, shell->thread_name);                              /* 赋予可辨认的调试线程名。 */

	return 0;                                                                /* 实例初始化与线程创建完成。 */
}

/**
  * @brief  请求关闭 Shell 并通过回调报告结果。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  cb：[输入] 完成回调；多线程路径允许 NULL，单线程路径对此有断言。
  * @retval 无。
  * @note   多线程模式提交 KILL 信号后即返回；关闭和回调由 Shell 线程处理。
  */
void shell_uninit(const struct shell *shell, shell_uninit_cb_t cb)
{
	__ASSERT_NO_MSG(shell);                          /* 调用方必须提供要卸载的实例。 */

	/* 多线程模式把关闭操作转交 Shell 线程执行。 */
	if (IS_ENABLED(CONFIG_MULTITHREADING)) {
		struct k_poll_signal *signal =           /* 取本实例专用 KILL 信号。 */
				&shell->ctx->signals[SHELL_SIGNAL_KILL];

		shell->ctx->uninit_cb = cb;              /* 记录实例关闭后需要调用的回调。 */
		(void)k_poll_signal_raise(signal, 0);    /* 通知线程执行 kill_handler。 */

		return;                                  /* 请求已经排队，调用者无需在这里等线程退出。 */
	}

	/* 单线程模式就地卸载并同步通知结果。 */
	int err = instance_uninit(shell);

	if (cb) {                                        /* 立即报告单线程反初始化结果。 */
		cb(shell, err);                          /* 执行由调用方提供的完成回调。 */
	} else {
		__ASSERT_NO_MSG(0);                      /* 单线程路径要求有回调，否则触发断言。 */
	}
}

/**
  * @brief  把已初始化实例切入交互态并显示首次提示符。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 成功为 0；状态不是 INITIALIZED 时为 -ENOTSUP。
  */
int shell_start(const struct shell *shell)
{
	__ASSERT_NO_MSG(shell);                                                  /* 检查调用者提供的实例。 */
	__ASSERT_NO_MSG(shell->ctx && shell->iface && shell->default_prompt);    /* 确认启用交互所需的运行状态、后端与提示符均可用。 */

	if (state_get(shell) != SHELL_STATE_INITIALIZED) {                       /* 只允许从已初始化状态启动交互。 */
		return -ENOTSUP;                                                 /* 避免在未初始化或已活动时重复启动。 */
	}

	/* 串行设置输出颜色、打印空行和首个提示符。 */
	k_mutex_lock(&shell->ctx->wr_mtx, K_FOREVER);

	if (IS_ENABLED(CONFIG_SHELL_VT100_COLORS)) {                             /* 编入颜色功能时才设置终端默认颜色。 */
		z_shell_vt100_color_set(shell, SHELL_NORMAL);                    /* 将 VT100 输出恢复到普通文字颜色。 */
	}

	z_shell_raw_fprintf(shell->fprintf_ctx, "\n\n");                         /* 输出起始换行，分隔此前的启动信息。 */
	state_set(shell, SHELL_STATE_ACTIVE);                                    /* 进入活动态，初始化编辑行并显示提示符。 */

	k_mutex_unlock(&shell->ctx->wr_mtx);                                     /* UI 更新完毕后释放写锁。 */

	return 0;                                                                /* 当前实例可以收命令。 */
}

/**
  * @brief  暂停输入处理，将正在交互的实例退回已初始化状态。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 成功为 0；尚未启动或未初始化时返回 -ENOTSUP。
  */
int shell_stop(const struct shell *shell)
{
	__ASSERT_NO_MSG(shell);                       /* 检查实例指针。 */
	__ASSERT_NO_MSG(shell->ctx);                  /* 需要读取并更新实例的运行状态。 */

	enum shell_state state = state_get(shell);    /* 保存当前生命周期状态用于判断是否已停用。 */

	if ((state == SHELL_STATE_INITIALIZED) ||     /* 对从未初始化或已停止的实例，拒绝重复停止。 */
	    (state == SHELL_STATE_UNINITIALIZED)) {
		return -ENOTSUP;                      /* 告知当前状态不支持这次操作。 */
	}

	state_set(shell, SHELL_STATE_INITIALIZED);    /* 保留初始化资源，暂停正常交互。 */

	return 0;                                     /* 本次停止成功。 */
}

/**
  * @brief  标记本实例正在处理输入，按运行状态决定是否读取终端字节。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
void shell_process(const struct shell *shell)
{
	__ASSERT_NO_MSG(shell);                 /* 调用方应传入接收事件所属的实例。 */
	__ASSERT_NO_MSG(shell->ctx);            /* 实例运行上下文必须有效。 */

	/* 置处理标记，使卸载路径能检测到本实例正在消费输入。 */
	z_flag_processing_set(shell, true);

	switch (shell->ctx->state) {            /* 按当前状态决定是否可以读终端。 */
	case SHELL_STATE_UNINITIALIZED:         /* 尚未初始化的实例不读取后端。 */
	case SHELL_STATE_INITIALIZED:           /* 已初始化但未启动的实例也暂不消费。 */
		break;

	case SHELL_STATE_ACTIVE:                /* 活动态允许解析输入。 */
		state_collect(shell);           /* 从晚到或合并的接收事件中读取实际缓冲字节。 */
		break;
	default:                                /* 未知状态不触发任何输入操作。 */
		break;
	}

	z_flag_processing_set(shell, false);    /* 复位原子处理标志，允许后续卸载检查通过。 */
}

/**
  * @brief  在实例上以指定颜色输出格式化文本，并按需重画编辑中的命令行。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  color：[输入] 本次输出颜色。
  * @param  fmt：[输入] 有效的格式字符串。
  * @param  args：[输入] 与 fmt 对应的可变参数列表。
  * @retval 无。
  * @note   仅可在线程上下文使用。Shell 线程内部仅允许从命令回调调用；非活动实例记录待提示标记后返回。
  */
void shell_vfprintf(const struct shell *shell, enum shell_vt100_color color,
		   const char *fmt, va_list args)
{
	__ASSERT_NO_MSG(shell);                                         /* 验证输出目标。 */
	__ASSERT(!k_is_in_isr(), "Thread context required.");           /* 输出可能等待互斥锁，要求在线程上下文执行。 */
	__ASSERT_NO_MSG(shell->ctx);                                    /* 需要读写该实例的运行状态。 */
	__ASSERT_NO_MSG((shell->ctx->internal.flags.cmd_ctx == 1) ||    /* Shell 线程自身仅允许在命令处理器中使用此公共输出函数。 */
			(k_current_get() != shell->ctx->tid));
	__ASSERT_NO_MSG(shell->fprintf_ctx);                            /* 格式化输出对象必须已配置。 */
	__ASSERT_NO_MSG(fmt);                                           /* 调用方须提供有效的格式字符串。 */

	/* 实例尚未激活时只记下提示请求，避免等待无活动后端。 */
	if (state_get(shell) != SHELL_STATE_ACTIVE) {
		z_flag_print_noinit_set(shell, true);                   /* 下次激活时补一条警告。 */
		return;                                                 /* 提前结束本次输出。 */
	}

	/* 持有写锁时输出格式化消息，需要时临时移走编辑行并恢复。 */
	k_mutex_lock(&shell->ctx->wr_mtx, K_FOREVER);
	if (!z_flag_cmd_ctx_get(shell) && !shell->ctx->bypass) {        /* 非命令回调且未处于旁路时需保护屏幕上的编辑内容。 */
		z_shell_cmd_line_erase(shell);                          /* 擦掉现有提示符与命令行，避免和消息混叠。 */
	}
	z_shell_vfprintf(shell, color, fmt, args);                      /* 格式化并按指定颜色发送文本。 */
	if (!z_flag_cmd_ctx_get(shell) && !shell->ctx->bypass) {        /* 普通异步输出完成后需恢复编辑行。 */
		z_shell_print_prompt_and_cmd(shell);                    /* 重新画出提示符和仍未提交的文本。 */
	}
	z_transport_buffer_flush(shell);                                /* 让格式化缓冲中的剩余内容交给传输接口。 */
	k_mutex_unlock(&shell->ctx->wr_mtx);                            /* 允许其他线程继续使用终端。 */
}

/**
  * @brief  以可变参数形式调用带互斥保护的 Shell 格式化输出。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  color：[输入] 本次输出颜色。
  * @param  fmt：[输入] 有效的格式字符串。
  * @param  ...：[输入] 与格式说明符对应的附加实参。
  * @retval 无。
  * @note   沿用 shell_vfprintf 的线程及命令上下文限制。
  */
void shell_fprintf(const struct shell *shell, enum shell_vt100_color color,
		   const char *fmt, ...)
{
	va_list args;                               /* 保存本次调用的变长参数游标。 */

	va_start(args, fmt);                        /* 从最后一个具名形参 fmt 后初始化变长参数遍历。 */
	shell_vfprintf(shell, color, fmt, args);    /* 转交给实际带锁输出路径。 */
	va_end(args);                               /* 结束本次可变参数遍历。 */
}

/**
  * @brief  打印一行数据的偏移、十六进制和可显示字符。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  offset：[输入] 行首相对整个缓冲区的字节偏移。
  * @param  data：[输入] 本行的可读数据首地址。
  * @param  len：[输入] 本行有效字节数，调用者每行最多分配 SHELL_HEXDUMP_BYTES_IN_LINE 字节。
  * @retval 无。
  */
void shell_hexdump_line(const struct shell *shell, unsigned int offset,
			const uint8_t *data, size_t len)
{
	int i;                                                         /* 用列下标同时组织十六进制区和字符区。 */

	shell_fprintf(shell, SHELL_NORMAL, "%08X: ", offset);          /* 以八位十六进制显示该行的起始字节偏移。 */

	/* 先排版十六进制数值区，短行补空以保持右侧字符区对齐。 */
	for (i = 0; i < SHELL_HEXDUMP_BYTES_IN_LINE; i++) {
		if (i > 0 && !(i % 8)) {                               /* 每八字节增加一处分组空白。 */
			shell_fprintf(shell, SHELL_NORMAL, " ");       /* 打印不同字节组之间的空格。 */
		}

		if (i < len) {                                         /* 对实际存在的数据打印数值。 */
			shell_fprintf(shell, SHELL_NORMAL, "%02x ",    /* 单字节占两位十六进制，加一格分隔。 */
				      data[i] & 0xFF);
		} else {
			shell_fprintf(shell, SHELL_NORMAL, "   ");     /* 空字节位按同样的列宽补齐。 */
		}
	}

	shell_fprintf(shell, SHELL_NORMAL, "|");                       /* 开始右侧字符视图。 */

	/* 再按原数据顺序打印可显示的字符，不可打印的字节用点代替。 */
	for (i = 0; i < SHELL_HEXDUMP_BYTES_IN_LINE; i++) {
		if (i > 0 && !(i % 8)) {                               /* 与左侧保持相同的八字节分组。 */
			shell_fprintf(shell, SHELL_NORMAL, " ");       /* 写出组间分隔。 */
		}

		if (i < len) {                                         /* 只有有效数据区才能读取。 */
			char c = data[i];                              /* 取该位置的字节用来判断可打印性。 */

			shell_fprintf(shell, SHELL_NORMAL, "%c",       /* 直接显示可打印字符，其余以点占位。 */
				      isprint((int)c) ? c : '.');
		} else {
			shell_fprintf(shell, SHELL_NORMAL, " ");       /* 对短行末尾补一列空格。 */
		}
	}

	shell_print(shell, "|");                                       /* 封闭字符视图并结束该输出行。 */
}

/**
  * @brief  将一段连续内存分行显示为带偏移的十六进制和字符视图。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  data：[输入] 至少 len 字节的可读区域。
  * @param  len：[输入] 需要打印的总字节数。
  * @retval 无。
  */
void shell_hexdump(const struct shell *shell, const uint8_t *data, size_t len)
{
	const uint8_t *p = data;                                     /* 起点指向待转储区域的第一个字节。 */
	size_t line_len;                                             /* 当前转储行实际会使用的字节数。 */

	/* 每次处理不超过固定行宽的字节，最后一行按剩余长度输出。 */
	while (len) {
		line_len = MIN(len, SHELL_HEXDUMP_BYTES_IN_LINE);    /* 求本轮可安全读取的长度。 */

		shell_hexdump_line(shell, p - data, p, line_len);    /* 相对整个缓冲的偏移与该行数据一起传入排版器。 */

		len -= line_len;                                     /* 从剩余字节数扣掉这次已经输出的数据。 */
		p += line_len;                                       /* 把指针推进到下一行的起点。 */
	}
}

/**
  * @brief  借用调用者提供的字符串作为提示符，并更新终端记录的显示长度。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  prompt：[输入] 持续有效的零结尾字符串；实例保存其地址。
  * @retval 成功为 0，提示符指针为空返回 -EINVAL。
  */
int shell_prompt_change(const struct shell *shell, const char *prompt)
{
	__ASSERT_NO_MSG(shell);                                          /* 目标实例必须有效。 */

	if (prompt == NULL) {                                            /* 提示符不能为空指针。 */
		return -EINVAL;                                          /* 拒绝后保持原提示符。 */
	}
	shell->ctx->prompt = prompt;                                     /* 借用新字符串的地址，并不复制它的内容。 */
	shell->ctx->vt100_ctx.cons.name_len = z_shell_strlen(prompt);    /* 更新光标计算依赖的提示符长度。 */

	return 0;                                                        /* 提示符设置完成。 */
}

/**
  * @brief  获得写互斥锁后打印当前活动命令的帮助。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @retval 无。
  */
void shell_help(const struct shell *shell)
{
	k_mutex_lock(&shell->ctx->wr_mtx, K_FOREVER);    /* 取得输出锁，避免使用法说明与其他终端输出交错。 */
	shell_internal_help_print(shell);                /* 打印本实例当前活动命令的帮助信息。 */
	k_mutex_unlock(&shell->ctx->wr_mtx);             /* 帮助文本输出后释放写锁。 */
}

/**
  * @brief  在调用线程中同步解析和执行一条程序传入的命令行。
  * @param  shell：[输入] 指定目标实例，传 NULL 时需要启用虚拟后端。
  * @param  cmd：[输入] 零结尾的命令字符串，长度须能连同结尾零放入输入缓冲区。
  * @retval 返回 execute 的结果；空命令指针为 -ENOEXEC，过长为 -ENOMEM，无实例且缺少虚拟后端为 -EINVAL。
  * @note   禁止从 Shell 命令处理函数中递归调用。本实现复制输入后才获取执行锁。
  */
int shell_execute_cmd(const struct shell *shell, const char *cmd)
{
	uint16_t cmd_len = z_shell_strlen(cmd);              /* 取得待执行字符串的字节数。 */
	int ret_val;                                         /* 保存分派或业务处理结果。 */

	/* 检查命令是否存在，以及整行能否放进当前配置的编辑缓冲区。 */
	if (cmd == NULL) {
		return -ENOEXEC;                             /* 输入指针为空，无法执行。 */
	}

	if (cmd_len > (CONFIG_SHELL_CMD_BUFF_SIZE - 1)) {    /* 还要为最后的字符串结束符预留一个字节。 */
		return -ENOMEM;                              /* 返回缓冲区不足错误。 */
	}

	if (shell == NULL) {                                 /* 没有显式实例时按配置尝试虚拟后端。 */
#if defined(CONFIG_SHELL_BACKEND_DUMMY)
		shell = shell_backend_dummy_get_ptr();       /* 在具备 dummy 后端的构建中选择其全局实例。 */
#else
		return -EINVAL;                              /* 没有虚拟后端且调用者未指定目标。 */
#endif
	}

	__ASSERT(shell->ctx->internal.flags.cmd_ctx == 0,    /* 禁止从命令处理上下文再次进入命令分派。 */
						"Function cannot be called"
						" from command context");

	/* 将调用方的命令复制到实例缓冲，并把长度与光标同步至末尾。 */
	strcpy(shell->ctx->cmd_buff, cmd);
	shell->ctx->cmd_buff_len = cmd_len;                  /* 保存要参与分词的实际命令长度。 */
	shell->ctx->cmd_buff_pos = cmd_len;                  /* 把本地编辑光标置于命令末尾。 */

	/* 使用与线程输入分派相同的写锁调用 execute。 */
	k_mutex_lock(&shell->ctx->wr_mtx, K_FOREVER);
	ret_val = execute(shell);                            /* 从这条预置输入同步取出并执行处理函数。 */
	k_mutex_unlock(&shell->ctx->wr_mtx);                 /* 命令完成后归还终端写锁。 */

	cmd_buffer_clear(shell);                             /* 清空本次程序调用的编辑状态。 */

	return ret_val;                                      /* 交还此次分派的结果。 */
}

/**
  * @brief  改变实例编辑时的插入模式标志。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  val：[输入] 希望写入的新标志值。
  * @retval 返回此前的标志值（0 或 1）；实例为空时返回 -EINVAL。
  */
int shell_insert_mode_set(const struct shell *shell, bool val)
{
	if (shell == NULL) {                               /* 有效实例才可切换插入模式。 */
		return -EINVAL;                            /* 空实例不允许修改编辑标志。 */
	}

	return (int)z_flag_insert_mode_set(shell, val);    /* 更新插入模式并保留此前布尔状态作为返回值。 */
}

/**
  * @brief  改变终端使用彩色输出的标志。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  val：[输入] 希望写入的新标志值。
  * @retval 返回此前的标志值（0 或 1）；实例为空时返回 -EINVAL。
  */
int shell_use_colors_set(const struct shell *shell, bool val)
{
	if (shell == NULL) {                              /* 先检查颜色配置的目标实例。 */
		return -EINVAL;                           /* 空指针无法指定输出终端。 */
	}

	return (int)z_flag_use_colors_set(shell, val);    /* 更新颜色开关并返回旧状态。 */
}

/**
  * @brief  改变输入回显标志。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  val：[输入] 希望写入的新标志值。
  * @retval 返回此前的标志值（0 或 1）；实例为空时返回 -EINVAL。
  */
int shell_echo_set(const struct shell *shell, bool val)
{
	if (shell == NULL) {                        /* 验证输入回显的目标实例。 */
		return -EINVAL;                     /* 空实例不能切换回显。 */
	}

	return (int)z_flag_echo_set(shell, val);    /* 改写回显状态并报告之前的状态。 */
}

/**
  * @brief  改变交互输入的掩码显示标志。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  val：[输入] 希望写入的新标志值。
  * @retval 返回此前的标志值（0 或 1）；实例为空时返回 -EINVAL。
  */
int shell_obscure_set(const struct shell *shell, bool val)
{
	if (shell == NULL) {                           /* 拒绝缺少目标的掩码设置。 */
		return -EINVAL;                        /* 没有实例可应用此状态。 */
	}

	return (int)z_flag_obscure_set(shell, val);    /* 更新输入遮蔽标志并返回原来的布尔值。 */
}

/**
  * @brief  设置 DEL 字节采用退格还是前向删除。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  val：[输入] true 使 DEL 删除光标左侧字符，false 删除当前位置字符。
  * @retval 返回此前的标志值（0 或 1）；实例为空时返回 -EINVAL。
  */
int shell_mode_delete_set(const struct shell *shell, bool val)
{
	if (shell == NULL) {                               /* 需要有效实例保存 DEL 键行为。 */
		return -EINVAL;                            /* 无目标实例时报告参数错误。 */
	}

	return (int)z_flag_mode_delete_set(shell, val);    /* 改写删除方向标志并返回旧设置。 */
}

/**
  * @brief  设置或撤销实例直接接收原始字节的旁路回调。
  * @param  sh：[输入] 需要切换模式的有效 Shell 实例。
  * @param  bypass：[输入] 收到原始数据时调用的回调，NULL 恢复普通接收路径。
  * @retval 无。
  */
void shell_set_bypass(const struct shell *sh, shell_bypass_cb_t bypass)
{
	sh->ctx->bypass = bypass;    /* 后续接收将读取此指针，NULL 表示回到逐字节命令行解析。 */
}

/**
  * @brief  实现内置 help 命令，说明本构建支持的交互功能并输出根命令表。
  * @param  shell：[输入] 本次操作所属的有效 Shell 实例。
  * @param  argc：[输入] 命令参数个数，本处理函数不使用。
  * @param  argv：[输入] 命令参数数组，本处理函数不使用。
  * @retval 0，帮助输出完成。
  */
static int cmd_help(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);                                                           /* 本帮助命令固定以命令名调用，无需读取实参数量。 */
	ARG_UNUSED(argv);                                                           /* 输出内容来自构建配置，忽略输入参数数组。 */

/* 根据编入的交互功能有选择地显示相应使用提示。 */
#if defined(CONFIG_SHELL_TAB)
	shell_print(shell, "Please press the <Tab> button to see all available "    /* 告知用户可按 Tab 列出可用命令。 */
			   "commands.");
#endif

#if defined(CONFIG_SHELL_TAB_AUTOCOMPLETION)
	shell_print(shell,                                                          /* 只有开通自动补全时才介绍候选提示和子命令补全操作。 */
		"You can also use the <Tab> button to prompt or auto-complete"
		" all commands or its subcommands.");
#endif

#if defined(CONFIG_SHELL_HELP)
	shell_print(shell,                                                          /* 说明可用 -h 和 --help 查询命令用法。 */
		"You can try to call commands with <-h> or <--help> parameter"
		" for more information.");
#endif

#if defined(CONFIG_SHELL_METAKEYS)
	shell_print(shell,                                                          /* 列出已启用的 Ctrl 和 Alt 快捷键。 */
		"\nShell supports following meta-keys:\n"
		"  Ctrl + (a key from: abcdefklnpuw)\n"
		"  Alt  + (a key from: bf)\n"
		"Please refer to shell documentation for more details.");
#endif

	/* 输出该实例可访问的根命令；没有完整帮助组件时也可以手动枚举。 */
	if (IS_ENABLED(CONFIG_SHELL_HELP)) {
		z_shell_help_subcmd_print(shell, NULL,                              /* 父节点传 NULL 表示列出全局根命令。 */
					 "\nAvailable commands:\n");
	} else {
		const struct shell_static_entry *entry;                             /* 暂存当前枚举到的一条根命令。 */
		size_t idx = 0;                                                     /* 从根表第一项开始。 */

		shell_print(shell, "\nAvailable commands:");                        /* 打印列表标题，随后逐项显示语法名。 */
		while ((entry = z_shell_cmd_get(NULL, idx++, NULL)) != NULL) {      /* 到达表尾时 z_shell_cmd_get 返回 NULL。 */
			shell_print(shell, "  %s", entry->syntax);                  /* 输出当前根命令名称。 */
		}
	}

	return 0;                                                                   /* 这次 help 命令正常完成。 */
}

/* 登记全局根命令 help，参数规则为包含命令名在内共一项，执行时转到 cmd_help。 */
SHELL_CMD_ARG_REGISTER(help, NULL, "Prints the help message.", cmd_help, 1, 0);
