/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Shell 内部工具。交互线程先在可写命令缓冲内建立 argv，再按输入词遍历根命令和子命令表；本文件还负责终端坐标、文本整理和设备补全。 */
#include <ctype.h>             /* 提供空白字符判定 isspace。 */
#include <device.h>            /* 提供静态设备列表与就绪状态查询。 */
#include "shell_utils.h"       /* 声明命令树、字符串及终端布局辅助接口。 */
#include "shell_wildcard.h"    /* 提供可选的通配符识别。 */

extern const struct shell_cmd_entry __shell_root_cmds_start[];    /* 链接器提供的根命令入口首地址。 */
extern const struct shell_cmd_entry __shell_root_cmds_end[];      /* 指向根命令范围的尾后位置。 */

/**
  * @brief  取得根命令链接段中第 id 个入口的地址。
  * @param  id：[输入]从零起算的根命令序号，调用方须先保证小于总数。
  * @retval 指向常驻根命令包装项的指针。
  * @note   根命令由注册宏生成并由链接脚本汇集，表边界由起止符号给出。
  */
static inline const struct shell_cmd_entry *shell_root_cmd_get(uint32_t id)
{
	return &__shell_root_cmds_start[id];    /* 返回包装项，具体描述由 u.entry 获取。 */
}

/**
  * @brief  计算命令缓冲位置在终端中相对首行的行号。
  * @param  cons：[输入]有效布局状态，含提示符长度和非零终端宽度。
  * @param  buffer_pos：[输入]从命令文本起点计的缓冲偏移。
  * @retval 从零起算的相对行号。
  * @note   除法前先加入提示符占用的宽度。
  */
static uint32_t line_num_with_buffer_offset_get(struct shell_multiline_cons *cons,
					     uint16_t buffer_pos)
{
	return ((buffer_pos + cons->name_len) / cons->terminal_wid);    /* 提示符和缓冲偏移相加后除以行宽，舍去当前行内余数。 */
}

/**
  * @brief  计算命令缓冲位置在当前终端行中的列号。
  * @param  cons：[输入]有效布局状态，终端宽度必须非零。
  * @param  buffer_pos：[输入]命令文本中的缓冲偏移。
  * @retval 从 1 起算的显示列号。
  * @note   加入提示符宽度后，以终端宽度取余处理换行。
  */
static uint32_t col_num_with_buffer_offset_get(struct shell_multiline_cons *cons,
					    uint16_t buffer_pos)
{
	return (1 + ((buffer_pos + cons->name_len) % cons->terminal_wid));    /* 余数是从零起算的行内偏移，加一转为终端列号。 */
}

/**
  * @brief  计算从第一处命令偏移移动到第二处时的屏幕列差。
  * @param  cons：[输入]布局状态，含非零终端宽度。
  * @param  offset1：[输入]起始缓冲偏移。
  * @param  offset2：[输入]目标缓冲偏移。
  * @retval 目标列减起始列；左移时可为负。
  * @note   跨行时比较两个屏幕列号。
  */
int32_t z_column_span_with_buffer_offsets_get(struct shell_multiline_cons *cons,
					      uint16_t offset1,
					      uint16_t offset2)
{
	return col_num_with_buffer_offset_get(cons, offset2)    /* 目标列与起始列之差，保留移动方向。 */
			- col_num_with_buffer_offset_get(cons, offset1);
}

/**
  * @brief  计算从第一处命令偏移移动到第二处时的屏幕行差。
  * @param  cons：[输入]布局状态，含非零终端宽度。
  * @param  offset1：[输入]起始缓冲偏移。
  * @param  offset2：[输入]目标缓冲偏移。
  * @retval 目标行减起始行；向上移动时可为负。
  * @note   按命令前的提示符长度修正两处位置。
  */
int32_t z_row_span_with_buffer_offsets_get(struct shell_multiline_cons *cons,
					   uint16_t offset1,
					   uint16_t offset2)
{
	return line_num_with_buffer_offset_get(cons, offset2)    /* 从目标行号减去起始行号。 */
		- line_num_with_buffer_offset_get(cons, offset1);
}

