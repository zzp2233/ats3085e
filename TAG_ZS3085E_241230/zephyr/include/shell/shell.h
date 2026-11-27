/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* ATS3085E 应用使用的 Zephyr Shell 公共接口。命令表保存语法、子表和处理函数指针，传输后端负责收发终端字节。SHELL_H__ 防止同一编译单元重复引入以下定义。 */
#ifndef SHELL_H__
#define SHELL_H__

/* 内核对象、终端类型、历史缓冲、格式化输出、日志后端以及构造命令表所用的预处理宏。 */
#include <zephyr.h>
#include <shell/shell_types.h>
#include <shell/shell_history.h>
#include <shell/shell_fprintf.h>
#include <shell/shell_log_backend.h>
#include <logging/log_instance.h>
#include <logging/log.h>
#include <sys/util.h>

/* 启用命令行选项解析时，每个 Shell 实例单独保存 getopt 的解析位置。 */
#if defined CONFIG_SHELL_GETOPT
#include <shell/shell_getopt.h>
#endif

/* C++ 编译单元沿用 C 链接名。 */
#ifdef __cplusplus
extern "C" {
#endif

/* 公共头也可供未配置 Shell 的代码包含；以下容量缺省为零，实际实例使用当前构建配置，单位为字节。 */
#ifndef CONFIG_SHELL_CMD_BUFF_SIZE
#define CONFIG_SHELL_CMD_BUFF_SIZE 0
#endif

#ifndef CONFIG_SHELL_PRINTF_BUFF_SIZE
#define CONFIG_SHELL_PRINTF_BUFF_SIZE 0
#endif

#ifndef CONFIG_SHELL_HISTORY_BUFFER
#define CONFIG_SHELL_HISTORY_BUFFER 0
#endif

/* 根命令处于第 0 层；每行十六进制转储最多显示 16 字节。 */
#define Z_SHELL_CMD_ROOT_LVL		(0u)

#define SHELL_HEXDUMP_BYTES_IN_LINE	16

/**
 * @brief  将必选参数后余下的命令行作为一个原始字符串交给处理函数。
 * @note   用在有处理函数、无子命令的叶节点上，mandatory 至少为 1 并计入命令名。通常解析器按空白分词并去掉分组引号；原始尾串保留剩余内容。
 */
#define SHELL_OPT_ARG_RAW	(0xFE)

/**
 * @brief  optional 字段的特殊值，跳过注册项对可选参数个数的上限检查。
 * @note   若指定了必选个数，最少参数检查仍生效；命令缓冲和 CONFIG_SHELL_ARGC_MAX 继续限制实际输入。
 */
#define SHELL_OPT_ARG_CHECK_SKIP (0xFF)

/**
 * @brief  可直接写入 optional 字段的最大普通数量，保留后两种取值表示特殊解析规则。
 */
#define SHELL_OPT_ARG_MAX		(0xFD)

/**
 * @brief Shell 命令注册、终端实例、传输后端及运行时接口。
 * @defgroup shell_api Shell 接口
 * @ingroup shell
 * @{
 */

/* 此处仅声明类型，供后续接口使用指针。 */
struct getopt_state;
struct shell_static_entry;

/**
 * @brief  供 Shell 枚举动态子命令时按序号填充一个命令项。
 * @param  idx [输入] 从 0 开始的子命令序号。
 * @param  entry [输出] 本次调用期间可写的描述对象，由调用方提供。
 * @retval 无。
 * @note   多个项的 syntax 须按字母顺序返回；下标越界时令 entry->syntax 为 NULL，通知框架停止枚举。
 */
typedef void (*shell_dynamic_get)(size_t idx,
				  struct shell_static_entry *entry);

/**
 * @brief  描述一组子命令的来源，is_dynamic 决定读取联合体的哪一项。
 */
struct shell_cmd_entry {
	/* 为真时由回调逐项获取，为假时从静态条目数组读取。 */
	bool is_dynamic;
	union union_cmd_entry {
		/** 按请求的序号生成一项动态命令。 */
		shell_dynamic_get dynamic_get;

		/** 静态命令表首项；根命令描述符也用该成员指向其唯一条目。 */
		const struct shell_static_entry *entry;
	} u;
};

/* 后续处理函数接收实例指针，完整的实例字段在文件下方。 */
struct shell;

/* 框架在 mandatory 非零时，用这两个字段检查处理函数将收到的 argc。 */
struct shell_static_args {
	uint8_t mandatory;    /*!< 最少参数个数，包含命令名；零表示不启用数量预检。 */
	uint8_t optional;     /*!< 额外允许的个数，或 SHELL_OPT_ARG_* 的特殊规则。 */
};

/**
 * @brief  从匹配名称前缀的已就绪设备中按序号取得设备对象。
 * @param  idx [输入] 在筛选结果中从 0 起算的序号。
 * @param  prefix [输入] 以零结尾的名称前缀；NULL 表示不限制前缀。
 * @retval 匹配时返回静态设备指针，越界或没有候选设备时返回 NULL。
 * @note   设备还必须有非空名称。返回对象由设备模型管理，调用者不释放。
 */
const struct device *shell_device_lookup(size_t idx,
				   const char *prefix);

/**
 * @brief  Shell 匹配命令后通过此指针调用处理函数。
 * @param  shell [输入] 发起命令的 Shell 实例，输出可沿用该终端。
 * @param  argc [输入] 传给本级处理函数的参数个数，含它自己的命令名。
 * @param  argv [输入] 本次分派的字符串指针数组，argv[0] 为处理函数对应的命令名。
 * @retval 约定 0 为成功，1 为已输出帮助，-EINVAL 为参数无效，-ENOEXEC 为未执行；具体命令可另行返回业务错误。
 * @note   输入 app spi2ram 8 时，匹配到子命令后 shell_spi2ram 收到 argc=2，argv[0] 为字符串 "spi2ram"，argv[1] 为字符串 "8"。参数指针随本次分派使用。
 */
typedef int (*shell_cmd_handler)(const struct shell *shell,
				 size_t argc, char **argv);

/**
 * @brief  处理字典命令，将命令行参数和注册时与名称关联的数据交给同一函数。
 * @param  shell [输入] 发起命令的 Shell 实例，输出可沿用该终端。
 * @param  argc [输入] 传给本级处理函数的参数个数，含它自己的命令名。
 * @param  argv [输入] 本次分派的字符串指针数组，argv[0] 为处理函数对应的命令名。
 * @param  data [输入] 字典项的值，由包装函数转换为 void * 传入；解释方式由注册者约定。
 * @retval 约定 0 为成功，1 为已输出帮助，-EINVAL 为参数无效，-ENOEXEC 为未执行；具体命令可另行返回业务错误。
 * @note   包装函数透传这个回调的返回值。
 */
typedef int (*shell_dict_cmd_handler)(const struct shell *shell, size_t argc,
				      char **argv, void *data);

/**
 * @brief  描述命令树的一个节点，供匹配、补全、帮助及命令分派共同使用。
 */
struct shell_static_entry {
	const char *syntax;                      /*!< 命令的匹配字符串，如 spi2ram。 */
	const char *help;                        /*!< 帮助文本，允许为 NULL。 */
	const struct shell_cmd_entry *subcmd;    /*!< 下一级命令集，叶节点通常为 NULL。 */
	shell_cmd_handler handler;               /*!< 匹配时调用的函数；只用作命令目录时可为 NULL。 */
	struct shell_static_args args;           /*!< 本级处理函数的必选和可选参数数量规则。 */
};

/**
 * @brief  在第 0 层登记带参数规则的根命令。
 * @param  syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  help [输入] 帮助文本指针，可为 NULL。
 * @param  handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @param  mandatory [输入] 必选参数个数，含命令名；零表示跳过数量预检。
 * @param  optional [输入] 可选参数个数或 SHELL_OPT_ARG_* 特殊值。
 * @note   宏生成命令描述和指向它的静态入口，将入口放入 .shell_root_cmd_<命令名>。链接脚本保留并排序这些入口，供 Shell 在起止符号之间查找。根命令名须唯一。
 */
#define SHELL_CMD_ARG_REGISTER(syntax, subcmd, help, handler,		   \
			       mandatory, optional)			   \
	static const struct shell_static_entry UTIL_CAT(_shell_, syntax) = \
	SHELL_CMD_ARG(syntax, subcmd, help, handler, mandatory, optional); \
	static const struct shell_cmd_entry UTIL_CAT(shell_cmd_, syntax)   \
	__attribute__ ((section("."					   \
			STRINGIFY(UTIL_CAT(shell_root_cmd_, syntax)))))	   \
	__attribute__((used)) = {					   \
		.is_dynamic = false,					   \
		.u = {.entry = &UTIL_CAT(_shell_, syntax)}		   \
	}

