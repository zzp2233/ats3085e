/* P4 响应的有界 JSON 扫描器。所有读取以 [p, end) 为界，文档一次校验后，各字段视图借用同一缓冲。 */
#include "p4_json.h"
#include <string.h>

/**
  * @brief  前移扫描位置，跳过 JSON 的空格、制表符和回车换行。
  * @param  p：[输入输出]当前扫描位置的地址。
  * @param  end：[输入]输入区末字节之后的边界指针。
  * @retval 无。
  * @note   结束指针指向最后一个可读字节之后。
  */
static void space(const char **p, const char *end)
{
    while (*p < end && (**p == ' ' || **p == '\r' || **p == '\n' || **p == '\t')) ++*p;    /* 指针到达 end 时先短路，不需要在文档末尾放置零字节。 */
}

/**
  * @brief  将一位十六进制字符换算为数值。
  * @param  c：[输入]待解码的单个字符。
  * @retval 0～15 为数值，-1 为非法字符。
  * @note   供 Unicode 转义解码使用。
  */
static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';         /* 数字位的数值是与字符零的差。 */
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;    /* 小写 a～f 衔接在数字 9 之后。 */
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;    /* 同样接受大写的 Unicode 转义位。 */
    return -1;                                        /* 其他输入使上层转义解析失败。 */
}
/* 协议标识、URL 与头字段解码为 ASCII；跳过未读取的值时仍检查引号、转义及 Unicode 代理对。 */
/**
  * @brief  扫描 JSON 字符串及其转义，按需解出协议用 ASCII 文本。
  * @param  p：[输入输出]指向起始双引号的游标地址。
  * @param  end：[输入]输入区末字节之后的边界指针。
  * @param  out：[输出]可写 cap 字节，NULL 表示只扫描。
  * @param  cap：[输入]目标总容量，包含结尾零。
  * @retval 非负为解码单位数，-1 为语法、字符集或容量错误。
  * @note   跳过未知值时允许 UTF-8 原始字节，不校验原始多字节编码。Unicode 转义检查代理对。
  */
