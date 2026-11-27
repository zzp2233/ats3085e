/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Shell 核心与命令编辑器共用的内部接口。文本分词、根表和子表遍历的实现在同目录 shell_utils.c。 */
#ifndef SHELL_UTILS_H__
#define SHELL_UTILS_H__

#include <zephyr.h>         /* 内核通用类型及编译环境定义。 */
#include <shell/shell.h>    /* 提供实例、命令描述和终端布局类型。 */

/* 供 C++ 编译单元以 C 链接名调用下面的内部函数。 */
#ifdef __cplusplus
extern "C" {
#endif

/* 找到只含子命令的节点但没有给出下一层名称时使用的提示文字。 */
#define SHELL_MSG_SPECIFY_SUBCOMMAND	"Please specify a subcommand.\n"

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
					   uint16_t offset2);

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
					      uint16_t offset2);

/**
  * @brief  更新当前光标和命令末尾在多行终端上的坐标。
  * @param  cons：[输入输出]非空布局结构，使用 name_len 与非零 terminal_wid 并写回四个坐标。
  * @param  buff_pos：[输入]当前光标在命令中的偏移。
  * @param  buff_len：[输入]命令的有效长度。
  * @retval 无。
  * @note   显示行和列均从 1 起算。
  */
void z_shell_multiline_data_calc(struct shell_multiline_cons *cons,
				 uint16_t buff_pos, uint16_t buff_len);

/**
  * @brief  计算用于 Shell 编辑的字符串字节长度。
  * @param  str：[输入]零结尾文本或 NULL。
  * @retval NULL 返回 0，否则将 strlen 的结果转换为 uint16_t。
  * @note   调用方使用受 Shell 缓冲区长度限制的文本。
  */
static inline uint16_t z_shell_strlen(const char *str)
{
	return str == NULL ? 0U : (uint16_t)strlen(str);    /* 空指针视为零长度，其他字符串按字节计数并转换为 Shell 使用的宽度。 */
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
char z_shell_make_argv(size_t *argc, const char **argv,
		       char *cmd, uint8_t max_argc);

/**
  * @brief  在缓冲区内删除首个匹配串，并连同其前方紧挨的一个普通空格一起移除。
  * @param  buff：[输入输出]非空、可写、零结尾的字符串。
  * @param  buff_len：[输入输出]有效长度指针，匹配后扣除实际移除的字节数。
  * @param  pattern：[输入]非空指针，指向要精确查找的零结尾字符串。
  * @retval 无。
  * @note   没有匹配时维持原内容；匹配使用 strstr。
  */
void z_shell_pattern_remove(char *buff, uint16_t *buff_len,
			    const char *pattern);

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
					struct shell_static_entry *dloc);

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
					struct shell_static_entry *dloc);

/**
  * @internal
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
					bool only_static);

/**
  * @brief  把后面仍有正文的连续空白压缩为一个，供通配符处理整理命令行。
  * @param  str：[输入输出]可写、零结尾的命令文本，也接受 NULL。
  * @retval 无。
  * @note   只在找到后续非空白字符时移动文本；前后端裁剪另有 z_shell_cmd_trim。
  */
void z_shell_spaces_trim(char *str);
/**
  * @brief  去掉给定 Shell 命令缓冲的首尾空白并将编辑光标移到新末尾。
  * @param  shell：[输入]有效 Shell 实例，其 ctx 持有可写的命令缓冲、长度和光标偏移。
  * @retval 无。
  * @note   操作当前实例的命令行编辑状态。
  */
void z_shell_cmd_trim(const struct shell *shell);

/**
  * @brief  用完整命令名线性查找根命令描述。
  * @param  syntax：[输入]指向有效的零结尾命令名。
  * @retval 返回匹配的静态描述指针，遍历结束仍未匹配时为 NULL。
  * @note   在根命令范围内按 strcmp 精确比较，只有根表在此查询。
  */
const struct shell_static_entry *root_cmd_find(const char *syntax);

/**
  * @brief  把此 Shell 尚在格式化缓冲中的输出提交给后端。
  * @param  shell：[输入]有效实例，其 fprintf_ctx 指向格式化输出上下文。
  * @retval 无。
  * @note   提交缓冲的细节由 z_shell_fprintf_buffer_flush 处理。
  */
static inline void z_transport_buffer_flush(const struct shell *shell)
{
	z_shell_fprintf_buffer_flush(shell->fprintf_ctx);    /* 使用当前实例保存的格式化输出缓冲上下文。 */
}

/**
  * @brief  检查当前实例是否已选定命令作为命令查找起点。
  * @param  shell：[输入]有效的 Shell 实例及上下文。
  * @retval selected_cmd 非空返回 true，否则返回 false。
  * @note   在选择模式下，上层可以从已选命令继续查找子项。
  */
static inline bool z_shell_in_select_mode(const struct shell *shell)
{
	return shell->ctx->selected_cmd == NULL ? false : true;    /* selected_cmd 有效时上层从选中的节点解析后续子命令。 */
}

#ifdef __cplusplus
}
#endif

#endif    /* SHELL_UTILS_H__ */