/**
  * @brief  更新当前光标和命令末尾在多行终端上的坐标。
  * @param  cons：[输入输出]非空布局结构，使用 name_len 与非零 terminal_wid 并写回四个坐标。
  * @param  buff_pos：[输入]当前光标在命令中的偏移。
  * @param  buff_len：[输入]命令的有效长度。
  * @retval 无。
  * @note   显示行和列均从 1 起算。
  */
void z_shell_multiline_data_calc(struct shell_multiline_cons *cons,
				 uint16_t buff_pos, uint16_t buff_len)
{
	/* 将命令内偏移换算为终端坐标。提示符已占用行首若干列，跨过终端宽度即折到下一行。 */
	cons->cur_x = (buff_pos + cons->name_len) % cons->terminal_wid + 1;        /* 当前行内的光标列。 */
	cons->cur_y = (buff_pos + cons->name_len) / cons->terminal_wid + 1;        /* 当前光标所在的显示行。 */

	/* 以完整命令长度计算行末，用于光标在文本中间时重绘余下内容。 */
	cons->cur_y_end = (buff_len + cons->name_len) / cons->terminal_wid + 1;    /* 记录命令末尾所在的显示行。 */
	cons->cur_x_end = (buff_len + cons->name_len) % cons->terminal_wid + 1;    /* 记录该行内的终止列。 */
}

/**
  * @brief  就地扫描单个参数，处理引号和转义，并更新扫描位置。
  * @param  ppcmd：[输入输出]非空指针，指向可写的零结尾命令文本，返回分隔空白或结尾位置。
  * @param  c：[输入]调用者传入的首字符。循环每次从当前位置重新取值。
  * @retval 零表示没有尚待闭合的引号，否则返回未闭合的单或双引号。
  * @note   解析时会移动后续字符并改写原缓冲。引号保护其中的空白；识别引号内的八进制和十六进制转义。
  */