static int string(const char **p, const char *end, char *out, size_t cap)
{
    /* 从开引号扫描到配对闭引号，遇到反斜杠改走转义解码。 */
    size_t used = 0;
    if (*p == end || *(*p)++ != '"') return -1;                                          /* 缺少开引号或已到尾端时，不能按字符串读取。 */
    while (*p < end) {                                                                   /* 输入有长度上限，即使缺少闭引号也不会继续读后续内存。 */
        unsigned int c = (unsigned char)*(*p)++;                                         /* 先转无符号字节，使原始非 ASCII 输入不受 char 符号性影响。 */
        if (c == '"') {                                                                  /* 此引号终止当前字符串。 */
            if (out) { if (used >= cap) return -1; out[used] = 0; }                      /* 空字符串也必须为终止字节留空间。 */
            return (int)used;                                                            /* 返回已解出的内容长度，不计结尾零。 */
        }
        if (c < 32) return -1;                                                           /* 原始控制字符不能直接出现在 JSON 字符串内。 */
        if (c == '\\') {                                                                 /* 转义序列必须根据下一字节决定如何解码。 */
            if (*p == end) return -1;                                                    /* 反斜杠后必须仍有一个可读的转义标识。 */
            c = (unsigned char)*(*p)++;                                                  /* 消耗反斜杠后的转义类别。 */
            if (c == 'u') {                                                              /* Unicode 转义还需读取四位十六进制。 */
                unsigned int i;                                                          /* 四位编码的循环索引。 */
                c = 0;                                                                   /* 从零开始累积该码位。 */
                for (i = 0; i < 4; ++i) {                                                /* 一个 Unicode 转义固定读取四位十六进制。 */
                    int digit;                                                           /* 本轮解析出的一位十六进制数。 */
                    if (*p == end || (digit = hex(*(*p)++)) < 0) return -1;              /* 现代输入缓冲不保证零结尾，先检查边界再取数。 */
                    c = c * 16u + (unsigned int)digit;                                   /* 将新半字节合入逐位累积的编码。 */
                }
                if (c >= 0xd800 && c <= 0xdbff) {                                        /* 高代理码位要求后续紧跟低代理。 */
                    unsigned int low = 0;                                                /* 独立累积低代理的四个半字节。 */
                    if (end - *p < 6 || (*p)[0] != '\\' || (*p)[1] != 'u') return -1;    /* 高代理之后必须还有完整的第二个 Unicode 转义。 */
                    *p += 2;                                                             /* 跳过第二个转义的反斜杠和 u。 */
                    for (i = 0; i < 4; ++i) {                                            /* 第二个转义的四位也要逐位校验。 */
                        int digit = hex(*(*p)++);                                        /* 读一位后定位到下一位。 */
                        if (digit < 0) return -1;                                        /* 非十六进制字符使整个 Unicode 转义无效。 */
                        low = low * 16u + (unsigned int)digit;                           /* 解析第二个四位编码并保留当前扫描进度。 */
                    }
                    if (low < 0xdc00 || low > 0xdfff) return -1;                         /* 第二个码位必须位于低代理范围。 */
                    c = 0x10000;                                                         /* 用超出 ASCII 的哨值表示合法代理对，供后面的输出筛选。 */
                } else if (c >= 0xdc00 && c <= 0xdfff) return -1;                        /* 单独出现低代理时拒绝这段字符串。 */

            /* 单字符转义映射为对应 ASCII 控制码；引号、反斜杠和斜杠则保持本身。 */
            } else if (c == 'b') c = 8;
            else if (c == 'f') c = 12;                                                   /* 解出换页符。 */
            else if (c == 'n') c = 10;                                                   /* 解出换行符。 */
            else if (c == 'r') c = 13;                                                   /* 解出回车符。 */
            else if (c == 't') c = 9;                                                    /* 解出水平制表符。 */
            else if (c != '"' && c != '\\' && c != '/') return -1;                       /* 只允许引号、反斜杠和斜杠按原值转义。 */
        }

        /* 只扫描未知字段时跳过写缓冲。调用方实际索取协议字符串时再执行输出字符集与容量限制。 */
        if (out) {
            if (!c || c > 127 || used + 1 >= cap) return -1;                             /* 有输出时拒绝嵌入零及非 ASCII，同时给字符串末尾留一字节。 */
            out[used] = (char)c;                                                         /* 通过字符与容量检查才写当前位置。 */
        }
        ++used;                                                                          /* 扫描模式只计数，写入模式还推进下一个输出位置。 */
    }
    return -1;                                                                           /* 尚未遇到闭引号就耗尽输入。 */
}

/**
  * @brief  递归消费一个完整 JSON 值并定位其末尾。
  * @param  p：[输入输出]当前值的扫描游标地址。
  * @param  end：[输入]输入区末字节之后的边界指针。
  * @param  depth：[输入]嵌套深度，最外层为 0，大于 12 拒绝。
  * @retval 0 已消费，-1 为语法错误、输入截断或深度超限。
  * @note   对容器元素递归，对字面量和数字按 JSON 文法扫描。
  */