/**
 * @brief  按配置决定是否登记带参数规则的根命令。
 * @param  flag [输入] 配置宏；定义为 1 时启用，未定义或为 0 时关闭。
 * @param  syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  help [输入] 帮助文本指针，可为 NULL。
 * @param  handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @param  mandatory [输入] 必选参数个数，含命令名；零表示跳过数量预检。
 * @param  optional [输入] 可选参数个数或 SHELL_OPT_ARG_* 特殊值。
 * @note   启用时展开为 SHELL_CMD_ARG_REGISTER；关闭时仅生成标为未使用的引用，不生成可被 Shell 枚举的根入口。
 */
#define SHELL_COND_CMD_ARG_REGISTER(flag, syntax, subcmd, help, handler, \
					mandatory, optional) \
	COND_CODE_1(\
		flag, \
		(\
		SHELL_CMD_ARG_REGISTER(syntax, subcmd, help, handler, \
					mandatory, optional) \
		), \
		(\
		static shell_cmd_handler dummy_##syntax##_handler __unused = \
								handler;\
		static const struct shell_cmd_entry *dummy_subcmd_##syntax \
			__unused = subcmd\
		)\
	)
/**
 * @brief  登记由处理函数自行校验参数的根命令。
 * @param  syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  help [输入] 帮助文本指针，可为 NULL。
 * @param  handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @note   应用写 SHELL_CMD_REGISTER(app, &acts_app_cmds, …, cmd_acts_app) 后，框架按 app 找到根入口，再沿 subcmd 查找 spi2ram。此宏把两项参数数量置零。
 */
#define SHELL_CMD_REGISTER(syntax, subcmd, help, handler) \
	SHELL_CMD_ARG_REGISTER(syntax, subcmd, help, handler, 0, 0)

/**
 * @brief  按配置登记根命令，参数数量由其处理函数自行检查。
 * @param  flag [输入] 配置宏；定义为 1 时启用，未定义或为 0 时关闭。
 * @param  syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  help [输入] 帮助文本指针，可为 NULL。
 * @param  handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @note   相当于向 SHELL_COND_CMD_ARG_REGISTER 传入 0、0。
 */
#define SHELL_COND_CMD_REGISTER(flag, syntax, subcmd, help, handler) \
	SHELL_COND_CMD_ARG_REGISTER(flag, syntax, subcmd, help, handler, 0, 0)

/**
 * @brief  在文件作用域定义静态子命令数组及指向它的命令集描述符。
 * @param  name [输入] 供父命令引用的集合名，如 acts_app_cmds。
 * @param  ... [输入] 以逗号分隔的 SHELL_CMD 或 SHELL_CMD_ARG 初始化项，最后放入 SHELL_SUBCMD_SET_END。
 * @note   子项本身不会加入根命令链接段；父节点的 subcmd 指向此集合后，Shell 才能沿命令树找到它。
 */