static char make_argv(char **ppcmd, uint8_t c)
{
	char *cmd = *ppcmd;                                                        /* 当前命令的可写扫描位置。 */
	char quote = 0;                                                            /* 零表示引号外，否则保存开启区段的引号字符。 */

	/* 扫描到引号外的空白或文本结束为止。删除转义前缀或引号后，指针通常还在原位置，以便处理前移的新字符。 */
	while (1) {
		c = *cmd;                                                          /* 每次从可能已经前移的文本取新字符。 */

		if (c == '\0') {                                                   /* 到达字符串结束符时停止，即使尚有引号未闭合。 */
			break;                                                     /* 退出扫描，由返回值报告引用状态。 */
		}

		/* 在引号外，反斜杠保护紧随字符，单引号和双引号则开启相应的引用区。 */
		if (!quote) {
			switch (c) {                                               /* 区分引号外的转义起始符、引号和普通字符。 */
			case '\\':                                                 /* 引号外的反斜杠使下一字符按字面处理。 */
				memmove(cmd, cmd + 1,
						z_shell_strlen(cmd));              /* 左移余串，移除反斜杠并连同结尾零一起复制。 */
				cmd += 1;                                          /* 越过转义保护的字面字符。 */
				continue;                                          /* 从其后继续扫描。 */

			case '\'':                                                 /* 单引号与双引号进入相同的开启处理。 */
			case '\"':                                                 /* 记录实际出现的引号种类。 */
				memmove(cmd, cmd + 1,
						z_shell_strlen(cmd));              /* 把开引号从最终参数中删去。 */
				quote = c;                                         /* 记住需要由哪种引号闭合。 */
				continue;                                          /* 下一轮处理已经移到当前位置的字符。 */
			default:                                                   /* 普通字符交给后面的分隔符检查。 */
				break;                                             /* 退出字符分类。 */
			}
		}

		/* 匹配同种引号才结束引用，另一种引号作为普通字符保留。 */
		if (quote == c) {
			memmove(cmd, cmd + 1, z_shell_strlen(cmd));                /* 删除已匹配的闭引号。 */
			quote = 0;                                                 /* 恢复引号外状态。 */
			continue;                                                  /* 继续处理紧随闭引号的字符。 */
		}

		/* 解释引号内的转义，分别处理字面引号、八进制和十六进制字节。 */
		if (quote && c == '\\') {
			char t = *(cmd + 1);                                       /* 检查反斜杠后的转义类别。 */

			if (t == quote) {                                          /* 被转义的同种引号作为字面字符。 */
				memmove(cmd, cmd + 1,
						z_shell_strlen(cmd));              /* 只移除反斜杠，保留其后的引号字节。 */
				cmd += 1;                                          /* 越过该字面引号，避免将其作闭引号处理。 */
				continue;                                          /* 继续扫描余串。 */
			}

			if (t == '0') {                                            /* 零前缀触发八进制转义解码尝试。 */
				uint8_t i;                                         /* 指向当前候选数字的偏移，结束时也表示停止位置。 */
				uint8_t v = 0U;                                    /* 按字节宽度累积解码值。 */

				/* 从反斜杠和前缀 0 之后起，最多接收三个八进制数字。 */
				for (i = 2U; i < (2 + 3); i++) {
					t = *(cmd + i);                            /* 取得当前八进制候选字符。 */

					if (t >= '0' && t <= '7') {                /* 仅数字 0 至 7 可贡献一位。 */
						v = (v << 3) | (t - '0');          /* 每个八进制数字占三位；最终值保存在 uint8_t 中。 */
					} else {
						break;                             /* 首个无效字符留给后续扫描。 */
					}
				}

				/* 只有确实读到数字才缩短文本，写回一个解码字节，再处理后续输入。 */
				if (i > 2) {
					memmove(cmd, cmd + (i - 1),
						z_shell_strlen(cmd) - (i - 2));    /* 将剩余字节和终止符前移，为单个转义结果留位。 */
					*cmd++ = v;                                /* 写入结果并把扫描指针移到下一字节。 */
					continue;                                  /* 忽略已消耗的转义文本。 */
				}
			}

			if (t == 'x') {                                            /* x 前缀触发十六进制转义。 */
				uint8_t i;                                         /* 记录已检查的字符位置。 */
				uint8_t v = 0U;                                    /* 保存至多两个半字节的结果。 */

				/* 从反斜杠和 x 之后起，最多读取两个十六进制数字。 */
				for (i = 2U; i < (2 + 2); i++) {
					t = *(cmd + i);                            /* 取得当前十六进制候选字符。 */

					if (t >= '0' && t <= '9') {                /* 处理 0 至 9。 */
						v = (v << 4) | (t - '0');          /* 先腾出低四位，再并入这一位数字。 */
					} else if ((t >= 'a') &&
						   (t <= 'f')) {                   /* 处理小写 a 至 f。 */
						v = (v << 4) | (t - 'a' + 10);     /* 把小写字母换算成 10 至 15。 */
					} else if ((t >= 'A') && (t <= 'F')) {     /* 处理大写 A 至 F。 */
						v = (v << 4) | (t - 'A' + 10);     /* 把大写字母换算成 10 至 15。 */
					} else {
						break;                             /* 遇到非十六进制字符即停止收集。 */
					}
				}

				if (i > 2) {                                       /* 至少一位数字有效时才执行替换。 */
					memmove(cmd, cmd + (i - 1),
						z_shell_strlen(cmd) - (i - 2));    /* 将尚未解析的字符串及终止符前移。 */
					*cmd++ = v;                                /* 写入解码后的单字节并推进指针。 */
					continue;                                  /* 继续解析剩余命令文本。 */
				}
			}
		}

		/* 离开转义和引用处理后，只有引号外的空白才能结束当前词。 */
		if (!quote && isspace((int) c)) {
			break;                                                     /* 保留分隔符位置，外层负责写入零终止符。 */
		}

		cmd += 1;                                                          /* 普通字符占用一个输入字节。 */
	}
	*ppcmd = cmd;                                                              /* 将实际停止位置反馈给逐词分割循环。 */

	return quote;                                                              /* 报告是否仍等待闭合引号。 */
}