static int skip(const char **p, const char *end, unsigned int depth)
{
    char c;                                                                    /* 当前值的首字节决定后面的语法分支。 */
    if (depth > 12) return -1;                                                 /* 在继续下探前截断过深的嵌套，限制 MCU 线程栈消耗。 */
    space(p, end);                                                             /* 允许值前有 JSON 空白。 */
    if (*p == end) return -1;                                                  /* 容器有分隔符却没有后续值时在这里失败。 */
    c = **p;                                                                   /* 保留容器类型供整个递归层判断对象还是数组。 */
    if (c == '"') return string(p, end, NULL, 0) < 0 ? -1 : 0;                 /* 跳过字符串时仍执行同一套格式与转义校验。 */

    /* 对象先读成员名和冒号，数组直接读元素；处理完一项后按逗号或闭括号决定下一步。 */
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';                                     /* 根据开括号选择必须配对的终止符。 */
        ++*p;                                                                  /* 消耗开括号，进入成员区。 */
        space(p, end);                                                         /* 空容器的括号之间也允许有空白。 */
        if (*p < end && **p == close) { ++*p; return 0; }                      /* 空容器在其闭括号后结束。 */
        for (;;) {                                                             /* 逐个处理容器成员，遇到闭括号返回。 */
            if (c == '{') {                                                    /* 对象的值前还必须有名称与冒号。 */
                if (string(p, end, NULL, 0) < 0) return -1;                    /* 对象每个成员先扫描名称；数组直接递归扫描值。 */
                space(p, end);                                                 /* 字段名和冒号之间允许有空白。 */
                if (*p == end || *(*p)++ != ':') return -1;                    /* 冒号分隔对象的名称和值。 */
            }
            if (skip(p, end, depth + 1)) return -1;                            /* 每下降一级都增加深度，返回时已位于子值末尾。 */
            space(p, end);                                                     /* 允许成员值与逗号或闭括号之间存在空白。 */
            if (*p == end) return -1;                                          /* 输入结束不能当作正常闭括号。 */
            if (**p == close) { ++*p; return 0; }                              /* 消耗闭括号后返回到上一层容器。 */
            if (*(*p)++ != ',') return -1;                                     /* 未看到闭括号时必须用逗号引出下一成员。 */
            space(p, end);                                                     /* 跳到下一成员起始位置，排除逗号后的空白。 */
        }
    }

    /* 处理布尔值和 null，剩余的非容器、非字符串值必须符合 JSON 数字语法。 */
    if (c == 't' || c == 'f' || c == 'n') {
        const char *word = c == 't' ? "true" : c == 'f' ? "false" : "null";    /* 三个字面量均须逐字节完全匹配。 */
        size_t n = strlen(word);                                               /* 只比较目标字面量自身占用的字节。 */
        if ((size_t)(end - *p) < n || memcmp(*p, word, n)) return -1;          /* 先验证剩余长度，再比较字面量，防止越界访问。 */
        *p += n;                                                               /* 游标前移恰好该字面量长度。 */
        return 0;                                                              /* 定界符或尾随内容由外围容器和文档检查。 */
    }

    /* 整数部分按有没有前导零分别读取；可选的小数和指数各自要求至少一个数字。 */
    if (c == '-') ++*p;
    if (*p == end) return -1;                                                  /* 只有负号而没有数字属于截断。 */
    if (**p == '0') ++*p;                                                      /* 零只能单独作为整数部分；尾随数字将由外围定界检查拒绝。 */
    else {
        if (**p < '1' || **p > '9') return -1;                                 /* 非零整数首位只允许 1～9。 */
        do { ++*p; } while (*p < end && **p >= '0' && **p <= '9');             /* 非零整数部分连续消费所有十进制数字。 */
    }
    if (*p < end && **p == '.') {                                              /* 整数后的小数点打开可选小数部分。 */
        ++*p;                                                                  /* 消耗小数点后检查首位小数。 */
        if (*p == end || **p < '0' || **p > '9') return -1;                    /* 小数点后至少有一位数字。 */
        do { ++*p; } while (*p < end && **p >= '0' && **p <= '9');             /* 越过剩余小数位，保留第一个非数字字节。 */
    }
    if (*p < end && (**p == 'e' || **p == 'E')) {                              /* 指数标志可以大写或小写。 */
        ++*p;                                                                  /* 越过指数标志。 */
        if (*p < end && (**p == '+' || **p == '-')) ++*p;                      /* 指数可带一个可选正负号。 */
        if (*p == end || **p < '0' || **p > '9') return -1;                    /* 即使指数带符号，仍须至少有一位指数数字。 */
        do { ++*p; } while (*p < end && **p >= '0' && **p <= '9');             /* 消耗完整指数数字串。 */
    }
    return 0;                                                                  /* 此时数字语法完整，后续定界交给上层。 */
}