#define SHELL_STATIC_SUBCMD_SET_CREATE(name, ...)			\
	static const struct shell_static_entry shell_##name[] = {	\
		__VA_ARGS__						\
	};								\
	static const struct shell_cmd_entry name = {			\
		.is_dynamic = false,					\
		.u = { .entry = shell_##name }				\
	}

/**
 * @brief  静态子命令表的结束项，其 syntax 为 NULL，枚举到此停止。
 */
#define SHELL_SUBCMD_SET_END {NULL}

/**
 * @brief  定义由回调提供子项的动态命令集。
 * @param  name [输入] 供父节点引用的集合名。
 * @param  get [输入] 遵循 shell_dynamic_get 的提供函数，按序号填充单个条目。
 * @note   描述符选择联合体的 dynamic_get 成员，枚举发生在 Shell 请求子命令时。
 */
#define SHELL_DYNAMIC_CMD_CREATE(name, get)		\
	static const struct shell_cmd_entry name = {	\
		.is_dynamic = true,			\
		.u = { .dynamic_get = get }		\
	}

/**
 * @brief  生成带参数规则的子命令初始化项。
 * @param  syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  help [输入] 帮助文本指针，可为 NULL。
 * @param  handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @param  mand [输入] 必选参数个数，含命令名；零表示跳过数量预检。
 * @param  opt [输入] 可选参数个数或 SHELL_OPT_ARG_* 特殊值。
 * @note   mand 非零时，超出允许范围的调用会先输出参数错误并跳过处理函数。
 */
#define SHELL_CMD_ARG(syntax, subcmd, help, handler, mand, opt) \
	SHELL_EXPR_CMD_ARG(1, syntax, subcmd, help, handler, mand, opt)

/**
 * @brief  根据单个配置宏生成有效的子命令或静态占位项。
 * @param  flag [输入] 配置宏；定义为 1 时启用，未定义或为 0 时关闭。
 * @param  syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  help [输入] 帮助文本指针，可为 NULL。
 * @param  handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @param  mand [输入] 必选参数个数，含命令名；零表示跳过数量预检。
 * @param  opt [输入] 可选参数个数或 SHELL_OPT_ARG_* 特殊值。
 * @note   直接传配置宏名，由 IS_ENABLED 处理未定义情形。关闭时表项仍占用空间，其名称为空串、处理函数和子表指针均为空。
 */
#define SHELL_COND_CMD_ARG(flag, syntax, subcmd, help, handler, mand, opt) \
	SHELL_EXPR_CMD_ARG(IS_ENABLED(flag), syntax, subcmd, help, \
			  handler, mand, opt)

/**
 * @brief  用可在编译期求值的表达式决定命令项中的指针和名称。
 * @param  _expr [输入] 可用于静态初始化的编译期表达式，非零时启用。
 * @param  _syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  _subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  _help [输入] 帮助文本指针，可为 NULL。
 * @param  _handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @param  _mand [输入] 必选参数个数，含命令名；零表示跳过数量预检。
 * @param  _opt [输入] 可选参数个数或 SHELL_OPT_ARG_* 特殊值。
 * @note   可组合多个配置条件；条件为假时仍保留本项和参数数字，名称置为空串，帮助、子表和处理函数置为空指针。
 */
#define SHELL_EXPR_CMD_ARG(_expr, _syntax, _subcmd, _help, _handler, \
			   _mand, _opt) \
	{ \
		.syntax = (_expr) ? (const char *)STRINGIFY(_syntax) : "", \
		.help  = (_expr) ? (const char *)_help : NULL, \
		.subcmd = (const struct shell_cmd_entry *)((_expr) ? \
				_subcmd : NULL), \
		.handler = (shell_cmd_handler)((_expr) ? _handler : NULL), \
		.args = { .mandatory = _mand, .optional = _opt} \
	}

/**
 * @brief  为静态表建立一个子命令项并保存处理函数指针。
 * @param  _syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  _subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  _help [输入] 帮助文本指针，可为 NULL。
 * @param  _handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @note   应用的 SHELL_CMD(spi2ram, NULL, …, shell_spi2ram) 让命令表保存名称和函数地址。该项的 mandatory 为零，shell_spi2ram 自行核对 argc。
 */
#define SHELL_CMD(_syntax, _subcmd, _help, _handler) \
	SHELL_CMD_ARG(_syntax, _subcmd, _help, _handler, 0, 0)

/**
 * @brief  按单一配置宏生成子命令项，参数数量交给处理函数检查。
 * @param  _flag [输入] 配置宏；定义为 1 时启用，未定义或为 0 时关闭。
 * @param  _syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  _subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  _help [输入] 帮助文本指针，可为 NULL。
 * @param  _handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @note   参见 SHELL_COND_CMD_ARG 的关闭分支。
 */
#define SHELL_COND_CMD(_flag, _syntax, _subcmd, _help, _handler) \
	SHELL_COND_CMD_ARG(_flag, _syntax, _subcmd, _help, _handler, 0, 0)

/**
 * @brief  按编译期表达式生成子命令项，参数数量交给处理函数检查。
 * @param  _expr [输入] 可用于静态初始化的编译期表达式，非零时启用。
 * @param  _syntax [输入] 用作命令名的标记，宏将其字符串化。
 * @param  _subcmd [输入] 子命令集的地址；没有下一级时可为 NULL。
 * @param  _help [输入] 帮助文本指针，可为 NULL。
 * @param  _handler [输入] 本级处理函数，可以为仅提供子命令的节点传 NULL。
 * @note   表达式可组合多个开关，字段填充规则与 SHELL_EXPR_CMD_ARG 相同。
 */
#define SHELL_EXPR_CMD(_expr, _syntax, _subcmd, _help, _handler) \
	SHELL_EXPR_CMD_ARG(_expr, _syntax, _subcmd, _help, _handler, 0, 0)

/* 从 (命令名, 值) 生成专属包装函数，用命令名拼出函数名，把值作为 data 连同原始 argc/argv 传给共用回调，并返回其结果。 */
#define Z_SHELL_CMD_DICT_HANDLER_CREATE(_data, _handler)		\
static int UTIL_CAT(cmd_dict_, GET_ARG_N(1, __DEBRACKET _data))(	\
		const struct shell *shell, size_t argc, char **argv)	\
{									\
	return _handler(shell, argc, argv,				\
			(void *)GET_ARG_N(2, __DEBRACKET _data));	\
}

/* 用字典对的第一项生成叶节点，绑定上面生成的包装函数；只接收命令名本身。 */
#define SHELL_CMD_DICT_CREATE(_data)					\
	SHELL_CMD_ARG(GET_ARG_N(1, __DEBRACKET _data), NULL, NULL,	\
		UTIL_CAT(cmd_dict_, GET_ARG_N(1, __DEBRACKET _data)), 1, 0)

/**
 * @brief  从一组 (命令名, 值) 构造静态字典子命令。
 * @param  _name [输入] 父命令可引用的字典集合名。
 * @param  _handler [输入] 所有字典项共用的 shell_dict_cmd_handler。
 * @param  ... [输入] 每对给出可补全的命令名和相应数据值。
 * @note   宏先为每项生成包装函数，再把这些节点及结束标记写入同一个静态表。下面的注册使输入 dictionary value_2 时以数据值 2 调用 my_handler。
 *
 * 调用示例：
 *	static int my_handler(const struct shell *shell,
 *			      size_t argc, char **argv, void *data)
 *	{
 *		int val = (int)data;
 *
 *		shell_print(shell, "(syntax, value) : (%s, %d)", argv[0], val);
 *		return 0;
 *	}
 *
 *	SHELL_SUBCMD_DICT_SET_CREATE(sub_dict_cmds, my_handler,
 *		(value_0, 0), (value_1, 1), (value_2, 2), (value_3, 3)
 *	);
 *	SHELL_CMD_REGISTER(dictionary, &sub_dict_cmds, NULL, NULL);
 */