/**
  * @brief  把可写命令行就地切分为指回同一缓冲区的参数指针。
  * @param  argc：[输出]非空指针，返回已登记的项数。
  * @param  argv：[输出]至少容纳 max_argc 个指针的数组，项指向 cmd 内部。
  * @param  cmd：[输入输出]可写且以零结束的文本，其存储期须覆盖参数的使用。
  * @param  max_argc：[输入]本轮最多登记的项数，须大于零。
  * @retval 返回最近完整扫描的词的未闭合引号，正常闭合或未扫描时为零。
  * @note   达到上限时最后一项指向尚未继续拆分的余串。execute 利用上限 2 逐层处理命令树。
  */
char z_shell_make_argv(size_t *argc, const char **argv, char *cmd,
		       uint8_t max_argc)
{
	char quote = 0;                        /* 初始无待闭合的引号。 */
	char c;                                /* 暂存当前输入字节。 */

	/* 用零字节分隔参数，argv 各项直接引用原缓冲；由调用方保证 max_argc 至少为 1。 */
	*argc = 0;                             /* 清空本轮的输出项数。 */
	do {                                   /* 重复取词直到文本末尾或用尽 argv 容量。 */
		c = *cmd;                      /* 查看当前字符以区分结束符、空白和参数开头。 */
		if (c == '\0') {               /* 零字节表示已扫描到可用命令末尾。 */
			break;                 /* 当前没有更多参数可登记。 */
		}

		if (isspace((int) c)) {        /* 只有词间的空白由这一层清零。 */
			*cmd++ = '\0';         /* 终止上一参数，并跨过这一分隔字节。 */
			continue;              /* 连续多个空白也分别跨过。 */
		}

		/* 先登记当前词的首地址。若已用完参数槽，最后一个指针保留整段尚未分词的余串。 */
		argv[(*argc)++] = cmd;         /* 保存参数的首地址，随后递增计数。 */
		if (*argc == max_argc) {       /* 检查已登记项数是否达到调用方上限。 */
			break;                 /* 末项指向的文本留给调用方决定如何进一步分词。 */
		}
		quote = make_argv(&cmd, c);    /* 原地处理一个完整词及其引号转义，返回后 cmd 指向分隔处。 */
	} while (true);

	return quote;                          /* 将引号状态交给上层报错或继续处理。 */
}

/**
  * @brief  在缓冲区内删除首个匹配串，并连同其前方紧挨的一个普通空格一起移除。
  * @param  buff：[输入输出]非空、可写、零结尾的字符串。
  * @param  buff_len：[输入输出]有效长度指针，匹配后扣除实际移除的字节数。
  * @param  pattern：[输入]非空指针，指向要精确查找的零结尾字符串。
  * @retval 无。
  * @note   没有匹配时维持原内容；匹配使用 strstr。
  */
void z_shell_pattern_remove(char *buff, uint16_t *buff_len, const char *pattern)
{
	char *pattern_addr = strstr(buff, pattern);                  /* 定位第一次出现 pattern 的地址。 */
	uint16_t shift;                                              /* 保存删除区域之后需移动的字节数。 */
	uint16_t pattern_len = z_shell_strlen(pattern);              /* 初始删除长度为匹配串的字节数。 */

	/* 只有找到目标才修改缓冲，若目标前紧跟普通空格则扩大删除范围。 */
	if (!pattern_addr) {                                         /* 缓冲中没有出现目标模式。 */
		return;                                              /* 不改内容与长度。 */
	}

	if (pattern_addr > buff) {                                   /* 确保匹配位置前还有可检查的字节。 */
		if (*(pattern_addr - 1) == ' ') {                    /* 这里仅移除普通空格，不处理其他空白字符。 */
			pattern_len++;                               /* 将相邻的一个空格计入删除长度。 */
			pattern_addr--;                              /* 删除起点向前移到该空格。 */
		}
	}

	shift = z_shell_strlen(pattern_addr) - pattern_len + 1;      /* 计入结尾零字节，复制完成后仍可按字符串读取。 */
	*buff_len -= pattern_len;                                    /* 扣除实际删除的字节数。 */

	memmove(pattern_addr, pattern_addr + pattern_len, shift);    /* 删除区段之后的内容覆盖到删除起点。 */
}

