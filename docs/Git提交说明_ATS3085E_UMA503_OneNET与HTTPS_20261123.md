# ATS3085E 与 UMA503-V7 网络改动 Git 提交说明

## 目录

1. [结论先行](#summary)
2. [现象与问题定位](#diagnosis)
3. [分文件改动](#changes)
4. [验证命令、结果和版本](#checks)
5. [边界](#limits)
6. [遗留、影响和回退](#rollback)
7. [提交拆分与顺序](#split)
8. [可复制的七段提交正文](#messages)
9. [暂存与交付核对](#review)

<a id="summary"></a>

## 1. 结论先行

复核日期为 2026-11-23，北京时间。ATS 当前 HEAD `46d8250`，UMA 当前 HEAD `44138c0`，均为 11 月 19 日分组交付提交。其前一组 `3f15579` / `2427020` 是 SPI 提速及 DMA 环基线。两端本次目标是把已有分组交付接入 UMA HTTPS 消费，并独立接通 OneNET 登录和绑定属性。复核时两仓库暂存区均为空，正文描述的是相对当前 HEAD 的已跟踪差异及相关新增文件。

| 通道 | 本轮成功的判据 | 实测结果 |
|---|---|---|
| OneNET | 503 登录、订阅、匹配属性回执，并写入绑定镜像 | `record_state` 探针返回 200，`bind_status=0` 下发匹配回执、保存与当次读回通过 |
| 普通 HTTPS | 两端事务闭合、对象存储返回 200、业务同步确认、下载正文一致 | 48,001 B，两组、127 笔、零补发，CRC32 `887489ae` |
| 板端背压注入 | 四组均有停顿且组缓冲完整，恢复后上传及业务确认成功 | 144,001 B，四组、339 笔、零补发，CRC32 `23122b0b`，四组 `guard_ok=1`，PC complete 返回 `SYNC_SUCCESS` |

上传固件组合为 ATS `ATS3085E_SPI_P3_QUIET_20261123.fw`，UMA 按用途选 `UMA503_ONENET_MQTT7_P3_NORMAL_20261123.zip` 或 `UMA503_ONENET_MQTT7_P3_BACKPRESSURE_TEST_20261123.zip`。包 SHA-256 与双端操作见[复盘第 8 章](ATS3085E与UMA503_OneNET及HTTPS上传调试复盘_20261119-23.md#build)。P1 的 8 MHz 与离线消费者回归属于先前基线，本轮真实上传验收使用 4 MHz。

<a id="diagnosis"></a>

## 2. 现象与问题定位

| 输入与首个异常 | 排查结论及责任位置 | 如何验收 |
|---|---|---|
| SIM 已注册，虚拟 AT 返回 `rc=0`、`+CEREG: 0,1`，任务仍不进 MQTT | `onenet_config.c` 原判断按外部 AT 要求正文有 OK。虚拟 AT 用返回码表达完成 | API 成功且 stat=1/5 才推进；单字段 URC、错误或截断不能推进 |
| MQTT INIT 后 `ummqtt_recv_ta` DataAbort | SDK 创建接收任务早于客户端指针就绪，原调用任务优先级 90，接收任务 80 可抢占 | `mqtt_init_guarded()` 用 79 覆盖初始化窗口，完成后恢复，真机进入 CONNECT 和属性处理 |
| 发布 rc=0 但 property reply 超时 | PC 对照为旧编号收到 2405、13 位纯数字收到 200；旧解析丢弃四位 code | 对照请求 ID 和业务 code；板端真机取得匹配的 200 |
| COM6 无会话清理进入 `osa_mem.c:1305`，随后 Silent Reset | SDK 释放 NULL 断言，`sl_file_discard()` / HTTPS 响应清理有未分配路径 | 从机与生产 HTTPS 模型拒绝 NULL free，实机后续启动未再出现原断言 |
| LE Rand 超时后 ATS shell Usage Fault，PC=0 | 旧随机源的 HCI 超时分支与空回调相符，问题在 SPI 前 | SE_TRNG 48 B 成功才发包。占用、超时或明显异常样本时返回错误，transfers 不能增加 |
| 请求 1 MHz，`ret=-34 transfers=0` | 32 MHz HOSC /14 的最低可请求值为 2,285,715 Hz，原配置不受支持 | 改用 /8 后实机回读 4,000,000 Hz，并用真实分频函数验证非法档不写寄存器 |
| 首组 48,000 B 已到，随后主机 -116、从机 -216 | 14:13 FIRST_FAULT 证实 SPI 部分帧先发生，worker 随后取消，尚未进入 TCP/TLS；物理触发未定位 | 网络作业期间不归还 READY，保留完整组，结束后复查边界且只归还对应许可 |
| QUIET 后 DNS/TCP 通过，但 TLS `-9984 verify=0x08` | 对象存储和业务入口证书链不同，验证对象根缺 UCA Global G2 Root | 维持 TLS 校验，加根后真实 PUT 返回 200，并取回正文对账 |
| 自动装载的 clear 无回执且乱码 | COM4 实际是 2 Mbaud，默认 115200 错误，测试未开始 PUT | 显式 2,000,000 / 8N1，逐行回执，发送前验证连续偏移、长度和 CRC |
| PUT 已 200，业务仍 SYNCING | 对象写入与业务侧完成有不同接口 | 同一会话检查连续字节、待传片和两个 ID，再 complete；业务为 00000、SYNC_SUCCESS |

现场同时出现网络与 SPI 日志时，以清理前时间为准。14:35 的首笔全 00 与 13:56 的收齐首组后超时是两种现象，重焊后仍曾复现后一种。P3 并未因此证明所有板级问题消失。

<a id="changes"></a>

## 3. 分文件改动（按依赖顺序）

下表采用各仓库相对路径。ATS 中 `A/` 指 `TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main/`，UMA 中 `P/` 指 `startup/app_release/`。函数名和现有文件行号便于从当前工作区跳转。

### 3.1 ATS3085E 固件

| 文件和定位 | 改动及关键行为 | 为什么这样改 / 影响 |
|---|---|---|
| `A/spi_link_file_protocol.h:14` | 在 P1 命令之后追加 STREAM、HTTPS、HTTP、HTTP_META、HTTP_READ 与失败状态；定义 48 B 种子、3,072 B 扩展元数据、16 KiB 扩展响应、400 B 页读取 | 让旧分组帧继续承载新控制，双端需按同一协议解释新增命令；512 B 事务及 480 B 正文保留 |
| `A/spi_link_entropy.h:18、:57`（新增） | 用安全引擎取 64 位样本，单组等待 2,000 µs，48 B 种子在发帧前准备；检查占用与全零、全一、重复，失败清除输出 | 隔离蓝牙 HCI 超时；占用时可返回忙，健康检查只发现明显异常 |
| `A/spi_link_file_host.h:47、:129、:339` | 保留普通 READY 2 s；HTTPS GROUP_END 35 s、FINISH 120 s，总期限中扣除已用时间。按配套从机等待规则调整轮询与 ACK 匹配。HTTPS 为 4 MHz | QUIET 期间长等待是网络任务在消费组，不宜当作普通 DATA 失联；总期限防止每次 QUERY 重新延时 |
| `A/spi_link_file_host.h:201、:217、:248` | 统一生成源、文件范围源和组补传；显式读取偏移与长度，把组控制和信息回传纳入会话 | 补发须回读同一范围。生成源适配无 SD 的板端验收，文件接口仍依赖实际可读文件 |
| `A/spi_link_file_host.h:287、:303、:540` | 增加扩展请求元数据和 HTTP 结果分页；支持按文件 offset/bytes 发送请求体，结果输出按命令入口处理 | 固件能处理超过单帧容量的元数据和响应。当前调用仍以 Shell 和测试入口为主 |
| `A/spi_link_put_probe.h:38、:49、:91`（新增） | clear/begin/data 分段装入并校验，完成后交给 `spi2putgen`；终止后清掉一次性配置 | 长 URL 及头字段不再挤进一条串口命令，错序或缺字节时在发送前停止 |
| `A/system_app_shell.c:2001` | 注册 `spi2http`、`spi2putfile`、`spi2put`、`spi2putcfg`、`spi2putgen`、`spi2stream` | 分开实际网络、无 SD 生成源与 8 MHz 离线模拟，便于对照 |
| `.gitignore` | 忽略 `/.local/` | PC 私有配置、签名地址、现场证据不随 Git 传递 |

### 3.2 UMA503 固件与构建

| 文件和定位 | 改动及关键行为 | 为什么这样改 / 影响 |
|---|---|---|
| `P/Makefile` | 加入 `third_party/cjson` | 供 OneNET 配置与回执解析 |
| `P/demo/Makefile` | 加入三个 OneNET C 文件及 `spi_link_https.c` 与头路径；本地配置和背压注入均为显式选项 | 普通构建不嵌私有配置，也没有人工停顿 |
| `P/app_main/app_main.c:45` | 启动 `onenet_bootstrap_start()` | 让 OneNET 走独立启动路径，属性接入无需 ATS 串口触发 |
| `P/demo/onenet_bootstrap.h`（新增） | 声明配置结构和共享解析、启动接口 | 启动入口和配置解析按同一字段约定链接 |
| `P/demo/onenet_config.c:7、:85、:131`（新增） | 解析 EPS 注册、对照有效期和配置字段、按 ID 匹配 OneJSON 回执并读取 int code | 支持虚拟 AT 的正文格式，保留错误拒绝和业务错误可观测性 |
| `P/demo/onenet_bootstrap.c:39、:374`（新增） | 组织 LTE、UTC、配置与 MQTT 阶段；优先级 90 → 79 → 原值，使用初始化 guard；生成 13 位请求 ID | 避免接收任务抢在指针写入前执行。若初始化意外中断，重启见 guard 时停驻，保留现场 |
| `P/demo/onenet_downlink.h`（新增） | 声明绑定属性队列和处理入口 | 从 MQTT 回调交给任务处理文件保存与回执 |
| `P/demo/onenet_downlink.c:38、:82、:163`（新增） | 接收 set/get，请求长度与字段校验；临时文件写入、关闭、rename 与读回，发送匹配回复 | 让成功回执与本地镜像一致。属性范围为 bind_status，替换已有文件的各类掉电情况需实测 |
| `P/demo/spi_link_file_protocol.h` | 增补与 ATS 相同的扩展命令、状态和主要长度 | 旧 SPI 环继续工作，新增命令要求配套两端固件 |
| `P/demo/spi_link_https.h:11`（新增） | 定义 TLS 接口、即时结果与完整响应大小 | 512 B SPI 块带不下完整 HTTP 响应，扩展响应堆容量和分页需独立检查 |
| `P/demo/spi_link_https.c:112、:173、:221、:314、:521`（新增） | 校验 URL 和方法、连接、TLS 认证、部分写入、取消、HTTP 响应；保留请求内取消且重复清理安全。诊断包只在正文回调注入 WANT_WRITE | 网络 I/O 有界，状态可回传且防止在清理中丢首错；扩展响应最高 16 KiB。正常模式无注入 |
| `P/demo/spi_link_https_ca.h`、`spi_link_https_digicert_ca.h`、`spi_link_https_uca_ca.h`（均新增） | 分别提供 ISRG Root X1、DigiCert Global Root G2、UCA Global G2 Root 的公开证书 | 按实测链路为 TLS 提供信任根，SNI、域名与日期验证仍启用 |
| `P/third_party/mbedtls/port/asros/asros_mbedtls_config.h:231` | 启用所需 EC、ECDH、ECDSA、ECDHE 套件和 SNI | 支持真实站点 TLS 协商，重新链接相应模块 |
| `P/demo/spi_link_file_slave.h:237、:498、:513、:628` | 加入 worker 交接、元数据收集、消费结果与响应分页；只释放实际分配的内存 | 防止组缓冲在网络未结束时被覆盖，修复本 SDK NULL free 断言；失败后通过协议返回 |
| `P/demo/spi_link_dma_circular.h:339` | `lc_https_quiet()` 在完整双槽边界暂缓 READY，组上限 30 s、FINISH 115 s，期间复查边界。清理前保存 FIRST_FAULT | 将网络等待与 SPI 许可绑定，保留出现意外时钟时的首错；与 ATS 期限一起变更 |
| `.gitignore` | 忽略 `/.local/` 及 cJSON build 目录 | 保留源码，构建中间文件和本地配置不跟踪 |

### 3.3 PC 工具、模型与测试

| 仓库和文件 | 改动及使用理由 |
|---|---|
| ATS `tools/onenet_device.py` | 生成本地短期配置并验证 MQTT 连接和属性回执。先隔离凭据和平台，再测 503 SDK |
| ATS `tools/p3_put_probe.py` | 生成确定性字节，构造分段命令或解析响应。复用同一规则比对服务器回下文件 |
| ATS `tools/p3_serial_send.py` | 校验命令文件，要求显式 baud，逐行等严格回执并核对 PUT 结果。发送失败立即停在相应阶段 |
| ATS `tools/lifeiot_direct.py` | 业务 init/refresh、调用串口 PUT、同会话预检查、complete 与状态核对，保留私有证据目录 |
| ATS `tests/recording_multipart_p4.py` | PC 参考状态模型，覆盖后端范围读取与请求恢复设计，供 P4 后续接板对照 |
| ATS `tests/test_spi_circular_host_v11.py` | 已有 ARM 嵌入模型扩展到 TRNG、真实时钟分频、HTTP 元数据、响应、QUIET 及错帧边界 |
| ATS `tests/test_lifeiot_direct.py` | 会话、签名输入、已同步短路及 complete 核验 |
| ATS `tests/test_onenet_device.py` | 短期配置、字段、MQTT 数据包与回执匹配 |
| ATS `tests/test_p3_put_probe.py` | 元数据、确定性正文和输出解析 |
| ATS `tests/test_p3_serial_send.py` | 短行、错误回执、结果校验及只装载路径 |
| ATS `tests/test_recording_multipart_p4.py` | 多片、403 刷新、PUT/complete 结果不确定的模型 |
| UMA `tests/test_spi_circular_v11.py` | 已有 ARM 模型覆盖 worker、NULL 释放、首错、QUIET 与恢复 |
| UMA `tests/test_spi_https_cleanup.py` | 执行实际 HTTPS C 的 ARM 模型，核对清理、取消等平台语义 |
| UMA `tests/test_mqtt_vendor_init.py` | 使用调度模型对照 90 被接收任务抢占、79 先完成指针发布两种情况 |
| UMA `tests/test_onenet_config.py` | 覆盖虚拟 AT、配置、有效期及 OneJSON 回执 |
| UMA `tests/spi_https_put_receiver.py` | 带 TLS 的可控接收器，支持分段读、慢读和非 2xx 响应 |
| UMA `tests/test_spi_https_put_receiver.py` | 本地验证接收器的内容检查及失败路径 |
| UMA `tests/build_onenet_no_sd.py` | 从本地配置生成私有测试包，按需构建背压版并清理临时配置，避免把短期凭据固化为普通构建默认输入 |

### 3.4 文档与暂存范围

- ATS 的《OneNET 及 HTTPS 上传调试复盘》已归纳十份阶段原稿和后续 P4 复核。十份是原先未跟踪的草稿，从工作区清走后不会产生 Git 删除项。仓库外保留原稿 ZIP。
- ATS 旧《SPI 提速与实机调试复盘》和《代码修改与Git提交说明模板》也有未暂存差异。可按各自用途单独审阅并提交，避免挤进固件功能提交。
- UMA 的本轮 10 份未跟踪验收稿逐份核对后已归档。归档路径及去向见下方清单。已跟踪的 V11/V11A 证据、SPI 历史复盘与交接文档沿用原路径。

### 3.5 关键行为的前后对照

| 项目 | 旧路径或故障版 | 当前路径 | 代价和依赖 |
|---|---|---|---|
| 组等候 | P1 BUSY 后约 20 ms 继续 QUERY；HTTPS 曾在网络作业期仍按普通 READY 等待 | 503 完整双槽后暂停 READY；ATS 只对已开始的 HTTPS 控制操作使用剩余预算 | 无新许可时主机不打时钟，网络消费者需在限定时间内归还缓冲 |
| HTTPS SCK | 问题诊断版请求 1 MHz，低于 HOSC 可用档 | 4,000,000 Hz /8 可达 | 普通 4 MHz 的实机证据不外推到 8 MHz HTTPS |
| 服务器信任 | 实际对象根不在信任链，验证 NOT_TRUSTED | 保留原根并追加公开 UCA Global G2 Root | 固件增加证书与 TLS 支持体积，保留证书检查 |
| 控制台装载 | 长命令与错误的默认 115200 使请求未装入 | 逐短行确认，显式 2 Mbaud，装载完成后一次 PUT | 必须独占串口，脚本需匹配工作终端波特率 |
| 同步完成 | PUT 返回 200 后曾停在 SYNCING | 重新 init 核对会话和连续覆盖，再由 PC complete | 平台进度未知时需要重新查询，不能把重复提交或丢失回包当作成功 |

### 3.6 本轮拟提交的新增文件逐项说明

依据两仓库的未跟踪文件清单核对。ATS 有 14 份文件，含 12 份源码、工具及测试和 2 份文档。UMA 有 16 份拟纳入的源码、构建工具及测试。既有文件修改见第 3.1–3.5 节。

#### ATS3085E（14 份）

| 路径 | 新增原因及使用入口 | 提交处理 |
|---|---|---|
| `TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main/spi_link_entropy.h` | 主机 sfile_run() 准备 HTTPS 会话时调用，提供有界的 48 B 硬件种子，补上 HCI 随机源崩溃后的替代路径 | 随 ATS 固件提交 |
| `TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main/spi_link_put_probe.h` | 提供 spi2putcfg 与 spi2putgen，将长请求分多条 Shell 命令装入并在生成数据测试中一次性消费 | 随 ATS 固件提交 |
| `tools/onenet_device.py` | PC 生成短期配置和最小 MQTT 探针，用于在烧录前隔离平台身份与属性回执 | 随工具提交 |
| `tools/lifeiot_direct.py` | PC 驱动 init、串口 PUT、进度复查和 complete，供真实后端 P3 验收 | 随工具提交 |
| `tools/p3_put_probe.py` | 生成可独立复算的正文、短行元数据命令与响应解码，供上传脚本和取回验收复用 | 随工具提交 |
| `tools/p3_serial_send.py` | 逐行等待装载回执，严格核对串口上传结果，可独立执行，也由 lifeiot_direct.py 的 upload 流程调用 | 随工具提交 |
| `tests/test_onenet_device.py` | 验证本地配置、MQTT 编解码和匹配回执，配合 OneNET 工具防回归 | 随工具提交 |
| `tests/test_lifeiot_direct.py` | 验证后端业务请求、同会话完整性、complete 短路和结果检查 | 随工具提交 |
| `tests/test_p3_put_probe.py` | 覆盖生成图案、元数据边界与响应重组，防止工具生成板端不能使用的命令 | 随工具提交 |
| `tests/test_p3_serial_send.py` | 覆盖缺失或错误回执、只装载和结果校验，防止还没装完就 PUT | 随工具提交 |
| `tests/recording_multipart_p4.py` | 可运行的 PC 参考模型，验证待传片区间、重查及 complete 决策 | 可单独提交为 P4 实验 |
| `tests/test_recording_multipart_p4.py` | 为参考模型验证多片、403 刷新及响应丢失重查 | 随 P4 实验提交 |
| `docs/ATS3085E与UMA503_OneNET及HTTPS上传调试复盘_20261119-23.md` | 交接文档，收录双端排障、包身份、复测流程与 P4 边界 | 随文档提交 |
| `docs/Git提交说明_ATS3085E_UMA503_OneNET与HTTPS_20261123.md` | 双端文件清单、证据、回退及可复制七段正文 | 随文档提交 |

#### UMA503-V7（16 份）

| 路径 | 新增原因及使用入口 | 提交处理 |
|---|---|---|
| `startup/app_release/demo/onenet_bootstrap.c` | app_main_entry() 启动任务的实现，处理 LTE、UTC、MQTT 初始化保护及 OneJSON 业务回执 | 随 UMA OneNET 提交 |
| `startup/app_release/demo/onenet_bootstrap.h` | 导出启动、配置结构及解析接口，供启动实现、配置实现和模型共用 | 随 UMA OneNET 提交 |
| `startup/app_release/demo/onenet_config.c` | 从启动任务调用，处理虚拟 AT、配置有效期、OneJSON 回执，避免凭据或未匹配回复进入后续阶段 | 随 UMA OneNET 提交 |
| `startup/app_release/demo/onenet_downlink.c` | 实现绑定属性请求入队、set/get、本地镜像与匹配回执，避免在 MQTT 回调内直接用文件系统 | 随 UMA OneNET 提交 |
| `startup/app_release/demo/onenet_downlink.h` | 声明队列、初始化和处理边界，供 bootstrap 连接回调与任务 | 随 UMA OneNET 提交 |
| `startup/app_release/demo/spi_link_https.c` | 为 SPI 分组 worker 提供 DNS/TCP/TLS、流式写、取消、响应和释放；含默认关闭的诊断注入 | 随 UMA HTTPS 提交 |
| `startup/app_release/demo/spi_link_https.h` | 向 SPI 文件接收路径导出网络调用、容量和结果结构 | 随 UMA HTTPS 提交 |
| `startup/app_release/demo/spi_link_https_ca.h` | 供 HTTPS 信任链装载 ISRG Root X1 公共证书 | 随 UMA HTTPS 提交 |
| `startup/app_release/demo/spi_link_https_digicert_ca.h` | 供 HTTPS 信任链装载 DigiCert Global Root G2，覆盖业务入口对应链 | 随 UMA HTTPS 提交 |
| `startup/app_release/demo/spi_link_https_uca_ca.h` | 供 HTTPS 信任链装载 UCA Global G2 Root，修复对象存储 NOT_TRUSTED | 随 UMA HTTPS 提交 |
| `tests/build_onenet_no_sd.py` | 按显式本地配置构建私有免 SD 包，可切换背压注入；提交的是构建脚本 | 随 UMA OneNET/构建工具提交 |
| `tests/spi_https_put_receiver.py` | 带 TLS 的控制端，提供慢读、完整内容校验和非 2xx，供复现网络端问题 | 随 UMA HTTPS 提交 |
| `tests/test_spi_https_put_receiver.py` | 配套检查控制端读写和返回路径 | 随 UMA HTTPS 提交 |
| `tests/test_spi_https_cleanup.py` | 编译执行生产 C，验证释放、取消、部分写和注入保持 | 随 UMA HTTPS 提交 |
| `tests/test_mqtt_vendor_init.py` | 用实际厂商 ARM 指令模型对照优先级引发的初始化竞态 | 随 UMA OneNET 提交 |
| `tests/test_onenet_config.py` | 覆盖 LTE、UTC、配置及属性回执的生产逻辑与关联保护 | 随 UMA OneNET 提交 |

#### UMA503 归档的阶段 Markdown（10 份）

以下原件逐份参考并收录到实机复盘，按原名保存在仓库外的 `D:/ats3085test_备份/P3/SPI阶段文档原稿_20261116-19/UMA503`。此批原件本来未跟踪，归档后没有 Git 删除项。

| 路径 | 新增原因及使用入口 | 提交处理 |
|---|---|---|
| `docs/ONENET_NO_SD_DIAG1_20261120.md` | 开机 PP 日志的证据边界，补查注册、UTC、返回码与 IDLE。对照复盘第 4 章案例 02 | 已归档 |
| `docs/ONENET_NETINIT2_RESULT_20261120.md` | 补网络初始化三步。COM5 只读 AT 与 COM6 的采样时点不同，历史 ARM 10 项。案例 02 | 已归档 |
| `docs/ONENET_LTE3_RESULT_20261120.md` | voice=1/data=0 与直接 CEREG/CGATT/CGACT 对照，历史 ARM 12 项。案例 02–03 | 已归档 |
| `docs/ONENET_LTE4_RESULT_20261120.md` | 内部虚拟 AT 的 11 B 正文省略 OK，保留 API 返回码检查，历史 ARM 13 项。案例 03 | 已归档 |
| `docs/ONENET_MQTT5_RESULT_20261120.md` | 厂商 ARM 反汇编和调度模型，优先级 79 与 guard；历史 19 项生产模型及 2 项厂商指令模型。案例 04 | 已归档 |
| `docs/ONENET_MQTT6_RESULT_20261120.md` | 13 位 ID/2405 与 MQTT ABI 对照，历史 25 项。案例 05 | 已归档 |
| `docs/ONENET_MQTT7_RESULT_20261120.md` | 实机 bind_status 下发、全属性读取限制与历史 34 项模型。案例 06 | 已归档 |
| `docs/LIFEIOT_TLS_DIRECT_20261120.md` | 业务入口 DigiCert 链、PC TLS 对照和 401/A0002 的阶段证据。案例 07、13 | 已归档 |
| `docs/ONENET_RENEWAL_20261123.md` | 11 月 23 日私有配置续期和包差异。复核的是构建与包，续期时未另做实时 MQTT 探测。案例 06 | 已归档 |
| `docs/P3_NULL_FREE_FIX_20261123.md` | 16 次同类 NULL 释放断言、修复位置和 38+4 项历史模型。案例 08 | 已归档 |

<a id="checks"></a>

## 4. 验证命令、结果和版本

本次在 ATS 仓库复跑下面五个离线模块，实际顺序结果为 **14、6、5、9、10 项通过，共 44 项**。运行环境为项目本机 Python。

~~~powershell
foreach ($test in @('test_lifeiot_direct.py','test_p3_serial_send.py','test_p3_put_probe.py','test_recording_multipart_p4.py','test_onenet_device.py')) {
    python -m unittest discover -s tests -p $test
    if ($LASTEXITCODE -ne 0) { throw "Failed: $test" }
}
~~~

实机复现先按配套版本烧录，开启 COM6，关闭串口助手的 COM4，使用授权本地身份和签名文件。普通包使用 48,001 B，注入包用 144,001 B 及独立会话目录。两个变量分别为测试设备标识和本地业务签名文件路径。

~~~powershell
python tools/lifeiot_direct.py upload --imei $DeviceId --bytes 48001 --directory .local/p3-normal-recheck --secret-file $BusinessKeyFile --port COM4 --baud 2000000
~~~

| 对照项 | 改前或首错记录 | 已有修复证据 |
|---|---|---|
| 时钟配置 | 1 MHz 请求在 SPI open 返回 -34，零笔 | QUIET 组合实测 4 MHz，正常和注入均交付 |
| 从机和网络 | 13:56 一轮主机 117 笔、从机完成 115 笔，SPI -216 触发取消 | 普通 127 笔两端闭合，注入 339 笔两端闭合 |
| 信任链 | 14:49 DNS/TCP 成功，TLS -9984 / 0x08 | 15:20 普通 PUT HTTP 200，下载 48,001 B CRC32 887489ae |
| 注入与业务结果 | 16:23 尝试时从机在命令前已挂起，未进入注入 | 16:35 四组注入成功，group_wait 24,151 ms，总耗时 24,705 ms。电脑 complete 成功，下载 144,001 B CRC32 23122b0b |

历史验证按对应产物解读。TRNG 修复 33 项 ARM、4 MHz 诊断 34 项、QUIET 主机 35 项。UMA NULL free 阶段 38 项 SPI 与 4 项生产 HTTPS ARM 测试通过；FIRST_FAULT 阶段另有 8 项 HTTPS ARM 记录，不能相加当作本次结果。QUIET ATS 历史链接 SRAM 为 **932,272 / 932,864 B**，余 592 B。503 用当次 ELF/map 与包内 app 一致性判定，本次未重新测量最新工作区的固件占用。下载与包哈希、串口文件名和事务号见[复盘证据索引](ATS3085E与UMA503_OneNET及HTTPS上传调试复盘_20261119-23.md#evidence)。

<a id="limits"></a>

## 5. 边界

- 真机已证明普通 HTTPS PUT，及板端模拟 WANT_WRITE 时组缓冲保持、等待恢复和电脑 complete。真实服务器慢读仍待实测，实际网络时延和工作负载变化需分别验收。
- OneNET 属性登录和本轮绑定下发属于设备侧能力。重启后恢复绑定状态、属性全集、3085E 界面及录音联动需要独立证据。
- 已确认部分帧先于网络取消，接线/总线/复位窗口中的物理触发还需同步波形。TRNG 异常不是随机质量认证，模型不能替代硬件验证。
- P4 目前有 PC 参考模型和辅助工具。板端自动调用业务 init/complete、录音源回读、持续写入下的响应容量和掉电恢复均为后续工作。

<a id="rollback"></a>

## 6. 遗留、影响和回退

- 常规验收使用不带注入的 UMA 包；诊断包用于复现持组与恢复。保留 FIRST_FAULT、HTTP 阶段及会话状态日志，现场按生成数据、响应和独立下载对账。
- 单独回退 OneNET 时核对 `app_main.c`、`demo/Makefile` 和顶层 cJSON 依赖是否还被其他提交使用。OneNET 私有配置在本地重新生成，提交不携带有效期数据。
- 回退 HTTPS 时从成对已知基线 ATS `46d8250` 与 UMA `44138c0` 取出对应代码或固件并共同重启。该组合用于验证旧 P1 分组交付。中间单端版本与其配套固件以复盘时序为准。对已共享提交使用成对 revert，保留既有工作区内容。
- 网络状态不确定时先查同一会话，再判断需要补片还是 complete。板子已 STOP 时先保存双端首错，然后恢复 SPI 基线。

<a id="split"></a>

## 7. 提交拆分与顺序

起点为 ATS `46d8250`、UMA `44138c0`。建议按下表拆分，提交后在配套说明中补上实际提交号。两端 HTTPS 协议与 QUIET 联动需要成对构建和上板。

| 顺序 | 仓库 | 建议主题 | 边界 |
|---|---|---|---|
| 1 | UMA503 | `fix(onenet): 修复 LTE 注册与 MQTT 启动并接入绑定属性` | MQTT 生命周期、配置入口和属性镜像 |
| 2 | UMA503 | `feat(spi): 通过 HTTPS 消费分组并在网络等待期间保持 READY 静默` | SPI/worker、TLS、公用 CA、失败快照与回归 |
| 3 | ATS3085E | `feat(spi): 增加 4 MHz HTTPS PUT 探针与分组响应回传` | 与 UMA 同版本的协议、TRNG、期限和主机测试 |
| 4 | ATS3085E | `feat(tools): 增加 OneNET 探针与 lifeiot 上传验收工具` | PC 工具和测试，P4 状态模型按需要另成一笔 |
| 5 | ATS3085E | `docs: 归档 OneNET 与 HTTPS 上传实机调试复盘` | 新复盘与本说明。已有 SPI 复盘和模板的工作区改动单独审阅 |

UMA 的两个构建入口若包含交叉改动，按 diff 分块暂存并验证各提交可构建。若交叉依赖难以拆开，可合并前两笔，并在正文中分列两条链路。公开根证书随代码提交，设备配置注入与本地数据分开保存。

<a id="messages"></a>

## 8. 可复制的七段提交正文

### UMA503

~~~text
feat(onenet,spi): 接入绑定属性与分组 HTTPS PUT

一、问题定位
- EPS 已注册时旧 dataReg 门槛仍停驻，内部虚拟 AT 回包只含 CEREG 正文。
- SDK MQTT 初始化创建接收任务早于客户端指针就绪，偶发 DataAbort。
- OneJSON 旧请求 ID 格式和三位 code 解析使平台拒绝被观察成超时。
- P3 在 SPI 错误清理中触发空指针释放。清理和网络取消还覆盖了首错现场。
- 正文网络工作与后续 QUERY 重叠曾遇到 SPI 部分帧。对象存储 TLS 另缺 UCA 根。

二、分文件改动
- Makefile、app_main/app_main.c、demo/Makefile 接入 OneNET 构建入口与启动任务。
- demo/onenet_bootstrap.c/.h 处理 LTE、UTC、MQTT 阶段、初始化优先级和 guard。
- demo/onenet_config.c 校验配置。demo/onenet_downlink.c/.h 处理绑定属性与本地镜像。
- demo/spi_link_file_protocol.h 和 spi_link_file_slave.h 新增上传及 HTTP 响应控制，保持组缓冲交付与受控清理。
- demo/spi_link_dma_circular.h 在完整双槽边界静默等待 worker，保存清理前 FIRST_FAULT。
- demo/spi_link_https.c/.h 实现流式 PUT、DNS/TCP/TLS 诊断、取消、超时和响应收尾。
- HTTPS CA 头及 mbedTLS 平台配置补齐受验证的根和套件。保留 SNI、主机名及时间验证。
- tests/ 更新真实 ARM 清理、串流及构建检查。背压注入默认关闭。
- 主机配套基线 ATS 46d8250 的后续同批改动，需同时具备扩展命令和 QUIET 期限。

三、验证命令
- 配 ATS QUIET 4 MHz，COM4 2,000,000 / 8N1，COM6 保存完整启动与应用日志。
- 按复盘第 4 章运行 lifeiot_direct.py upload，先 48001 B 正常，再 144001 B 专用注入包。
- 从同一设备读 OneNET 单项 bind_status，同时核对响应 ID 与 code。

四、验证结果
- 板端 MQTT 登录、订阅与测试属性 code=200，绑定属性 0 下发与保存有实机证据。
- 普通 PUT 两组、127 笔、HTTP 200；注入四组、339 笔、guard_ok=1、HTTP 200。
- 两份服务器下载分别 48001 B、144001 B，均与生成内容一致；PC complete 成功。

五、未证明与已知限制
- 真实后端慢收未验收。板端注入的等待与恢复已有独立证据。
- 复位前部分帧触发点仍缺波形。属性全集、板端自动 P4、真实录音及断点恢复需另测。

六、遗留与回滚
- 保留分层错误与 FIRST_FAULT。常规试验使用 P3_NORMAL 包。
- 代码回退以 44138c0 为配套起点，同时回退 ATS 至 46d8250 并重启两端。

七、硬件/环境动作
- 两板共地，核对 MOSI 接 503 PCM_DIN 和 1.8 V READY。记录重焊前后差异。
- 插入 SIM 后上电。短期 OneNET 配置与业务签名分别保存在授权的本地环境。
~~~

### ATS3085E

~~~text
feat(spi): 增加 4 MHz HTTPS PUT 探针和 PC 端验收

一、问题定位
- 蓝牙 LE Rand 超时后 shell Usage Fault，SPI 尚未发包。
- 1 MHz 低于 SPI2 当前 HOSC 分频下限，open 返回 ERANGE（transfers=0）。
- 固定 READY 期限和串口默认波特率不符合实际上传环境。
- 对象 PUT 之后还需业务 complete，且需核对服务端下载正文。

二、分文件改动
- spi_link_entropy.h 使用安全引擎 TRNG，并在配置 SPI 前检查种子结果。
- spi_link_put_probe.h、system_app_shell.c 支持短行装入元数据和生成数据测试。
- spi_link_file_protocol.h、spi_link_file_host.h 增加上传事务和 HTTP 响应分页。
- HTTPS 选 4 MHz。GROUP_END/FINISH 使用剩余总期限，普通 DATA 仍按 2 s READY。
- tools/ 增加 OneNET 探针、lifeiot 会话、串口传输和本地接收器。P4 辅助状态模型仅用于 PC 测试。
- tests/ 覆盖生产时钟代码、TRNG、QUIET、串口应答、会话与重复 complete 防护。
- .gitignore 排除 .local/，原始命令、会话及凭据由本地保管。
- 从机配套基线 UMA 44138c0 的后续同批改动，需同时具备 HTTPS worker、扩展命令和 QUIET。

三、验证命令
- python -m unittest discover -s tests -p 'test_lifeiot_direct.py'
- 另以同样命令执行 test_p3_serial_send.py、test_p3_put_probe.py、
  test_recording_multipart_p4.py、test_onenet_device.py。
- 配 UMA QUIET 兼容固件执行复盘第 4 章的 upload，COM4 明确指定 2000000。

四、验证结果
- 本次五套 PC 测试共 44 项通过。历史 ARM 与编译结果见分阶段复盘，QUIET 的 ATS SRAM 为 932272/932864 B。
- 实机 48001/144001 B 均收到 HTTP 200，分别 CRC32 887489ae/23122b0b，下载比对成功。
- 注入轮 transfers=339、resent=0，电脑 complete 返回 SYNC_SUCCESS。

五、未证明与已知限制
- 真实服务器慢收和 P4 脱离 PC 仍待验收。部分帧机理未由日志单独确定。

六、遗留与回滚
- 保留错误阶段及会话证据。停止于首个失败，按原会话状态判断是否需恢复。
- 回退至 46d8250 时需配 UMA 44138c0；不要混刷 QUIET 与早期固定查询固件。

七、硬件/环境动作
- 使用匹配烧录包，复位两端。关闭 COM4 串口助手后再让 pyserial 连接。
- 不随提交携带业务密钥、OneNET 私有配置或签名 URL。
~~~

<a id="review"></a>

## 9. 暂存与交付核对

对两仓库分别执行 `git status --short --untracked-files=all`、`git diff --cached --name-status` 和 `git diff --cached --check`。明确暂存预期的代码、测试和文档，不用整个工作区作为暂存范围。额外查看构建生成的配置头、`.local/`、签名命令、带凭据的固件及原始日志。设备标识和私有参数不放进复盘或提交消息。

引用的主机和从机测试包、现象及验证边界见[网络上传调试复盘](ATS3085E与UMA503_OneNET及HTTPS上传调试复盘_20261119-23.md)。这份说明供复制和拆分，实际提交以双方工作区核对后的暂存内容为准。