#define SHELL_SUBCMD_DICT_SET_CREATE(_name, _handler, ...)		\
	FOR_EACH_FIXED_ARG(Z_SHELL_CMD_DICT_HANDLER_CREATE, (),		\
			   _handler, __VA_ARGS__)			\
	SHELL_STATIC_SUBCMD_SET_CREATE(_name,				\
		FOR_EACH(SHELL_CMD_DICT_CREATE, (,), __VA_ARGS__),	\
		SHELL_SUBCMD_SET_END					\
	)

/** @internal @brief 接收终端按键时用于识别跨字节转义序列的状态。 */
enum shell_receive_state {
	SHELL_RECEIVE_DEFAULT,     /*!< 按普通输入解释字符，ESC 会启动转义解析。 */
	SHELL_RECEIVE_ESC,         /*!< 刚收到 ESC，区分后续的左方括号或组合键。 */
	SHELL_RECEIVE_ESC_SEQ,     /*!< 已收到 ESC 和左方括号，根据后续字节识别方向键等。 */
	SHELL_RECEIVE_TILDE_EXP    /*!< 按键动作已经识别，消费预期的序列尾字节后回到普通输入。 */
};

/** @internal @brief 实例生命周期及紧急日志状态。 */
enum shell_state {
	SHELL_STATE_UNINITIALIZED,         /*!< 尚未初始化，或已释放传输层。 */
	SHELL_STATE_INITIALIZED,           /*!< 传输层已初始化，尚未处理交互输入或已停止。 */
	SHELL_STATE_ACTIVE,                /*!< 终端工作中，处理接收字节及命令。 */
	SHELL_STATE_PANIC_MODE_ACTIVE,     /*!< 已进入紧急日志输出模式。 */
	SHELL_STATE_PANIC_MODE_INACTIVE    /*!< 已请求紧急模式，但后端未能切换。 */
};

/** @brief 传输后端报告给 Shell 的收发事件。 */
enum shell_transport_evt {
	SHELL_TRANSPORT_EVT_RX_RDY,    /*!< 接收后端通知 Shell 检查输入。事件不携带字节或字节数；UART 数据保存在 rx_ringbuf，等线程读取。 */
	SHELL_TRANSPORT_EVT_TX_RDY     /*!< 后端可继续写入，唤醒等待发送的路径。 */
};

/**
 * @brief  传输后端有新的收发事件时调用的通知函数。
 * @param  evt [输入] 接收就绪或发送就绪事件。
 * @param  context [输入] 传输初始化时交给后端的上下文，在框架中指向对应 Shell 实例。
 * @retval 无。
 * @note   Shell 将通知转换为相应轮询信号；实际数据另经传输操作表读取或写入。
 */
typedef void (*shell_transport_handler_t)(enum shell_transport_evt evt,
					  void *context);


/**
 * @brief  注销完成时接收传输层及 Shell 状态的清理结果。
 * @param  shell [输入] 正在注销的实例。
 * @param  res [输入] 0 表示清理成功，-EBUSY 表示仍在处理输入，其他错误来自后端。
 * @retval 无。
 * @note   多线程配置下由 Shell 线程在退出前调用。
 */
typedef void (*shell_uninit_cb_t)(const struct shell *shell, int res);

/**
 * @brief  旁路接收回调；安装后直接获得后端读出的原始字节。
 * @param  shell [输入] 正在接收的实例。
 * @param  data [输入] 本次读取的临时缓冲区，若要跨回调使用须自行复制。
 * @param  len [输入] data 中的有效字节数，没有字符串终止符要求。
 * @retval 无。
 * @note   旁路期间普通命令行解析暂停；可用 shell_set_bypass 退出。
 */
typedef void (*shell_bypass_cb_t)(const struct shell *shell,
				  uint8_t *data,
				  size_t len);

/* 回调操作表只持有传输实例指针，完整对象在操作表后定义。 */
struct shell_transport;

/**
 * @brief  Shell 调用传输后端的操作表，同一命令树可绑定 UART 等不同输入输出通道。
 */
struct shell_transport_api {
	/**
	 * @brief  实例初始化时安装传输通道及事件回调。
	 * @param  transport [输入] 当前后端实例，使用其中的 ctx 访问后端私有状态。
	 * @param  config [输入] 后端约定的配置，如 UART 后端接收的设备指针。
	 * @param  evt_handler [输入] 后端在可读或可写时调用的通知函数。
	 * @param  context [输入] 留给 evt_handler 的上下文，通知时原样传回。
	 * @retval 0 表示成功，其他返回值由具体后端定义并送回初始化调用者。
	 */
	int (*init)(const struct shell_transport *transport,
		    const void *config,
		    shell_transport_handler_t evt_handler,
		    void *context);

	/**
	 * @brief  实例注销时停止传输并清理后端资源。
	 * @param  transport [输入] 当前后端实例，使用其中的 ctx 访问后端私有状态。
	 * @retval 0 表示成功，其他值为后端报告的失败。
	 */
	int (*uninit)(const struct shell_transport *transport);

	/**
	 * @brief  启动或调整传输发送方式。
	 * @param  transport [输入] 当前后端实例，使用其中的 ctx 访问后端私有状态。
	 * @param  blocking_tx [输入] 为 true 时请求阻塞发送，例如切换到紧急日志输出时。
	 * @retval 0 表示已经启用；后端拒绝所请求的模式时返回错误码。
	 */
	int (*enable)(const struct shell_transport *transport,
		      bool blocking_tx);

	/**
	 * @brief  把待发送数据提交给后端，并取得本次实际接收的字节数。
	 * @param  transport [输入] 当前后端实例，使用其中的 ctx 访问后端私有状态。
	 * @param  data [输入] 至少 length 字节的可读缓冲区。
	 * @param  length [输入] 此次请求发送的字节数。
	 * @param  cnt [输出] 可写的计数变量，记录此次由后端接受的字节数。
	 * @retval 0 表示本次后端操作成功，其他值由后端定义。
	 * @note   缓冲型后端可只接受一部分；cnt 不能用来判断最后一个字节是否已从物理线路发出。
	 */
	int (*write)(const struct shell_transport *transport,
		     const void *data, size_t length, size_t *cnt);