/**
  * @brief  根据链接器生成的起止地址计算根命令入口个数。
  * @param  无。
  * @retval 可在根命令表中按索引访问的项数。
  * @note   每个包装项占 sizeof(struct shell_cmd_entry) 字节。
  */
static inline uint32_t shell_root_cmd_count(void)
{
	return ((uint8_t *)__shell_root_cmds_end -
			(uint8_t *)__shell_root_cmds_start)/
				sizeof(struct shell_cmd_entry);    /* 两个段边界的字节差除以单个包装项的大小。 */
}

/**
  * @brief  用完整命令名线性查找根命令描述。
  * @param  syntax：[输入]指向有效的零结尾命令名。
  * @retval 返回匹配的静态描述指针，遍历结束仍未匹配时为 NULL。
  * @note   在根命令范围内按 strcmp 精确比较，只有根表在此查询。
  */
const struct shell_static_entry *root_cmd_find(const char *syntax)
{
	const size_t cmd_count = shell_root_cmd_count();            /* 缓存根表长度作为索引边界。 */
	const struct shell_cmd_entry *cmd;                          /* 保存本轮比较的根命令包装项。 */

	/* 按链接器汇集的根命令范围逐项比对名称，返回包装项所指向的具体命令描述。 */
	for (size_t cmd_idx = 0; cmd_idx < cmd_count; ++cmd_idx) {
		cmd = shell_root_cmd_get(cmd_idx);                  /* 按序号取得常驻包装项。 */
		if (strcmp(syntax, cmd->u.entry->syntax) == 0) {    /* 与登记项的完整语法名称逐字比较。 */
			return cmd->u.entry;                        /* 把匹配的命令描述交给调用方。 */
		}
	}

	return NULL;                                                /* 所有根项均未匹配。 */
}

/**
  * @brief  按下标枚举根命令或指定父项的一级子命令。
  * @param  parent：[输入]为 NULL 时枚举根表，否则指向当前父命令描述。
  * @param  idx：[输入]从零起算的序号。静态子表须按顺序枚举到结束项为止。
  * @param  dloc：[输出]访问非根层时需提供可写临时描述符，动态回调写入其中。
  * @retval 静态项返回常驻表项，动态项返回 dloc；无对应命令时为 NULL。
  * @note   静态子项以 syntax 为 NULL 的哨兵结束；根表按链接段项数定界。
  */
const struct shell_static_entry *z_shell_cmd_get(
					const struct shell_static_entry *parent,
					size_t idx,
					struct shell_static_entry *dloc)
{
	const struct shell_static_entry *res = NULL;                          /* 初始结果为空，也作为未找到项的返回值。 */

	/* 根层按链接段边界检查索引。子层则由父项指向静态表或提供动态枚举函数。 */
	if (parent == NULL) {
		return  (idx < shell_root_cmd_count()) ?
				shell_root_cmd_get(idx)->u.entry : NULL;      /* 表内下标返回根描述，越界在此层返回空指针。 */
	}

	__ASSERT_NO_MSG(dloc != NULL);                                        /* 子层枚举接口要求传入有效临时描述符。 */

	/* 先检查有无下层，再根据子表类型选取回调枚举或数组索引。 */
	if (parent->subcmd) {
		if (parent->subcmd->is_dynamic) {                             /* 动态项由父描述内保存的回调按序号生成。 */
			parent->subcmd->u.dynamic_get(idx, dloc);             /* 将结果写入调用方提供的临时区。 */
			if (dloc->syntax != NULL) {                           /* 回调提供空名称即表示到达末尾。 */
				res = dloc;                                   /* 动态结果沿用 dloc 的生命周期。 */
			}
		} else {
			if (parent->subcmd->u.entry[idx].syntax != NULL) {    /* 静态子表使用空名称作为哨兵。 */
				res = &parent->subcmd->u.entry[idx];          /* 返回表内原有描述符，不分配内存。 */
			}
		}
	}

	return res;                                                           /* 返回选定条目或 NULL。 */
}