/**
  * @brief  验证按字节长度给出的完整 JSON 文档，返回根值视图。
  * @param  data：[输入]至少 size 字节的原始文本，无需以零结束。
  * @param  size：[输入]文档字节数。
  * @param  out：[输出]非空根值视图，成功时有效。
  * @retval 0 成功，-1 为语法错误、截断、过深或尾随内容。
  * @note   后续访问器接收这里验证过的视图，原始缓冲在使用期间须保持有效。
  */
int p4_json_document(const char *data, size_t size, struct p4_json *out)
{
    const char *p = data, *end = data + size;    /* end 由调用方给出的真实响应长度决定。 */
    space(&p, end);                              /* 从原始数据跳过根值前的空白。 */
    out->p = p;                                  /* 借用去掉前置空白后的根值起点。 */
    if (skip(&p, end, 0)) return -1;             /* 一个根值必须能够完整消费。 */
    out->end = p;                                /* 记录根值的排他末尾，再检查其后的空白。 */
    space(&p, end);                              /* 根值以后只允许尾随空白。 */
    return p == end ? 0 : -1;                    /* 多出来的第二个根值或其他尾随字节均视为错误。 */
}

/**
  * @brief  在已验证的对象中查找唯一指定字段。
  * @param  object：[输入]已验证的对象视图。
  * @param  key：[输入]以零结束的非空指针，用于区分大小写比较。
  * @param  out：[输出]非空字段视图，返回 1 时有效。
  * @retval 1 找到，0 缺失，-1 为错误或目标键重复。
  * @note   继续扫描到对象结束以发现同名键；扫描经过的键须能解入 80 字节临时区。
  */
int p4_json_get(struct p4_json object, const char *key, struct p4_json *out)
{
    const char *p = object.p, *end = object.end;                   /* 查询游标限定在传入的对象视图内。 */
    int found = 0;                                                 /* 找到目标后继续扫描，检查后面是否重名。 */
    char name[80];                                                 /* 临时解码成员名，最多保留 79 个内容字节和结尾零。 */
    if (p == end || *p++ != '{') return -1;                        /* 只对对象执行按名称查找。 */
    space(&p, end);                                                /* 跳到首个成员名或空对象的结束符。 */

    /* 逐项记下值的首尾地址，再比较成员名；命中后仍扫描后续项以排除重名。 */
    while (p < end && *p != '}') {
        const char *value;                                         /* 指向该成员原始值的起点。 */
        if (string(&p, end, name, sizeof(name)) < 0) return -1;    /* 先解码成员名，这里同时约束可读取的名字长度。 */
        space(&p, end);                                            /* 成员名之后可有空白再出现冒号。 */
        if (p == end || *p++ != ':') return -1;                    /* 名称与值之间缺少冒号视为非法。 */
        space(&p, end);                                            /* 定位到值的第一个有效字节。 */
        value = p;                                                 /* 保留值的起始地址，skip 会把游标推进到值末尾。 */
        if (skip(&p, end, 0)) return -1;                           /* 跳过整个值，同时获得排他末尾。 */
        if (!strcmp(name, key)) {                                  /* 以大小写敏感的名称检查目标键。 */
            if (found) return -1;                                  /* 目标字段重名会产生歧义，拒绝整个查询。 */
            out->p = value; out->end = p; found = 1;               /* 借用原响应中的连续片段，不复制字段值。 */
        }
        space(&p, end);                                            /* 名称和值处理完后越过分隔符前空白。 */
        if (p < end && *p == ',') { ++p; space(&p, end); }         /* 逗号后继续扫描下一字段。 */
        else break;                                                /* 已验证对象的末尾没有下一字段。 */
    }
    return found;                                                  /* 成功找到为 1，遍历结束未命中为 0。 */
}