	/**
	 * @brief  从后端取出当前可用的输入字节。
	 * @param  transport [输入] 当前后端实例，使用其中的 ctx 访问后端私有状态。
	 * @param  data [输出] 容量至少为 length 字节的可写缓冲区。
	 * @param  length [输入] 此次最多读取的字节数。
	 * @param  cnt [输出] 实际取得的字节数；零表示本次没有字节供解析。
	 * @retval 0 表示本次后端操作成功，其他值由后端定义。
	 */
	int (*read)(const struct shell_transport *transport,
		    void *data, size_t length, size_t *cnt);

	/**
	 * @brief  供后端把较耗时的操作安排在 Shell 线程的事件循环中。
	 * @param  transport [输入] 当前后端实例，使用其中的 ctx 访问后端私有状态。
	 * @retval 无。
	 * @note   这是可选回调，未提供时框架跳过。
	 */
	void (*update)(const struct shell_transport *transport);

};

/* 把该后端的操作表和私有状态放在同一描述符内交给 Shell。 */
struct shell_transport {
	const struct shell_transport_api *api;    /*!< 对应后端的收发与生命周期回调表。 */
	void *ctx;                                /*!< 由后端解释的实例状态。 */
};

/**
 * @brief  按实例统计 Shell 日志输出的丢弃情况。
 */
struct shell_stats {
	atomic_t log_lost_cnt;    /*!< 原子累计后端丢失的日志条数。 */
};

/* 启用统计时为每个实例分配计数器并取得地址，关闭时定义为空且实例持有 NULL。 */
#ifdef CONFIG_SHELL_STATS
#define Z_SHELL_STATS_DEFINE(_name) static struct shell_stats _name##_stats
#define Z_SHELL_STATS_PTR(_name) (&(_name##_stats))
#else
#define Z_SHELL_STATS_DEFINE(_name)
#define Z_SHELL_STATS_PTR(_name) NULL
#endif /* CONFIG_SHELL_STATS */

/** @internal @brief 压入一个 32 位状态字的 Shell 编辑与执行标志。 */
struct shell_flags {
	uint32_t insert_mode :1;    /*!< 置位时在行内覆盖光标处的字符，行尾仍追加；清零时在光标处插入。 */
	uint32_t use_colors  :1;    /*!< 允许使用终端颜色控制序列。 */
	uint32_t echo        :1;    /*!< 是否把用户输入回显到终端。 */
	uint32_t obscure     :1;    /*!< 回显开启时用星号遮蔽输入字符。 */
	uint32_t processing  :1;    /*!< shell_process 正在处理输入，注销时会检查此位。 */
	uint32_t tx_rdy      :1;    /*!< 发送就绪标志，供传输路径协调写入。 */
	uint32_t mode_delete :1;    /*!< 置位使 ASCII DEL 执行退格，清零时删除光标处字符。 */
	uint32_t history_exit:1;    /*!< 请求退出命令历史浏览模式。 */
	uint32_t last_nl     :8;    /*!< 最近收到的换行字符，用于识别相邻的 CR/LF。 */
	uint32_t cmd_ctx     :1;    /*!< 正在执行命令处理函数，输出路径据此决定是否重绘输入行。 */
	uint32_t print_noinit:1;    /*!< 记录在 Shell 尚未进入活动态时发起的打印请求。 */
};

/* 编译期检查整个位域恰占一个 uint32_t，以匹配下面的整体更新视图。 */
BUILD_ASSERT((sizeof(struct shell_flags) == sizeof(uint32_t)),
	     "Structure must fit in 4 bytes");

/** @internal @brief 让状态字与命名位域共享存储。 */
union shell_internal {
	uint32_t value;              /*!< 整体状态字，供标志更新代码按字访问。 */
	struct shell_flags flags;    /*!< 同一存储按位字段解释的视图。 */
};

/* Shell 线程与传输层协调工作所用的信号槽号。 */
enum shell_signal {
	SHELL_SIGNAL_RXRDY,      /*!< Shell 线程等待的接收信号。多次通知可以合并，取多少数据由后端队列决定。 */
	SHELL_SIGNAL_LOG_MSG,    /*!< 日志后端有消息待处理。 */
	SHELL_SIGNAL_KILL,       /*!< 收到注销并结束线程的请求。 */
	SHELL_SIGNAL_TXDONE,     /*!< 必须紧邻 SHELL_SIGNALS 并排在最后；主循环用它作轮询数量，单独等待发送时才使用此信号。 */
	SHELL_SIGNALS            /*!< 信号槽位总数，供两个数组分配空间。 */
};

/**
 * @brief  每个 Shell 终端独立保存的可变状态，包括编辑缓冲、终端设置、轮询对象和输出锁。
 */
struct shell_ctx {
	const char *prompt;                        /*!< 当前提示符字符串，由实例引用。 */

	enum shell_state state;                    /*!< 当前实例生命周期状态。 */
	enum shell_receive_state receive_state;    /*!< 当前输入字符的转义解析阶段。 */

	/* 本次匹配到的命令描述，调用处理函数时从该副本取 handler 与参数规则。 */
	struct shell_static_entry active_cmd;

	/* 选定的命令根；为 NULL 时从全部已注册根命令开始匹配。 */
	const struct shell_static_entry *selected_cmd;

	/* VT100 颜色、光标位置和终端尺寸，供交互行编辑与重绘使用。 */
	struct shell_vt100_ctx vt100_ctx;

	/* 多线程模式下，Shell 线程完成注销并即将退出时调用。 */
	shell_uninit_cb_t uninit_cb;

	/* 旁路启用时，读取的原始数据交给此回调。 */
	shell_bypass_cb_t bypass;

#if defined CONFIG_SHELL_GETOPT
	/* 启用 getopt 的实例在此保存自己的选项解析状态。 */
	struct getopt_state getopt_state;
#endif

	uint16_t cmd_buff_len;                     /*!< 输入缓冲区中命令的有效字节数。 */
	uint16_t cmd_buff_pos;                     /*!< 输入行的光标位置，从缓冲区首字节计。 */

	uint16_t cmd_tmp_buff_len;                 /*!< 暂存命令的有效字节数。 */

	/* 当前正在编辑或等待执行的命令行。 */
	char cmd_buff[CONFIG_SHELL_CMD_BUFF_SIZE];

	/* 编辑与命令处理共用的临时命令缓冲。 */
	char temp_buff[CONFIG_SHELL_CMD_BUFF_SIZE];

	/* 按配置的格式化输出容量预留的字符数组。 */
	char printf_buff[CONFIG_SHELL_PRINTF_BUFF_SIZE];

	volatile union shell_internal internal;    /*!< 按整字或位访问的运行状态。 */