/**
  * @brief  从当前命令层的第 0 项起逐个比较名称，返回首个精确匹配。
  * @param  parent：[输入]为 NULL 表示在根表查找。指定父项时从其子表查找。
  * @param  cmd_str：[输入]非空的零结尾命令词。
  * @param  dloc：[输出]非根查找时提供的有效临时描述，动态项可在后续枚举时被覆盖。
  * @retval 返回命中的静态项或 dloc，到本层末尾仍未命中时返回 NULL。
  * @note   输入 app spi2file 时，先以空父项匹配 app，再以 app 为父项在 acts_app_cmds 内匹配 spi2file。调用者使用命中项中的 handler。
  */
const struct shell_static_entry *z_shell_find_cmd(
					const struct shell_static_entry *parent,
					const char *cmd_str,
					struct shell_static_entry *dloc)
{
	const struct shell_static_entry *entry;                                    /* 当前被枚举的候选条目。 */
	struct shell_static_entry parent_cpy;                                      /* 此轮查找内独立保存父项描述，避免被动态子项覆盖。 */
	size_t idx = 0;                                                            /* 从该层第一项开始枚举。 */

	/* 动态父项可能也保存在 dloc 内。先复制父描述，避免枚举下一项时回调改写当前父项本身。 */
	if (parent) {
		memcpy(&parent_cpy, parent, sizeof(struct shell_static_entry));    /* 浅复制父描述，保留其中子表指针的取值。 */
		parent = &parent_cpy;                                              /* 后续枚举经副本追踪子表。 */
	}

	/* 这是串口命令真正的逐项查表循环。idx 每次加一，能匹配的只有 strcmp 判为相同的完整命令名。 */
	while ((entry = z_shell_cmd_get(parent, idx++, dloc)) != NULL) {
		if (strcmp(cmd_str, entry->syntax) == 0) {                         /* 用完整词与候选条目的 syntax 比较。 */
			return entry;                                              /* 命中即返回该项供上层检查 handler、子表等字段。 */
		}
	}

	return NULL;                                                               /* 到表尾还没有匹配。 */
}

/**
  * @brief  沿命令树跟随连续匹配的输入词，找出最后可用的命令描述。
  * @param  entry：[输入]起始父项，NULL 从根表开始。
  * @param  argc：[输入]argv 中可读的参数项数。
  * @param  argv：[输入]argc 个有效的零结尾词。
  * @param  match_arg：[输出]下一个待匹配词的索引，亦计入允许跳过的通配词。
  * @param  dloc：[输出]有效临时描述符，供动态子项枚举使用。
  * @param  only_static：[输入]为真时拒绝动态匹配结果。
  * @retval 返回最后匹配的项或起始项；静态限制命中动态项时返回 NULL。
  * @note   按配置可跳过带通配符的词，普通词匹配失败时保留此前的命令。
  */
const struct shell_static_entry *z_shell_get_last_command(
					const struct shell_static_entry *entry,
					size_t argc,
					const char *argv[],
					size_t *match_arg,
					struct shell_static_entry *dloc,
					bool only_static)
{
	const struct shell_static_entry *prev_entry = NULL;                 /* 记住每轮查找前的最近父项。 */

	*match_arg = Z_SHELL_CMD_ROOT_LVL;                                  /* 从根层对应的起始索引开始统计。 */

	/* 从第一个输入词向下查找；遇到参数等未登记的词时，保留先前命中的命令。 */
	while (*match_arg < argc) {

		if (IS_ENABLED(CONFIG_SHELL_WILDCARD)) {                    /* 开启通配功能后允许跳过带通配符的参数。 */
			if (z_shell_has_wildcard(argv[*match_arg])) {       /* 当前词含通配符时跳过精确匹配。 */
				(*match_arg)++;                             /* 该词计入已处理参数数目。 */
				continue;                                   /* 留在当前树节点继续检查下一个词。 */
			}
		}

		/* 以上一命令为父项查找下一级，失败后停在已经成功匹配的位置。 */
		prev_entry = entry;                                         /* 先保存上层命令，便于查找失败时恢复。 */
		entry = z_shell_find_cmd(entry, argv[*match_arg], dloc);    /* 在当前父项之下查找下一个输入词。 */
		if (entry) {                                                /* 找到子项后继续深入。 */
			(*match_arg)++;                                     /* 当前输入词与一个已登记命令匹配。 */
		} else {
			entry = prev_entry;                                 /* 恢复前一个成功匹配的描述。 */
			break;                                              /* 后续参数不再当作命令名继续查找。 */
		}

		/* 调用方只接受静态地址时撤回刚登记的动态匹配，临时描述不能作为常驻指针返回。 */
		if (only_static && (entry == dloc)) {
			(*match_arg)--;                                     /* 撤销动态项对应的匹配计数。 */
			return NULL;                                        /* 明确报告无法提供所要求的静态命令地址。 */
		}
	}

	return entry;                                                       /* 返回当前有效的最深条目。 */
}