/**
  * @brief  将一个完整字符串视图解入缓冲并添加结尾零。
  * @param  value：[输入]已验证的单值视图。
  * @param  out：[输出]可写 capacity 字节的目标，可为 NULL。
  * @param  capacity：[输入]目标总字节数，含结尾零。
  * @retval 非负为解码长度，-1 为类型、编码约束、容量或尾随内容错误。
  * @note   输出限非零 ASCII 字符，传 NULL 时只按解析器的扫描规则计数。
  */
int p4_json_string(struct p4_json value, char *out, size_t capacity)
{
    int n = string(&value.p, value.end, out, capacity);    /* 复制的视图游标可前移，调用方持有的视图不受影响。 */
    return n < 0 || value.p != value.end ? -1 : n;         /* 排除合法字符串后还残留其他内容的视图。 */
}

/**
  * @brief  将纯十进制数字视图解为无符号 32 位数。
  * @param  value：[输入]已验证的值视图。
  * @param  out：[输出]非空整数接收地址。
  * @retval 0 成功，-1 为空、含非数字或溢出。
  * @note   在乘十和加当前位之前检查 UINT32_MAX 边界。
  */
int p4_json_u32(struct p4_json value, uint32_t *out)
{
    uint32_t n = 0;                                            /* 初值为零，逐位累积无符号十进制整数。 */
    const char *p = value.p;                                   /* 仅读取本视图边界内的内容。 */
    if (p == value.end) return -1;                             /* 空视图无法表示一个数。 */
    while (p < value.end) {                                    /* 每次消耗一个十进制字符。 */
        unsigned int d = (unsigned char)*p++ - '0';            /* 将非数字也折算为大于 9 的无符号值，统一拒绝。 */
        if (d > 9 || n > (UINT32_MAX - d) / 10u) return -1;    /* 移项比较可避免 n*10+d 先发生整数回绕。 */
        n = n * 10u + d;                                       /* 通过上限检查之后才进行乘加。 */
    }
    *out = n;                                                  /* 完整扫描成功才写回结果。 */
    return 0;                                                  /* 所有字符均为合法数字并已写回结果。 */
}

/**
  * @brief  统计已验证数组的元素个数，可同时借用首项视图。
  * @param  value：[输入]已验证的数组视图。
  * @param  first：[输出]可空，数组非空时写第一项视图。
  * @retval 非负为元素数，-1 为类型或解析错误。
  * @note   空数组返回零，此时不写首项。
  */
int p4_json_array(struct p4_json value, struct p4_json *first)
{
    const char *p = value.p;                                              /* 保存本次遍历的独立游标。 */
    int count = 0;                                                        /* 空数组从零个元素开始。 */
    if (p == value.end || *p++ != '[') return -1;                         /* 验证根标记为数组。 */
    space(&p, value.end);                                                 /* 越过开括号后的空白。 */

    /* 按完整元素推进游标，数组首项仅在调用者要求时记录。 */
    while (p < value.end && *p != ']') {
        const char *start = p;                                            /* 保存当前元素在原始文本中的起点。 */
        if (skip(&p, value.end, 0)) return -1;                            /* 先跳完整元素，保留下一元素的边界。 */
        if (!count && first) { first->p = start; first->end = p; }        /* 按调用者需要借用首项，不额外分配存储。 */
        ++count;                                                          /* 每完整消费一项才增加计数。 */
        space(&p, value.end);                                             /* 最后一个元素和闭括号之间也允许空白。 */
        if (p < value.end && *p == ',') { ++p; space(&p, value.end); }    /* 跳过元素间逗号及空白，准备读下一项。 */
        else break;                                                       /* 已无分隔符，结束经验证数组的遍历。 */
    }
    return count;                                                         /* 返回已完整统计的元素数。 */
}

/**
  * @brief  按从零开始的下标返回已验证数组的一项。
  * @param  value：[输入]已验证的数组视图。
  * @param  index：[输入]元素下标，从 0 起。
  * @param  out：[输出]非空视图，成功时可读。
  * @retval 0 找到，-1 为无效数组、解析错误或下标越界。
  * @note   先跳过前面的完整元素，不分配数组索引。
  */