	struct k_poll_signal signals[SHELL_SIGNALS];    /*!< 每种事件的内核信号，供后端或控制调用唤醒处理者。 */

	/* Shell 线程内部的轮询对象，与同下标信号配对；主循环不轮询最后的发送完成项。 */
	struct k_poll_event events[SHELL_SIGNALS];

	struct k_mutex wr_mtx;                          /*!< 序列化终端输出及部分命令处理操作的互斥锁。 */
	k_tid_t tid;                                    /*!< 实例运行时的 Shell 线程标识。 */
};

/* Shell 提供给日志子系统的后端操作表，具体实现负责排队和输出日志。 */
extern const struct log_backend_api log_backend_shell_api;

/**
 * @brief  每个终端实例的输出换行设置，初始化时须选其中一种。
 */
enum shell_flag {
	SHELL_FLAG_CRLF_DEFAULT	= (1<<0),    /*!< 保留原有的 CR 或 LF。 */
	SHELL_FLAG_OLF_CRLF	= (1<<1)     /*!< 写出 LF 时先输出 CR。 */
};

/**
 * @brief  Shell 实例的静态配置与资源指针；运行中变化的编辑状态集中在 ctx。
 */
struct shell {
	const char *default_prompt;                     /*!< 初始化时采用的默认提示符。 */

	const struct shell_transport *iface;            /*!< 连接输入与输出后端及其状态。 */
	struct shell_ctx *ctx;                          /*!< 实例私有的运行时上下文。 */

	struct shell_history *history;                  /*!< 历史记录对象，关闭历史功能时可为空。 */

	const enum shell_flag shell_flag;               /*!< 格式化输出时的换行映射规则。 */

	const struct shell_fprintf *fprintf_ctx;        /*!< 格式化输出上下文与缓冲处理方式。 */

	struct shell_stats *stats;                      /*!< 可选的丢失日志计数对象。 */

	const struct shell_log_backend *log_backend;    /*!< 可选的 Shell 日志后端。 */

	LOG_INSTANCE_PTR_DECLARE(log);                  /*!< 按日志配置声明该实例关联的日志实例指针。 */

	const char *thread_name;                        /*!< 创建内核线程时所用的名称。 */
	struct k_thread *thread;                        /*!< 线程控制块，由实例宏分配。 */
	k_thread_stack_t *stack;                        /*!< 由实例宏分配的内核线程栈。 */
};

/**
 * @brief  格式化模块刷新缓冲区时，将字节流交给对应的 Shell 传输层。
 * @param  user_ctx [输入] 定义格式化上下文时绑定的 Shell 实例地址。
 * @param  data [输入] 至少 data_len 字节的可读数据，无需以零结尾。
 * @param  data_len [输入] 本段要写出的字节数。
 * @retval 无。
 * @note   SHELL_DEFINE 把该函数作为字节流输出回调登记。
 */
extern void z_shell_print_stream(const void *user_ctx, const char *data,
				 size_t data_len);
/**
 * @brief  在文件作用域定义可供内核枚举的 Shell 实例及其常驻资源。
 * @param  _name [输入] 实例标识，用来拼出上下文、缓冲区、栈等对象名。
 * @param  _prompt [输入] 与实例一起长期有效的默认提示符字符串。
 * @param  _transport_iface [输入] 后端传输描述符的指针。
 * @param  _log_queue_size [输入] 日志后端的 k_msgq 容量，以消息项数计，适用于使用该队列的延迟日志配置。
 * @param  _log_timeout [输入] 日志队列满时允许等待入队的时长，单位毫秒，超时则丢弃消息。
 * @param  _shell_flag [输入] 输出时的换行转换方式，取 enum shell_flag 的一个值。
 * @note   分别建立可写上下文、输出缓冲、日志与历史资源、线程栈和控制块，再将指针填入可遍历的 shell 实例段。真正的后端初始化在 shell_init 中执行。
 */