/**
  * @brief  为所有已登记的 Shell 实例设置选定的根命令。
  * @param  cmd：[输入]有效的根命令名，NULL 表示清除已选根项。
  * @retval 找到指定命令或清除成功返回 0；名称不存在返回 -EINVAL。
  * @note   先查表再一起设置，查找失败时不更改实例上下文。
  */
int shell_set_root_cmd(const char *cmd)
{
	const struct shell_static_entry *entry;     /* 保存待选择或清除的根项地址。 */

	/* 按名称先验证根项，传空指针则解除已选根项。 */
	entry = cmd ? root_cmd_find(cmd) : NULL;    /* 根项名为空指针时选择结果设为空。 */

	if (cmd && (entry == NULL)) {               /* 显式提供名称时必须能在根表中找到。 */
		return -EINVAL;                     /* 指定名称不存在，提前返回参数错误。 */
	}

	/* 在链接器登记的所有 Shell 上应用相同的起点。 */
	STRUCT_SECTION_FOREACH(shell, sh) {
		sh->ctx->selected_cmd = entry;      /* 写入每个实例自己的运行上下文。 */
	}

	return 0;                                   /* 所有实例已更新。 */
}




/**
  * @brief  把后面仍有正文的连续空白压缩为一个，供通配符处理整理命令行。
  * @param  str：[输入输出]可写、零结尾的命令文本，也接受 NULL。
  * @retval 无。
  * @note   只在找到后续非空白字符时移动文本；前后端裁剪另有 z_shell_cmd_trim。
  */
void z_shell_spaces_trim(char *str)
{
	uint16_t len = z_shell_strlen(str);                         /* 缓存初始文本长度，空指针的长度由辅助函数视作零。 */
	uint16_t shift = 0U;                                        /* 统计当前连续多余空白的数量。 */

	if (!str) {                                                 /* 接受空指针输入。 */
		return;                                             /* 没有缓冲可处理时直接退出。 */
	}

	/* 在每个空白后查找下一个非空白字符，前移它和后续字符串，保留首个空白。 */
	for (uint16_t i = 0; i < len - 1; i++) {
		if (isspace((int)str[i])) {                         /* 空白可能是下一串连续空白的起点。 */
			for (uint16_t j = i + 1; j < len; j++) {    /* 向后寻找第一个非空白字符。 */
				if (isspace((int)str[j])) {         /* 仍为空白则累加可删除数量。 */
					shift++;                    /* 多余空白数加一。 */
					continue;                   /* 继续寻找该空白段后的正文。 */
				}

				if (shift > 0) {                    /* 有多余空白才搬移文本。 */
					memmove(&str[i + 1],
						&str[j],
						len - j + 1);       /* 把后续内容连同终止符移到首个空白后面。 */
					len -= shift;               /* 扣掉已移除的字节数。 */
					shift = 0U;                 /* 重置计数后再扫描下一段。 */
				}

				break;                              /* 该段后方首个非空白已经处理。 */
			}
		}
	}
}

/**
  * @brief  就地裁去命令缓冲两端的空白并更新有效长度。
  * @param  buff：[输入输出]可写且以零结尾的字符串。
  * @param  buff_len：[输入输出]有效指针，进入时应与实际字符串长度一致，返回去空白后的字节数。
  * @retval 无。
  * @note   命令为空则直接返回，全部为空白时产生空字符串。
  */