int p4_json_at(struct p4_json value, unsigned int index, struct p4_json *out)
{
    const char *p = value.p;                                             /* 从该数组首字节重新扫描。 */
    unsigned int i = 0;                                                  /* 第一项下标从零算。 */
    if (p == value.end || *p++ != '[') return -1;                        /* 按下标读取只接受数组。 */
    space(&p, value.end);                                                /* 跳过数组开头空白。 */

    /* 从零计数，找到目标项即返回其原始文本区间。 */
    while (p < value.end && *p != ']') {
        const char *start = p;                                           /* 保存当前项的排他区间起点。 */
        if (skip(&p, value.end, 0)) return -1;                           /* 扫到目标前始终完整跳过嵌套元素。 */
        if (i++ == index) { out->p = start; out->end = p; return 0; }    /* 找到目标时直接写入原始文本的首尾，不复制内容。 */
        space(&p, value.end);                                            /* 先跳过当前项结束之后的空白。 */
        if (p == value.end || *p++ != ',') break;                        /* 看不到分隔符就没有后续元素可选。 */
        space(&p, value.end);                                            /* 逗号之后再次跳过空白。 */
    }
    return -1;                                                           /* 扫到数组末尾仍未命中该下标。 */
}

/**
  * @brief  检查待下发 HTTP 头名的字符及保留字段。
  * @param  s：[输入]以零结束的候选头名。
  * @retval 0 允许，-1 为非法字符、长度或保留名。
  * @note   仅接受字母、数字和连字符；以小写形式匹配传输层和签名保留名。
  */
static int header_name(const char *s)
{
    /* 这些字段由传输层生成或决定分帧方式，不允许远端附加头覆盖。 */
    static const char *reserved[] = { "host", "content-length", "connection",
        "transfer-encoding", "expect" };
    char lower[80];                                                 /* 小写副本用于保留字段比较，不影响原名字节。 */
    size_t i, j, n = strlen(s);                                     /* i 遍历名字，j 遍历保留名表，n 为源长度。 */
    if (!n || n >= sizeof(lower)) return -1;                        /* 借用 80 字节栈缓冲做头名比较，含结尾零。 */
    for (i = 0; i < n; ++i) {                                       /* 检查每一个头名字节并生成规范化副本。 */
        char c = s[i];                                              /* 取当前待校验的原始字符。 */
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return -1;       /* 检查允许的 ASCII 字母、数字及连字符。 */
        lower[i] = c >= 'A' && c <= 'Z' ? c + 32 : c;               /* 保留原名用于输出，仅将临时副本折为小写来检查。 */
    }
    lower[n] = 0;                                                   /* 补上 NUL，供后面的字符串比较使用。 */
    if (!strncmp(lower, "x-device-", 9)) return -1;                 /* 设备签名专用头也不能由服务端 PUT 元数据提供。 */
    for (j = 0; j < sizeof(reserved) / sizeof(reserved[0]); ++j)    /* 逐项和传输层保留名比对。 */
        if (!strcmp(lower, reserved[j])) return -1;                 /* 命中保留项即拒绝上层覆盖。 */
    return 0;                                                       /* 名称通过字符、前缀及保留列表检查。 */
}

/**
  * @brief  将 JSON 对象转换为逐行以 CRLF 终止的 HTTP 头部。
  * @param  value：[输入]已验证的头字段对象视图。
  * @param  out：[输出]至少 capacity 字节的可写缓冲，不可为空。
  * @param  capacity：[输入]包含结尾零的容量，单位字节。
  * @retval 非负为结果字节数（不含结尾零），-1 为解析、字段或容量错误。
  * @note   按 HTTP 规则忽略头名大小写判重，值限可打印 ASCII。
  */