#define SHELL_DEFINE(_name, _prompt, _transport_iface,			      \
		     _log_queue_size, _log_timeout, _shell_flag)	      \
	static const struct shell _name;				      \
	static struct shell_ctx UTIL_CAT(_name, _ctx);			      \
	static uint8_t _name##_out_buffer[CONFIG_SHELL_PRINTF_BUFF_SIZE];     \
	Z_SHELL_LOG_BACKEND_DEFINE(_name, _name##_out_buffer,		      \
				 CONFIG_SHELL_PRINTF_BUFF_SIZE,		      \
				 _log_queue_size, _log_timeout);	      \
	Z_SHELL_HISTORY_DEFINE(_name##_history, CONFIG_SHELL_HISTORY_BUFFER); \
	Z_SHELL_FPRINTF_DEFINE(_name##_fprintf, &_name, _name##_out_buffer,   \
			     CONFIG_SHELL_PRINTF_BUFF_SIZE,		      \
			     true, z_shell_print_stream);		      \
	LOG_INSTANCE_REGISTER(shell, _name, CONFIG_SHELL_LOG_LEVEL);	      \
	Z_SHELL_STATS_DEFINE(_name);					      \
	static K_KERNEL_STACK_DEFINE(_name##_stack, CONFIG_SHELL_STACK_SIZE); \
	static struct k_thread _name##_thread;				      \
	static const STRUCT_SECTION_ITERABLE(shell, _name) = {		      \
		.default_prompt = _prompt,				      \
		.iface = _transport_iface,				      \
		.ctx = &UTIL_CAT(_name, _ctx),				      \
		.history = IS_ENABLED(CONFIG_SHELL_HISTORY) ?		      \
				&_name##_history : NULL,		      \
		.shell_flag = _shell_flag,				      \
		.fprintf_ctx = &_name##_fprintf,			      \
		.stats = Z_SHELL_STATS_PTR(_name),			      \
		.log_backend = Z_SHELL_LOG_BACKEND_PTR(_name),		      \
		LOG_INSTANCE_PTR_INIT(log, shell, _name)		      \
		.thread_name = STRINGIFY(_name),			      \
		.thread = &_name##_thread,				      \
		.stack = _name##_stack					      \
	}

/**
 * @brief  初始化传输通道与交互状态，并为此实例创建 Shell 线程。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  transport_config [输入] 传给后端 init 的配置指针，类型和可空性由后端确定。
 * @param  use_colors [输入] 请求使用彩色终端，实际还受 CONFIG_SHELL_VT100_COLORS 控制。
 * @param  log_backend [输入] 请求让该终端输出日志，须有对应后端支持。
 * @param  init_log_level [输入] 启用日志后端时使用的初始严重性过滤级别。
 * @retval 0 表示初始化并创建线程成功；已有线程返回 -EALREADY；后端初始化失败时透传其错误。
 * @note   Shell 线程启动后才使能传输、启动命令处理；返回 0 时后续的线程操作可能尚未完成。
 */
int shell_init(const struct shell *shell, const void *transport_config,
	       bool use_colors, bool log_backend, uint32_t init_log_level);

/**
 * @brief  发起注销，停止使用此实例的传输接口并清理 Shell 状态。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  cb [输入] 清理结果通知回调；多线程路径可为空，非多线程路径要求提供回调。
 * @retval 无。
 * @note   多线程配置通过 KILL 信号通知 Shell 线程退出，清理结果由回调的 res 返回；非多线程路径在此调用中完成清理。
 */
void shell_uninit(const struct shell *shell, shell_uninit_cb_t cb);

/**
 * @brief  把已初始化的终端切换为活动状态，使其可以处理命令。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @retval 0 表示成功；当前状态不是 SHELL_STATE_INITIALIZED 时返回 -ENOTSUP。
 * @note   在输出互斥锁保护下恢复终端显示并切换到活动状态。
 */
int shell_start(const struct shell *shell);

/**
 * @brief  停止交互处理，将实例状态切回已初始化。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @retval 0 表示成功；实例未初始化或已处于停止状态时返回 -ENOTSUP。
 * @note   传输资源留给后续重新启动或注销使用。
 */
int shell_stop(const struct shell *shell);

/**
 * @brief  普通消息使用终端默认文字颜色。
 */
#define SHELL_NORMAL	SHELL_VT100_COLOR_DEFAULT

/**
 * @brief  提示信息使用绿色文字。
 */
#define SHELL_INFO	SHELL_VT100_COLOR_GREEN

/**
 * @brief  可选项或补全候选使用青色文字。
 */
#define SHELL_OPTION	SHELL_VT100_COLOR_CYAN

/**
 * @brief  警告使用黄色文字。
 */
#define SHELL_WARNING	SHELL_VT100_COLOR_YELLOW

/**
 * @brief  错误使用红色文字。
 */
#define SHELL_ERROR	SHELL_VT100_COLOR_RED

/**
 * @brief  以 printf 格式向当前活动终端写入文本。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  color [输入] 当前文字的 VT100 颜色。
 * @param  fmt [输入] 以零结束的只读格式串。
 * @param  ... [输入] 与 fmt 中转换说明符匹配的实参。
 * @retval 无。
 * @note   供命令处理函数或其他线程使用；不能在中断中调用，Shell 线程也只应在命令处理上下文使用。实际输出要取得互斥锁。
 */
void shell_fprintf(const struct shell *shell, enum shell_vt100_color color,
		   const char *fmt, ...);

/**
 * @brief  接受已有的可变参数列表，格式化并输出到当前终端。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  color [输入] 当前文字的 VT100 颜色。
 * @param  fmt [输入] 以零结束的只读格式串。
 * @param  args [输入] 与格式串匹配、已初始化的 va_list。
 * @retval 无。
 * @note   供命令处理函数或其他线程使用；不能在中断中调用，Shell 线程也只应在命令处理上下文使用。实际输出要取得互斥锁。
 */
void shell_vfprintf(const struct shell *shell, enum shell_vt100_color color,
		   const char *fmt, va_list args);

/**
 * @brief  打印一行包含偏移、十六进制字节和可打印字符的转储。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  offset [输入] 本行标注的起始字节偏移，不用于给 data 取地址。
 * @param  data [输入] 指向本行起始数据，至少可读 min(len, 16) 字节。
 * @param  len [输入] 可用的字节数，本行最多显示 SHELL_HEXDUMP_BYTES_IN_LINE 个字节。
 * @retval 无。
 * @note   不足一行时补空白对齐，可打印字符直接显示，其余以点表示。
 */
void shell_hexdump_line(const struct shell *shell, unsigned int offset,
			const uint8_t *data, size_t len);

/**
 * @brief  从偏移零开始把一段内存按行输出为十六进制及字符。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  data [输入] 指向连续可读的 len 字节。
 * @param  len [输入] 所需转储的总字节数。
 * @retval 无。
 * @note   每行上限由 SHELL_HEXDUMP_BYTES_IN_LINE 定义；最终不足一行时只显示余量。
 */
void shell_hexdump(const struct shell *shell, const uint8_t *data, size_t len);

/**
 * @brief  向当前终端打印提示信息并自动追加换行。
 * @param  _sh [输入] 目标 Shell 实例。
 * @param  _ft [输入] 可与换行字面量拼接的格式字符串。
 * @param  ... [输入] 格式字符串所需的可变参数。
 * @retval 无。
 * @note   经 shell_fprintf 以 SHELL_INFO 颜色输出，适用相同的线程上下文约束。
 */
#define shell_info(_sh, _ft, ...) \
	shell_fprintf(_sh, SHELL_INFO, _ft "\n", ##__VA_ARGS__)

/**
 * @brief  向当前终端打印普通消息并自动追加换行。
 * @param  _sh [输入] 目标 Shell 实例。
 * @param  _ft [输入] 可与换行字面量拼接的格式字符串。
 * @param  ... [输入] 格式字符串所需的可变参数。
 * @retval 无。
 * @note   经 shell_fprintf 以 SHELL_NORMAL 颜色输出，适用相同的线程上下文约束。
 */
#define shell_print(_sh, _ft, ...) \
	shell_fprintf(_sh, SHELL_NORMAL, _ft "\n", ##__VA_ARGS__)

/**
 * @brief  向当前终端打印警告并自动追加换行。
 * @param  _sh [输入] 目标 Shell 实例。
 * @param  _ft [输入] 可与换行字面量拼接的格式字符串。
 * @param  ... [输入] 格式字符串所需的可变参数。
 * @retval 无。
 * @note   经 shell_fprintf 以 SHELL_WARNING 颜色输出，适用相同的线程上下文约束。
 */
#define shell_warn(_sh, _ft, ...) \
	shell_fprintf(_sh, SHELL_WARNING, _ft "\n", ##__VA_ARGS__)

/**
 * @brief  向当前终端打印错误并自动追加换行。
 * @param  _sh [输入] 目标 Shell 实例。
 * @param  _ft [输入] 可与换行字面量拼接的格式字符串。
 * @param  ... [输入] 格式字符串所需的可变参数。
 * @retval 无。
 * @note   经 shell_fprintf 以 SHELL_ERROR 颜色输出，适用相同的线程上下文约束。
 */
#define shell_error(_sh, _ft, ...) \
	shell_fprintf(_sh, SHELL_ERROR, _ft "\n", ##__VA_ARGS__)

/**
 * @brief  处理传输后端已经就绪的输入，在活动状态下收集字符并执行完整命令。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @retval 无。
 * @note   通常由 Shell 线程响应接收信号调用；若由外部驱动处理，应在有数据时调用并避免重入。
 */
void shell_process(const struct shell *shell);

/**
 * @brief  修改此实例命令行前显示的提示符，并更新显示宽度所用的名称长度。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  prompt [输入] 非空、以零结尾的提示符，实例直接保存指针，使用期间字符串须持续有效。
 * @retval 0 表示成功；prompt 为 NULL 时返回 -EINVAL。
 */
int shell_prompt_change(const struct shell *shell, const char *prompt);

/**
 * @brief  输出当前命令的帮助及可用子命令列表。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @retval 无。
 * @note   输出受实例的写互斥锁保护，通常由命令处理函数请求。
 */
void shell_help(const struct shell *shell);

/* 处理结果 1 表示已经显示命令帮助。 */
#define SHELL_CMD_HELP_PRINTED	(1)

/* 仅在启用实例级选项解析时向调用者提供以下两个接口。 */
#if defined CONFIG_SHELL_GETOPT
/**
 * @brief  使用当前实例的 getopt 状态扫描一项命令行选项。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  argc [输入] 参数个数，包含命令名。
 * @param  argv [输入] 参数字符串数组，解析过程使用当前实例保存的位置。
 * @param  ostr [输入] 允许的选项字符和参数规则，选项后的冒号指示需要参数。
 * @retval 匹配时返回选项字符；未知选项返回问号；缺少参数时，若 ostr 以冒号起始则返回冒号，否则返回问号；扫描结束返回 -1。
 * @note   本接口采用源自 FreeBSD 的 getopt 解析方式。
 */
int shell_getopt(const struct shell *shell, int argc, char *const argv[],
		 const char *ostr);

/**
 * @brief  取得当前实例专用的 getopt 状态，供读取选项参数或当前下标。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @retval 返回 ctx 中 getopt_state 的地址，生命周期随该实例，调用者不释放。
 * @note   该声明仅对启用 CONFIG_SHELL_GETOPT 的构建可见。
 */
struct getopt_state *shell_getopt_state_get(const struct shell *shell);
#endif /* CONFIG_SHELL_GETOPT */

/**
 * @brief  由诊断代码提交一条完整命令行，交给 Shell 按普通分派路径执行。
 * @param  shell [输入] 目标实例；配置了 CONFIG_SHELL_BACKEND_DUMMY 时可用 NULL 选择虚拟后端。
 * @param  cmd [输入] 以零结束的命令字符串，须能连同结尾零存入命令缓冲区。
 * @retval 返回分派或处理函数的结果；cmd 为空指针时为 -ENOEXEC，命令过长为 -ENOMEM，缺少可用实例为 -EINVAL。
 * @note   不得从 Shell 命令处理函数内调用；调试命令名称和参数没有稳定接口的保证。
 */
int shell_execute_cmd(const struct shell *shell, const char *cmd);

/**
 * @brief  为所有已登记的 Shell 实例选择同一个根命令，后续输入从它的子命令开始匹配。
 * @param  cmd [输入] 已登记根命令的名称，NULL 恢复默认根命令集合。
 * @retval 0 表示设置或恢复成功；找不到指定名称时返回 -EINVAL。
 * @note   效果近似交互式 select 命令，但作用于全部实例。
 */
int shell_set_root_cmd(const char *cmd);

/**
 * @brief  设置接收旁路，使原始输入直接交给回调处理。
 * @param  shell [输入] 有效的 Shell 实例指针。
 * @param  bypass [输入] 收到数据时要调用的函数；NULL 表示恢复普通命令行处理。
 * @retval 无。
 * @note   传入的函数须在旁路启用期间保持可调用。
 */
void shell_set_bypass(const struct shell *shell, shell_bypass_cb_t bypass);

/**
 * @brief  设置行内字符编辑方式。
 * @param  shell [输入] 要修改的 Shell 实例。
 * @param  val [输入] 为 true 时在行内覆盖光标处字符，为 false 时插入。
 * @retval 成功时返回修改前的值 0 或 1；shell 为 NULL 时返回 -EINVAL。
 * @note   底层按原子方式更新实例状态字中的对应标志。
 */
int shell_insert_mode_set(const struct shell *shell, bool val);

/**
 * @brief  设置终端是否使用彩色输出。
 * @param  shell [输入] 要修改的 Shell 实例。
 * @param  val [输入] 为 true 时允许输出颜色控制信息。
 * @retval 成功时返回修改前的值 0 或 1；shell 为 NULL 时返回 -EINVAL。
 * @note   底层按原子方式更新实例状态字中的对应标志。
 */
int shell_use_colors_set(const struct shell *shell, bool val);

/**
 * @brief  设置终端是否回显用户键入的字符。
 * @param  shell [输入] 要修改的 Shell 实例。
 * @param  val [输入] 为 true 时打开输入回显。
 * @retval 成功时返回修改前的值 0 或 1；shell 为 NULL 时返回 -EINVAL。
 * @note   底层按原子方式更新实例状态字中的对应标志。
 */
int shell_echo_set(const struct shell *shell, bool val);

/**
 * @brief  设置输入回显时是否遮蔽用户键入的内容。
 * @param  shell [输入] 要修改的 Shell 实例。
 * @param  obscure [输入] 为 true 且回显开启时，以星号显示输入，适用于密码等场景。
 * @retval 成功时返回修改前的值 0 或 1；shell 为 NULL 时返回 -EINVAL。
 * @note   底层按原子方式更新实例状态字中的对应标志。
 */
int shell_obscure_set(const struct shell *shell, bool obscure);

/**
 * @brief  设置收到 ASCII DEL 字节时的行编辑行为。
 * @param  shell [输入] 要修改的 Shell 实例。
 * @param  val [输入] 为 true 时删除光标前一字符，为 false 时删除光标处字符。
 * @retval 成功时返回修改前的值 0 或 1；shell 为 NULL 时返回 -EINVAL。
 * @note   底层按原子方式更新实例状态字中的对应标志。
 */
int shell_mode_delete_set(const struct shell *shell, bool val);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* SHELL_H__ */