static void buffer_trim(char *buff, uint16_t *buff_len)
{
	uint16_t i = 0U;                                          /* 统计前导空白及随后一次探测的索引。 */

	if (buff[0] == '\0') {                                    /* 当前是空字符串时无需再找最后一个字节。 */
		return;                                           /* 返回已有的空命令。 */
	}

	/* 先从尾部逐字缩短有效长度，全为空白时清成空串并提前返回。 */
	while (isspace((int) buff[*buff_len - 1U])) {
		*buff_len -= 1U;                                  /* 每遇到一个尾部空白就缩短一个字节。 */
		if (*buff_len == 0U) {                            /* 缩至零表示原命令只有空白。 */
			buff[0] = '\0';                           /* 在第一个位置写回字符串结束符。 */
			return;                                   /* 避免继续以零长度访问尾部。 */
		}
	}
	buff[*buff_len] = '\0';                                   /* 终止去除尾部空白后的字符串。 */

	/* 统计最前面的空白。循环在取到首个非空白时也递增了 i，随后回退一次。 */
	while (isspace((int) buff[i++])) {
	}


	/* 将第一个非空白字符起的余串连同结尾零一起复制到缓冲首部。 */
	if (--i > 0) {
		memmove(buff, buff + i, (*buff_len + 1U) - i);    /* 前移的字节数包含结尾零。 */
		*buff_len = *buff_len - i;                        /* 更新去除前导空白后的长度。 */
	}
}

/**
  * @brief  去掉给定 Shell 命令缓冲的首尾空白并将编辑光标移到新末尾。
  * @param  shell：[输入]有效 Shell 实例，其 ctx 持有可写的命令缓冲、长度和光标偏移。
  * @retval 无。
  * @note   操作当前实例的命令行编辑状态。
  */
void z_shell_cmd_trim(const struct shell *shell)
{
	buffer_trim(shell->ctx->cmd_buff, &shell->ctx->cmd_buff_len);    /* 同步裁剪实例缓冲并修正有效长度。 */
	shell->ctx->cmd_buff_pos = shell->ctx->cmd_buff_len;             /* 编辑光标重定位到新的文本末尾。 */
}

/**
  * @brief  在已就绪且名称非空的静态设备中，按名称前缀枚举第 idx 个匹配设备。
  * @param  idx：[输入]从零起算的匹配序号。
  * @param  prefix：[输入]零结尾的名称前缀，NULL 表示接受任意名称。
  * @retval 返回匹配设备的静态描述符，索引超出匹配范围时为 NULL。
  * @note   未就绪或无有效名称的设备不占匹配序号。
  */
const struct device *shell_device_lookup(size_t idx,
				   const char *prefix)
{
	size_t match_idx = 0;                                    /* 已通过过滤条件的设备数量。 */
	const struct device *dev;                                /* 遍历静态设备描述符的指针。 */
	size_t len = z_device_get_all_static(&dev);              /* 同时取得首个设备和表内设备总数。 */
	const struct device *dev_end = dev + len;                /* 尾后指针用于界定设备遍历范围。 */

	/* 依设备登记顺序扫描，只给已就绪且名称满足要求的设备编号。 */
	while (dev < dev_end) {
		if (device_is_ready(dev)                         /* 以下条件共同筛出可枚举的设备。 */
		    && (dev->name != NULL)                       /* 跳过没有设备名指针的设备。 */
		    && (strlen(dev->name) != 0)                  /* 跳过空名称字符串。 */
		    && ((prefix == NULL)
			|| (strncmp(prefix, dev->name,
				    strlen(prefix)) == 0))) {    /* 没有前缀限制或名称以指定前缀开头时才继续。 */
			if (match_idx == idx) {                  /* 匹配设备中的索引达到调用者请求的值。 */
				return dev;                      /* 交回已就绪的设备描述符。 */
			}
			++match_idx;                             /* 只给通过筛选的设备累计序号。 */
		}
		++dev;                                           /* 考察表中下一个静态设备。 */
	}

	return NULL;                                             /* 匹配设备数量不足以达到所求索引。 */
}