int p4_json_headers(struct p4_json value, char *out, size_t capacity)
{
    const char *p = value.p;                                                                          /* 在声明的头字段对象边界内逐项遍历。 */
    size_t used = 0;                                                                                  /* 已写成 HTTP 头的字节数，起始为零。 */
    char name[80];                                                                                    /* 留 79 个头名字符及一个终止字节。 */
    if (p == value.end || *p++ != '{' || !capacity) return -1;                                        /* 只接受头对象且必须能放下最终 NUL。 */
    space(&p, value.end);                                                                             /* 进入第一项前越过空白。 */
    while (p < value.end && *p != '}') {                                                              /* 直到遇到经验证的对象闭括号。 */
        int n = string(&p, value.end, name, sizeof(name));                                            /* 先取字段名，随后验证名称和剩余输出空间。 */
        size_t i, start;                                                                              /* i 遍历字符，start 暂存当前已有头的开头或新值开头。 */
        if (n < 0 || header_name(name) || used + (size_t)n + 4 >= capacity) return -1;                /* 校验名字、单行固定开销及输出缓冲余量。 */
        /* HTTP 字段名不区分大小写，重复头可能改变服务端签名语义，须拒绝。 */
        for (start = 0; start < used;) {                                                              /* 逐行检查已经写入的头字段，比较时只处理名字部分。 */
            for (i = 0; i < (size_t)n; ++i) {                                                         /* 逐字节匹配正在比较的已输出头名。 */
                char a = name[i], b = out[start + i];                                                 /* 从新名字和已生成头行各取一个比较字符。 */
                if (a >= 'A' && a <= 'Z') a += 32;                                                    /* 比较时临时折叠候选头名的大写字母。 */
                if (b >= 'A' && b <= 'Z') b += 32;                                                    /* 同样折叠已写出头名的大写字母。 */
                if (a != b) break;                                                                    /* 首个不同字节已足以区分本行头名。 */
            }
            if (i == (size_t)n && out[start + i] == ':') return -1;                                   /* 候选名字长度匹配后还须确认冒号，防止把前缀当重名。 */
            while (start < used && out[start] != '\n') ++start;                                       /* 跳到该行末尾，开始比较下一条头字段。 */
            ++start;                                                                                  /* 跳过换行，定位下一旧头的开头。 */
        }

        /* 已排除与此前输出重复的头名，继续解码这一项的值并合成单行 HTTP 字段。 */
        space(&p, value.end);
        if (p == value.end || *p++ != ':') return -1;                                                 /* JSON 成员名称后必须有冒号。 */
        space(&p, value.end);                                                                         /* 跳过成员值前的空白。 */
        memcpy(out + used, name, n); used += n;                                                       /* 通过名称校验后按原始大小写生成头。 */
        out[used++] = ':'; out[used++] = ' ';                                                         /* 输出标准的冒号加空格分隔名字和值。 */
        start = used;                                                                                 /* 记住值的起点，稍后只检查该值内的控制字符。 */
        n = string(&p, value.end, out + used, capacity - used);                                       /* 在剩余的缓冲中解码头值，再检查 CRLF 余量。 */
        if (n < 0 || used + (size_t)n + 3 > capacity) return -1;                                      /* 为响应行尾的 CR、LF 和最终 NUL 留足三字节。 */
        used += n;                                                                                    /* 更新到当前头值末尾。 */
        for (i = start; i < used; ++i) if ((unsigned char)out[i] < 32 || out[i] >= 127) return -1;    /* 拒绝控制字符和 DEL，以免值中注入额外的 HTTP 头行。 */
        out[used++] = '\r'; out[used++] = '\n';                                                       /* 每条头字段使用 HTTP 规定的 CRLF 换行。 */
        space(&p, value.end);                                                                         /* 跳过该成员和值后的空白。 */
        if (p < value.end && *p == ',') { ++p; space(&p, value.end); }                                /* 逗号后继续读下一头字段。 */
        else break;                                                                                   /* 已无更多头字段时写终止字节。 */
    }
    out[used] = 0;                                                                                    /* 空头对象也产出有效的空字符串。 */
    return (int)used;                                                                                 /* 返回完整头块长度，不计最后的 NUL。 */
}
