# UMA503-V7 接入 ATS3085E — 全程 BUG 复盘

> 范围：从「杜邦线第一次接到模组焊盘」到「`app at2tx` 收到 OK」再到「HTTP GET 命令就绪」。
> 每一个 BUG 都按同一模板记录：**现象 → 根因 → 排查步骤 → 改动 → 为什么改 → 验证判据**。

---

## 目录

- [实测成功的最终配置](#实测成功的最终配置)
- [阶段总览](#阶段总览)
- [阶段一：接线前必须先释放两个引脚](#阶段一接线前必须先释放两个引脚)
  - [BUG-01 GPIO53 输出低只有 0V —— 外置功放抢脚](#bug-01-gpio53-输出低只有-0v--外置功放抢脚)
  - [BUG-02 GPIO54 被加速度计 INT1 硬顶](#bug-02-gpio54-被加速度计-int1-硬顶)
- [阶段二：UART2 通路建立](#阶段二uart2-通路建立)
  - [BUG-03 编译命令用错，build.cmd 带参数报错](#bug-03-编译命令用错buildcmd-带参数报错)
  - [BUG-04 自环通、接模组 0 字节](#bug-04-自环通接模组-0-字节)
  - [BUG-05 at2tx 只收到 2 字节就退出](#bug-05-at2tx-只收到-2-字节就退出)
- [阶段三：核心 —— TX 方向永久 0 字节](#阶段三核心--tx-方向永久-0-字节)
  - [BUG-06 误判：收到 URC 就当链路通了](#bug-06-误判收到-urc-就当链路通了)
  - [BUG-07 误判：DTR 没接地](#bug-07-误判dtr-没接地)
  - [BUG-08 误判：波特率被锁死](#bug-08-误判波特率被锁死)
  - [BUG-09 误判：模组进了 PSM 睡眠](#bug-09-误判模组进了-psm-睡眠)
  - [BUG-10 误判：PWRKEY 按太短](#bug-10-误判pwrkey-按太短)
  - [BUG-11 PWRKEY 直连 GND —— 一次阶段性尝试（已回退）](#bug-11-pwrkey-直连-gnd--一次阶段性尝试已回退)
  - [BUG-12 假阳性：模组关机时测到 0V](#bug-12-假阳性模组关机时测到-0v)
  - [BUG-13 PADDRV 驱动档位不足（4 → 7）—— TX 方向的真正根因](#bug-13-paddrv-驱动档位不足4--7--tx-方向的真正根因)
- [阶段四：shell 参数与手册陷阱](#阶段四shell-参数与手册陷阱)
  - [BUG-14 Zephyr shell 把 ? 当 glob 通配符吃掉](#bug-14-zephyr-shell-把--当-glob-通配符吃掉)
  - [BUG-15 官方手册两个参数名印错](#bug-15-官方手册两个参数名印错)
- [**第五章：代码改动逐行清单（按重写顺序）**](#第五章代码改动逐行清单按重写顺序)
  - [5.0 提交注释](#50-提交注释本章开头的总览注释)
  - [5.1 全景：6 个文件 + 1 个硬件动作](#51-全景一共-6-个文件--1-个硬件动作)
  - [第 1 步 board_cfg.h —— 让 UART2 存在](#第-1-步board_cfgh--让-uart2-存在3-行)
  - [第 2 步 defconfig —— 腾出 GPIO53](#第-2-步ats3085e_dev_watch_ext_nor_defconfig--腾出-gpio531-行)
  - [第 3 步 board.h —— 软件层不再占 GPIO54](#第-3-步boardh--软件层不再占-gpio541-行)
  - [第 4 步 sensor_acc_qma6100.c —— 让芯片闭嘴](#第-4-步sensor_acc_qma6100c--让芯片闭嘴8-行后被拆芯片取代)
  - [第 5 步 board.c —— 配引脚 + 拉满驱动](#第-5-步boardc--配引脚--拉满驱动8-行其中-1-行是决定性改动)
  - [第 6 步 system_app_shell.c —— 加验证命令](#第-6-步system_app_shellc--加验证命令)
  - [**5.7 实测：HTTP / HTTPS 双通（六步全部走通）**](#57-实测http--https-双通六步全部走通)
  - [5.8 基础知识：PADDRV 到底是什么](#58-本章末尾基础知识--paddrv-到底是什么)
  - [5.9 实测：AT+HTTPGETEX 分段 GET（206 通过，DMA 可以不做）](#59-实测athttpgetex-分段-get206-通过dma-可以不做)
  - [5.10 实测：app at2get 正式循环命令（30 段取全 17143 字节）](#510-实测app-at2get-正式循环命令30-段取全-17143-字节三段踩坑才真完整)
- [第六章：能力边界查证（SPI 与 HTTP 指令集）](#第六章能力边界查证spi-与-http-指令集)
  - [6.1 UMA503 能不能给一组完整 SPI（否证）](#61-uma503-能不能给一组完整-spi否证)
  - [6.2 HTTP(S) 指令集 9 条对照](#62-https-指令集-9-条对照)
  - [6.3 HTTP POST 与 MQTT 是两条独立通道](#63-http-post-与-mqtt-是两条独立通道)
  - [6.4 由此得出的路线判断](#64-由此得出的路线判断)
- [附录 A：全部代码改动清单](#附录-a全部代码改动清单)
- [附录 B：寄存器与引脚速查](#附录-b寄存器与引脚速查)
- [附录 C：可复用的排查判据](#附录-c可复用的排查判据)
- [附录 D：还没做的事](#附录-d还没做的事)

---

## 实测成功的最终配置

> 这是全文的锚点。所有 BUG 排查完之后，真正跑通的状态是这样的。**先看这个，再看有哪些坑。**

### 接线：三根线

| 3085E 侧 | 模组焊盘 | 备注 |
|---|---|---|
| **GPIO53**（PADDRV=7） | `UART_RXD`（行 8 左） | 只接这一根 TX |
| **GPIO54** | `UART_TXD`（行 8 右） | 只接这一根 RX |
| **GND** | `GND`（行 2 任一） | 共地 |

**没接的引脚**

- `UART_DTR`：**已拆除，悬空**
- `UART_CTS` / `UART_RTS`：悬空（硬件流控默认关闭，无影响）
- `PWRKEY`：**没动，保持原厂状态**

**这正是手册 P26 图 12 的标准三线制：** 只用 TXD / RXD / GND 三根，没有任何握手线。

### 硬件：U13（QMA6100P）已拆除

**这是最终解决问题时最关键的一步。** 加速度计芯片 U13 已从板上物理拆下，GPIO54 上不再有任何外部芯片负载。

代价：**加速度计功能永久失效**（抬腕亮屏、计步等依赖 ACC 的功能不再工作）。

### 两个必需条件（缺一不可）

这套链路最终打通，靠的是**两个独立的修复动作**，各自负责一个方向：

| # | 动作 | 修的是 | 缺了会怎样 |
|---|---|---|---|
| 1 | **物理拆除 U13（QMA6100P）** | **RX（54 侧）** | 自环都过不去，"收"也做不到 |
| 2 | **PADDRV 4 → 7**（`0x4127` → `0x7127`） | **TX（53 侧）** | 能收不能发，模组收不到 AT |

**中间状态（这一段能帮你看懂后面所有内容）**

```
只拆 U13        → 自环通过，接 503 只能收、发不出去
只改 PADDRV     → 54 上还挂着芯片，自环都不通
两个都做        → AT 一问一答，11/11 全 OK
```

**以后遇到类似情况，先分清是"收不到"还是"发不出"：**

- **收不到** → 问题在 **RX 侧**（对端 TXD → 本端 RXD），查本端 RX 脚有没有被别的东西占着
- **发不出** → 问题在 **TX 侧**（本端 TXD → 对端 RXD），查本端 TX 脚的驱动够不够强

### 供电：USB

**模组板 USB 插入供电**（就是 CH342 那个口），**不接外部稳压电源**。

### 模组状态

| 项目 | 值 |
|---|---|
| 开机方式 | USB 插入后，板上自动开机电路上电（不需要按 PWRKEY） |
| 低功耗 | `AT+UMSCLK=0` 已关闭 |
| 波特率 | 115200 / 8N1 |

### 实测结果

**第一层：AT 指令一问一答**

```
app at2tx AT  →  0D 0A 4F 4B     连发 11 次，11/11 成功
AT+CSQ    → +CSQ: 27,99           信号 27/31
AT+CGPADDR → IPv4 = 10.38.183.59  已拿到 IP
AT+CGATT? → +CGATT: 1             已附着
```

**第二层：HTTP GET 六步全通（固件 `261112.fw` 14:12:22 版）**

```
app at2http http://www.baidu.com/
  1/6 requestheader  GOT      2/6 rspout/auto  GOT
  3/6 url-length     CONNECT  4/6 url-body    OK
  5/6 http-get       GOT +HTTPGET → err=0 http=200 body=17143
  6/6 http-read      RX 598 BUF-FULL
  TEXT: CONNECT <!DOCTYPE html> ...
-> HTTP 200 通过
```

**这是整条链路第一次真正跑通的验收结论** —— MCU 经 UART2、模组、基站、到百度服务器再收回来，全部走完。

**第三层：HTTPS 返回与 HTTP 完全相同 → 但不能当作 TLS 已可用**

```
app at2http https://www.baidu.com/  → err=0 http=200 body=17143（与 http 版一模一样）
```

**这个"一模一样"本身就是疑点**：`https` 和 `http` 得到完全相同的状态码和正文长度，`sslctxid` 实测又是 0，很可能是模组**剥掉 scheme、按 80 端口走 HTTP**。**"是否真加密"未查证。**

**读这一节时必须注意的三点**

1. **U13 已拆除**，54 上没有任何外部芯片负载 —— 这是"链路通"的前提条件之一
2. **`at2lv` 量到的 0.1V 是 GPIO 输出模式下测的**，而 UART 真正工作时 53 是 MFP=7 模式。**两种模式驱动级可能不同**，所以那个 0.1V 不能直接当作"UART 模式下模组收到了低电平"的证据（详见 BUG-13 的归因说明）
3. **`body=17143` 与 `RX 598` 不相等是正常的** —— 缓冲只有 600 字节，收满即停，剩下 16545 字节还在模组里。**这不代表数据丢了**，只是 MCU 没去拿

---

## 阶段总览

| 阶段 | 目标 | 遇到的 BUG | 最终结论 |
|---|---|---|---|
| 一 | 把 53/54 从别的功能手里抢回来 | BUG-01、BUG-02 | 关外置 PA + **物理拆除 U13** |
| 二 | UART2 能发能收 | BUG-03、BUG-04、BUG-05 | 通路建立，自环通过 |
| 三 | 让模组真的收到 AT | BUG-06 ~ BUG-13（**8 个连环误判**） | PADDRV 4 → 7 |
| 四 | 让查询命令能用 | BUG-14、BUG-15 | `#` 代写 `?` |

**最耗时间的是阶段三。8 个误判里有 7 个是"排除已有假设"，只有第 8 个才是真因。**

---

## 阶段一：接线前必须先释放两个引脚

这两个问题不解决，线接上去也没用——因为 53/54 已经被别的子系统占着。

### BUG-01 GPIO53 输出低只有 0V —— 外置功放抢脚

**现象**

`app at2out` 命令让 GPIO53 输出低电平，万用表量 53 号脚，读数是 **0V**，看起来"驱动正常"。但后来发现对端模组收不到。

更早的表现是：改代码前，53 号脚电压恒为 **0V**，完全不受控制。

**根因**

`GPIO53` 同时被两个功能使用：

1. `UART2_TX`（我们要的）
2. **外置功放使能脚 EXTERNAL_PA_EN**

链路是这样的：

```
defconfig:199  CONFIG_AUDIO_POWERON_OPEN_PA=y
defconfig:200  CONFIG_BOARD_EXTERNAL_PA_ENABLE=y
        ↓
audio_out_acts.c:1742-1750  调用 board_extern_pa_ctl()
        ↓
board.c:427-428  把 GPIO53 配成 GPIO 输出，并拉低
        ↓
UART2 的 MFP 复用被抹掉，53 变成普通 GPIO 且恒低
```

**排查步骤**

1. 先怀疑"焊锡短路" → 蜂鸣挡量 53 对 GND，不通 → 排除
2. 查 `pinctrl_leopard.h`，确认 UART2 在 53/54 只有一组，没有备选脚
3. 全局搜索 `GPIO_53` 的所有使用者 → 发现 `board_extern_pa_ctl`
4. 顺着调用链倒查 `defconfig` 的两个 PA 宏
5. 把 `CONFIG_BOARD_EXTERNAL_PA_ENABLE` 改成 `=n`（只关"外置 PA 使能"，保留 `CONFIG_AUDIO_POWERON_OPEN_PA=y`）
6. 编译烧入 → 53 从 0V 变 1.8V（空闲高），喇叭开机音效仍在

**改动**

`zephyr/boards/arm/ats3085e_dev_watch_ext_nor/ats3085e_dev_watch_ext_nor_defconfig`

```diff
  CONFIG_AUDIO_POWERON_OPEN_PA=y
- CONFIG_BOARD_EXTERNAL_PA_ENABLE=y
+ CONFIG_BOARD_EXTERNAL_PA_ENABLE=n
```

备份：`ats3085e_dev_watch_ext_nor_defconfig.bak_pa`

**为什么这样改而不是删代码**

`CONFIG_AUDIO_POWERON_OPEN_PA=y` 控制的是音频子系统内部的功放上电时序，删掉会影响喇叭。
`CONFIG_BOARD_EXTERNAL_PA_ENABLE=n` 只是告诉板级"这块板没有外置功放使能脚"，影响面最小。

**验证判据**

- 53 号脚从 0V 变成 1.8V
- 开机音效仍在（喇叭无副作用）

---

### BUG-02 GPIO54 被加速度计 INT1 硬顶

**现象**

`app at2reg` 读 QMA6100P，能读到芯片在线（`0x00=0x90`），但一开始有 `sensor init failed!` 的报错。

同时 54 号脚电压长期 **1.84V**，用 53 拉低也拉不动。

**根因（分两层）**

第一层：`board.h` 把 `CONFIG_SENSOR_ACC_ISR_GPIO` 配成 **54**，加速度计的中断输出直接驱动这个脚。

用户上传原理图确认：**GPIO54 直连 U13（STK8321/QMA6100P）第 5 脚 INT1，中间没有串联元件。**

第二层：`sensor init failed!` 是**误报**。`sensor_hal.c:48-87` 里的 `ret` 变量跨循环共用，一个子步骤失败会污染后面的判断，导致看起来"整个 sensor 初始化失败"。

**排查步骤（含一次严重误判）**

1. 先改 `board.h`，把 `CONFIG_SENSOR_ACC_ISR_GPIO` 从 54 改 0（ACC 自动退回 TIMER5 触发）
2. 54 仍然是 1.84V → 说明不是软件配错，是传感器芯片真的在驱动这个脚
3. 用 `app at2reg` 读 QMA6100P 寄存器：`0x00=0x90`（在线）、`0x11=0x4`（suspend）、`0x20=0x2`（开漏+低有效）
4. 改 `0x20` 为 `0x02`，并把 `0x17` / `0x1A` / `0x21` 清零
5. 用 `app at2out` 测 54 输出低 → 量到 **0V** → **当时判定"开漏生效，引脚已自由"**
6. **据此得出结论："传感器不用拆"**
7. **但用户此时已经把芯片拆掉了** —— 那次测量是在 U13 已经不在板上的前提下做的
8. 用户后来明确：**没拆之前跑不通，拆了之后用当时固件直接跑就通了**

**⚠ 这一条必须记住的教训（循环论证）**

第 5 步的"54 输出低 = 0V"之所以成立，是因为**芯片已经被拆掉了**，54 本来就是自由的。这个测试根本无法区分「开漏改造生效」和「芯片已物理移除」。

当时的推理链是：

```
我改了 0x20（开漏）
→ 54 能拉到 0V
→ 所以引脚自由
→ 所以不用拆芯片
```

**第二步的事实来源是"芯片已经拆了"，用它的结果去论证"不用拆"，这是纯粹的循环论证。**

**改动（软件）**

`board.h:68`

```diff
- #define CONFIG_SENSOR_ACC_ISR_GPIO          54
+ #define CONFIG_SENSOR_ACC_ISR_GPIO          0     /* was 54, change to 0 to free GPIO54 for UART2 RX (ACC falls back to timer trigger) */
```

`sensor_acc_qma6100.c` 的 `init_cfg` 和 `on_cfg` 两处都改：

| 寄存器 | 原值 | 新值 | 作用 |
|---|---|---|---|
| `0x17 INT_EN0` | 保留 | `0x00` | 关掉所有中断使能 |
| `0x1A` | 保留 | `0x00` | 关掉第二组中断使能 |
| `0x20 INTPIN_CONF` | **默认 0x05** | **`0x02`** | **开漏 + 低有效** |
| `0x21 INT_CFG` | 保留 | `0x00` | 中断配置清零 |

`off_cfg`（`0x11=0x4` → suspend）未动。

备份：`sensor_acc_qma6100.c.bak_int1`

**改动（硬件，最终真正解决问题的动作）**

**U13（QMA6100P / STK8321）已物理拆除。**

这一步不在任何软件改动清单里，但它是链路打通的决定性动作。

| 影响 | 结果 |
|---|---|
| GPIO54 | 完全自由，UART2 RX 上无任何外部负载 |
| 加速度计功能 | **永久失去**（抬腕亮屏、计步等依赖 ACC 的功能不再工作） |
| 屏幕旋转 / 自动亮度 | 若依赖 ACC，同样失效 |

**机理：这个 BUG 修的是 RX 方向，不是 TX**

拆掉 U13 之后的实际现象，用户已经说清楚了：

```
拆之前   ：自环失败 / 链路不通
拆之后   ：自环（53↔54 短接）成功  → 54 侧通路恢复
接 503   ：只能收（模组 TXD → 54），发不出去（53 → 模组 RXD）
改 PADDRV ：才真正能发能收
```

**关键区别在这里：**

- **拆 U13 解决的是 54（RX）方向** —— 芯片的 INT1 挂在 54 上，干扰了接收通路
- **PADDRV 4→7 解决的是 53（TX）方向** —— 完全另一个问题（见 BUG-13）

**这两个是串联的两个故障，不是一个故障的两种解法。**

之前的误判在于：我把"拆芯片后自环成功"读成了"链路全通"。实际那只证明了 **54 侧自由了**——TX 方向当时仍然是坏的，接入 503 后"只收到 URC、发不出 AT"就是铁证。

**准确的三句话：**

| 说法 | 对不对 |
|---|---|
| "拆 U13 是唯一真因" | ❌ |
| "PADDRV 7 是唯一真因" | ❌ |
| "拆 U13 修好 RX 侧，PADDRV 7 修好 TX 侧，两个都必需" | ✅ |

**现在的状态**

| 项目 | 状态 |
|---|---|
| U13 硬件 | **已拆除** |
| `0x20=0x02` 软件改动 | 保留（对已拆芯片已无意义，回滚也不影响任何功能） |
| 加速度计 | 不可用 |
| GPIO54 RX 方向 | 已修好（自环可通） |
| GPIO53 TX 方向 | 由 BUG-13 的 PADDRV 7 修好 |

**这条要记住的教训**

**"自环通过" ≠ "链路打通"。** 自环只验证了 MCU 自己的 TX 和 RX 在内部能连通，**完全不涉及对端设备**。

拆完 U13 看到自环成功就以为大功告成，实际对端（模组）那边还有一个独立的问题在等着——驱动强度压不住它的内部上拉。

**验收判据必须是"和对端一问一答"，不是"自己和自己对得上"。**

---

## 阶段二：UART2 通路建立

### BUG-03 编译命令用错，build.cmd 带参数报错

**现象**

```
build.cmd build
unrecognized arguments: build
```

**根因**

`build.cmd` 是个包装脚本，内部已经写死了 `west build` 的参数（板名、输出目录、应用路径）。它不接受位置参数，多传一个 `build` 会被透传给 `west` 并报错。

**排查步骤**

1. 打开 `build.cmd` 看内容 → 确认它是完整命令行，不是子命令分发器
2. 去掉参数直接跑 → 成功

**正确用法**

```powershell
Set-Location D:\ats3085e\TAG_ZS3085E_241230
cmd /c "build.cmd > D:\ats3085e\build_xxx.log 2>&1"
```

**关键约束**

- 必须在 `TAG_ZS3085E_241230` **根目录**跑
- 必须设好这几个环境变量：
  ```
  PATH 前置：Python39、Python39\Scripts、C:\Actions\ZEPHYR_SDK\1.10\bin
  ZEPHYR_BASE = ...\TAG_ZS3085E_241230\zephyr
  ZEPHYR_TOOLCHAIN_VARIANT = gnuarmemb
  GNUARMEMB_TOOLCHAIN_PATH = C:\Actions\ZEPHYR_SDK\1.10\gcc-arm-none-eabi-9-2020-q2-update-win32
  ```
- 前台跑超过 15 秒会被转后台，用 `TaskOutput block=true` 等 2~3 分钟
- 成功标志：日志末行 `build finished`，exit code 0

**怎么确认新代码真的进了固件**

| 手段 | 是否有效 |
|---|---|
| 看 `.fw` 的 mtime 变了 | ✅ 有效 |
| Grep 编译日志里的 `Building C object ... <源文件>.obj` | ✅ **最可靠** |
| `arm-none-eabi-nm` 查符号 | ❌ static 函数会被内联，查不到 |
| 二进制里搜字符串 | ❌ 没有新增字符串常量的改动搜不出来 |

---

### BUG-04 自环通、接模组 0 字节

**现象**

先用杜邦线把 53 和 54 短接（引脚自环），跑 `app at2 AT`：

```
RX total 3 byte(s)
TEXT: AT
discarded 0 old RX byte(s), error flags 0x0
```

四次全通。**说明 UART2 的发送和接收在 SoC 内部都是好的。**

但把 53 接到模组 `UART_RXD`、54 接到模组 `UART_TXD` 之后：

- 模组 → 3085E 方向：**通**（收到 `\r\n+CPIN: READY\r\n` 等 URC）
- 3085E → 模组方向：**永远 0 字节**

**根因**

当时未知。这个现象本身是关键线索：**RX 通、TX 不通 = 问题在 TX 这条线上。**

**排查步骤（这一层先排除逻辑故障）**

1. 自环测试通过 → 排除 UART2 外设没起来、驱动被裁、pinmux 被覆盖
2. 查开机日志有无 `z_sys_init dev(UART_2)` → 有，说明初始化跑了
3. 查寄存器：`ctrl=0xF0008003`（RX_EN/TX_EN/FIFO_EN/EN 全开）、`br=0x01160116`（div=278，32MHz → 115107 bps，误差 0.08%）→ 配置正确
4. 确认 `uart_acts_poll_in()` 是死等，绝不能调；`uart_acts_fifo_read()` 非阻塞；`uart_acts_irq_rx_ready()` 可当探针
5. 换 PC 直接监听：`listen_com5.ps1` 只听 COM5 不发字节，跑 `app at2tx AT` → **COM5 完全安静**，一个字节都没出去

**结论**

不是软件逻辑问题，是**物理层问题**。转入阶段三。

---

### BUG-05 at2tx 只收到 2 字节就退出

**现象**

外部供电解决冲突后（当时用外部 3.8V 稳压电源——**后来发现 USB 供电也能通，见开头"实测成功的最终配置"**），第一次跑 `app at2tx AT`：

```
RX 2 byte(s): 0D 0A
```

只有 `\r\n`，没有后面的 `OK`。

**根因**

`shell_at2tx` 的收尾条件写错了：

```c
/* 原代码 */
if (rxlen > 0 && rxbuf[rxlen - 1] == '\n') break;
```

模组应答是 `\r\nOK\r\n`。模组**先回显命令**（`AT\r\n`），再回结果。所以收到第一个 `\n`（回显的结尾）时就满足了条件，提前退出。

**排查步骤**

1. 看收到的字节是 `0D 0A` → 正好是 `\r\n`
2. 对照模组应答格式：`AT` 命令的回显 + `\r\n` + 结果 + `\r\n`
3. 定位到 `rxbuf[rxlen-1] == '\n'` 这个条件
4. 改成"扫 `OK` 两个字母"

**改动**

`system_app_shell.c` 新增辅助函数 + 改收尾条件：

```c
/*
 * 函数功能：检查收到的数据里有没有 "OK"（只扫最后 16 字节，结果码一定在尾部）
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
```

```diff
- if (rxlen > 0 && rxbuf[rxlen - 1] == '\n') break;
+ if (rxlen > 0 && at2_rx_has_ok(rxbuf, rxlen)) break;
```

**为什么不能靠"末尾换行"判结束**

模组应答头部就是 `\r\n`，用换行判定必然提前退出。**判据必须是"看到了结果码本身"**，而不是"看到了某个分隔符"。

**验证判据**

`app at2tx AT` → 收到 `0D 0A 4F 4B`（`\r\nOK`），不再只有 2 字节。

---

## 阶段三：核心 — TX 方向永久 0 字节

这一段是整个项目最费时间的部分：**连续 7 个假设被推翻，第 8 个才是真因。**

### BUG-06 误判：收到 URC 就当链路通了

**现象**

跑 `app at2tx AT`，收到 16 字节：

```
RX 16 byte(s): 0D 0A 2B 43 50 49 4E 3A 20 52 45 41 44 59 0D 0A
              = "\r\n+CPIN: READY\r\n"
```

当时判定"链路通了"。

**根因（认知错误）**

`+CPIN: READY` 是模组**主动上报**的 URC（Unsolicited Result Code），和"我发的 AT 被收到了"毫无关系。

**URC 只证明一件事：54（RX）这条线通。**

**排查步骤 & 自我纠正**

1. 反复跑 `app at2tx AT`，再也没出现过 `4F 4B`（O、K）
2. 用户自述"只是小概率成功" → 说明那 16 字节和发命令无关
3. 重新定义判据：**只有出现 `OK` 才算通，任何 URC 都不算**

**这个误判的代价**

浪费了一整轮判断，而且差点让方向转向"信号不稳定"。

**修正后的判据**

| 收到什么 | 能证明什么 |
|---|---|
| `+CPIN: READY` / `RDY` / `+CGDEV: ...` | 只证明 **RX 通** |
| `\r\nOK\r\n` | 证明 **TX 通了**（模组收到了命令并回复） |

---

### BUG-07 误判：DTR 没接地

**现象**

读到《UMA503-T9 模组 RI 功能与 DTR 功能说明 V1.0》，里面写：

> **DTR 拉高进休眠，拉低退出休眠；使用 DTR 功能前需配置 `AT+UMSCLK=1`。**

据此判断：DTR 悬空被内部上拉判高 → 模组睡着了 → UART 不响应。

**排查步骤**

1. 问清楚 DTR 接哪边 GND → 两边同一个 GND 网络，接 503 板第 2 行任一 GND 孔
2. 接上后重跑 `app at2tx AT` → **仍然 0 字节**
3. 结论：DTR 不是根因

**为什么这个假设合理**

手册明写 DTR 能控制休眠，而模组确实可能不响应 AT。但**"可能"和"是"之间必须靠实测分开。**

**后续处理**

**最终状态：DTR 已拆除，悬空。**

`AT+UMSCLK=0` 关掉低功耗之后，DTR 的作用完全消失。链路真正打通时 DTR 是**完全不接**的状态——这从反面证明这条假设与根因无关。

**这条经验**：DTR 只有在「`AT+UMSCLK=1` 允许进低功耗，且依赖 DTR 判休眠」的场景才需要处理。低功耗关掉后，它就是个无关引脚，可以不接。

---

### BUG-08 误判：波特率被锁死

**现象**

硬件手册 P25 写：

> MAIN_UART 支持自适应波特率 4800 ~ 3.6Mbps，**但一旦用某个速率连上过 AT 就不再自适应，锁定为该速率。**

之前用 PC 通过 COM5 在 **115200** 连通过，理论上应该锁在 115200。但也不排除锁在别的值。

**排查步骤**

1. 新增 `app at2scan` 命令：遍历 4800 / 9600 / 14400 / 19200 / 38400 / 57600 / 115200 / 230400 / 460800 / 921600
2. 每档直接写 `br` 寄存器（`0x40040010`）：`div = 32000000 / baud`，`*br = (div<<16) | (div & 0xFFFF)`
3. 每档发 `AT\r`，开 250ms 窗口轮询 `uart_irq_rx_ready`
4. 扫完恢复原分频

**结果**

```
10 档全部 RX 0
-> no baud produced a reply. rate is not the issue
AT2U[scanend] ctrl=0xF0008003 stat=0x001004A2 br=0x01160116
```

**结论**

**波特率假设被彻底排除。** 这个命令虽然没解决问题，但省掉了后面反复怀疑速率的可能。

**这个命令还有价值**

以后任何"发出去没回应"的场景，`at2scan` 都能在 3 秒内排除速率问题。

---

### BUG-09 误判：模组进了 PSM 睡眠

**现象**

读《UMA502-T9 & UMA503-T9 低功耗模式应用手册 V1.0》：

- `AT+UMSCLK`：0 = 退出低功耗 / 1 = 满足进入条件 / 2 = 立刻睡眠
- 手册示例 `AT+UMSCLK?` 回 `+UMSCLK: 1` → **出厂就允许进低功耗**
- 进 PSM 流程：`AT+UMSCLK=2` → `AT+CPSMS=1` → `AT+MEDCR=0,103,1`
- **唤醒只有两种：T3412 定时器超时、拉低 PWRKEY**
- **没有任何 UART 唤醒方式**
- 实测低功耗电流 6.5 µA

据此建立模型："模组睡着了 → UART 关闭 → AT 进不去"。

**排查步骤**

1. 新增 `app at2wake`：60 秒窗口，每 200ms 发 3 字节 `AT\r` + 120ms 收窗
2. **第一版有缺陷**：收到任何字节就 break → 收到 URC 就误判"已唤醒"
3. 修正：只在 `at2_rx_has_ok` 为真时才 break；收到非 OK 数据则打印 `-> that was a URC, not an AT answer. keep waiting...` 后继续
4. 跑 209 轮全 0 → `-> 25s no OK. module not in AT session.`
5. 窗口扩到 60 秒 → 仍然全 0

**决定性证据（推翻这个假设）**

用户上传一份 6599 行的完整 log，里面筛出 **12+ 次完整的「开机 → 关机」循环**，周期 2~5 秒，全部是 URC、**零个 OK**：

| RX 字节数 | 解码 |
|---|---|
| 45 | `+CGDEV: NW PDN DEACT 1` + `+CGDEV: ME DETACH` |
| 18 | `*RADIOPOWER: 0` |
| 7 | `RDY` |
| 16 | `+CPIN: READY` |
| 20 | `+CGDEV: ME DETACH` |
| 24 | `+CGDEV: EPPS PDN ACT 1` |

**更关键的一条**：排查中曾临时把 PWRKEY 直接接地（**后来已回退，最终成功时 PWRKEY 没动**），那次 log 里出现了完整的正常启动链：

```
RDY → +CPIN: READY → +CGDEV: EPPS PDN ACT 1
```

`EPPS PDN ACT 1` = **PDN 已激活，已经联网成功**。

**但在此之后 44 秒内、220 次 AT 全部 0 应答。**

**结论**

模组刚建好 PDN，不可能立刻进 PSM。**"睡眠"假说被彻底推翻——模组是醒着的，是 TX 驱动不够，模组压根没收到我们的字节。**

**这个假设为什么值得排除**

它看起来非常合理（手册明写有 PSM、明写没有 UART 唤醒）。**但"合理"不等于"是"。** 关键证伪点是：`PDN ACT` 之后的 44 秒里模组必然醒着。

---

### BUG-10 误判：PWRKEY 按太短

**现象**

手册写 PWRKEY **低有效 > 550ms 开机**。用户按"短按 100~300ms"，模组不开机。

**排查步骤**

1. 用户按 PWRKEY 时 log 出现 `ME DETACH` + `*RADIOPOWER: 0` → 这是**关机**流程，不是开机
2. 对照手册门槛：开机 > 550ms，关机 **650ms ~ 1s**
3. **两个门槛只差 100ms**
4. 用户承认"发送命令后反复按 PWRKEY" → 按键时长 > 650ms，变成"开机即关机"

**结论**

按键时长在两可之间，导致模组状态反复翻转。

**正确做法**

- 开机：按住 **> 550ms**，但 **< 650ms**（实操建议 600ms 左右）
- 想稳定：**串 4.7K 电阻接 GND**，上电自动开机，不靠按键

---

### BUG-11 PWRKEY 直连 GND —— 一次阶段性尝试（已回退）

**现象**

排查过程中曾临时把 PWRKEY 直接接 GND（原本 3.97V，接地后 0V），然后"一次没按"跑命令。

log 只剩 2 次循环，但出现了完整的正常启动链（`RDY` → `+CPIN: READY` → `+CGDEV: EPPS PDN ACT 1`）。

看起来"解决了"，但随后 44 秒 220 次 AT 全部 0 应答。

**根因**

手册 P22 注写明：

> 若要上电自动开机，可把 PWRKEY 通过约 **4.7K 电阻**接 GND。

**直连 0Ω 会让 PWRKEY 电位停在临界区**，噪声一抖就可能跨过 650ms 关机门槛，从"开机"滑到"关机"。log 里两次 `ME DETACH` + `*RADIOPOWER: 0` 就是证据。

**为什么这条没成为最终方案**

真正打通链路时，**PWRKEY 是完全没动的**——保持模组板原厂状态，靠 USB 插入后板上的自动开机电路上电。

也就是说：**PWRKEY 从头到尾都不是根因，它只是排查过程中被反复怀疑的一块。**

**最终状态**

| 项目 | 状态 |
|---|---|
| 是否直连 GND | ❌ **已回退，现在悬空 / 原厂状态** |
| 是否是根因 | ❌ **不是** |
| 是否仍需串 4.7K | 当前不需要 |

**什么时候才需要串 4.7K**

只有做"整机断电重启后自动联网"这类需求时才需要——那时不靠手动按键，靠 PWRKEY 常低触发自动开机。现在靠 USB 插入即可开机，不需要。

---

### BUG-12 假阳性：模组关机时测到 0V

**现象**

这是整个排查里**最致命的一个认知陷阱**。

早期为了确认 53 能不能把模组 RXD 拉低，用 `app at2out` 让 53 输出低，量模组 `UART_RXD` 孔：

| 时刻 | 模组状态 | 量到的电压 | 当时的判断 |
|---|---|---|---|
| CH342 通电时 | 模组开机 | **1.38V** | 分压 → 有第二个驱动源 |
| CH342 断电后 | **模组关机** | **0V** | "物理层全部合格"（**这是错的**） |

**根因**

模组关机（`VDD_EXT = 0V`）时，`UART_RXD` 引脚**不上电**，内部约 4.7K 上拉**不工作**。

此时 53 输出低量到 0V，**只证明了"导线通"，不证明"模组能收到低电平"**。

模组一开机，RXD 引脚上电，内部上拉激活，把线顶向 1.8V。53 的驱动不够，只能压到 1.38V，而 1.8V 逻辑判低阈值约 **0.54V**，所以模组永远读成"高 = 空闲"，AT 全部被丢。

**排查步骤（如何识破这个假阳性）**

1. 用户质疑："之前切换稳压电源时 at2out 量到 0V，为什么现在 1.38V？"
2. 反问：那次测量时模组是开机的吗？→ 是关机的（`VDD_EXT=0V`）
3. 结论：**那次 0V 是假阳性**
4. 重新建立判据：**所有电平测量必须在模组已上电的状态下做**

**1.38V 的正确解读**

```
模组 RXD 内部 4.7K 上拉 → 把线顶向 1.8V
53 输出低 → 等效阻抗很弱，压不到地
两个"半推"叠加 → 停在 1.38V
```

反推：`1.8 × R_dn/(R_dn+R_up) = 1.38` → `R_up ≈ 0.304 × R_dn`，即模组上拉约 4.7K、53 输出低等效阻抗约 15K（很弱）。

**这条教训**

**判断某个 GPIO 能不能把对端拉低，必须在"对端已上电"的状态下测。对端没上电时测到的 0V 只证明导线通。**

---

### BUG-13 PADDRV 驱动档位不足（4 → 7）—— TX 方向的真正根因

**这一条和 BUG-02 是两个独立的问题，别混在一起**

| | BUG-02 | BUG-13 |
|---|---|---|
| 方向 | **RX（54 侧）** | **TX（53 侧）** |
| 根因 | QMA6100P INT1 挂在 54 上，干扰接收通路 | PADDRV=4 驱动不足，压不住模组 RXD 内部上拉 |
| 解法 | **物理拆除 U13** | **PADDRV 4→7** |
| 验证 | 拆后自环通过 | 改后 `app at2tx AT` 连续 11 次 OK |

**为什么能确定这一条是必要的**

用户已经把时间线讲清了：

```
拆 U13 之后      → 自环成功，但接 503 只能收、不能发
改 PADDRV 之后   → 能发能收
```

**"拆完之后还是发不出去"就是 PADDRV 必要性的直接证据。**

如果拆芯片已经解决了全部问题，就不会存在"只能收不能发"这个中间状态。

**但测量方法仍有一个瑕疵（诚实保留）**

`at2lv` 把 53 配成 **MFP=0 的 GPIO 输出模式**（`0x40` = OUTEN，MFP 字段为 0），而 UART 真正工作时是 **MFP=7**。两种模式驱动级可能不同，所以 **1.37V / 0.1V 这两个读数不能直接当作 UART 模式下的真实电平**。

不过这不影响结论——**PADDRV 4→7 改完，实际链路就通了，这是最终实证。** 测量方法的瑕疵只影响"怎么解释它为什么通"，不影响"它确实通了"这个事实。

**现象**

模组开机状态下，53 输出低只能把线压到 **1.37V**，判不成低电平。

**根因**

`GPIO_CTL_PADDRV_LEVEL` 这个字段控制引脚的**驱动强度**，共 8 档（0~7）。

- `UART2_MFP_CFG` 里配的是 **PADDRV = 4**（中档）
- **4 档的输出低电流不足以压过模组 RXD 的内部上拉**

**排查步骤**

还发现 `at2out` 命令本身有缺陷：`*(AT2_CTL53_ADDR) = 0x40u` 只写了输出使能位，**PADDRV 字段是 0（最弱档）**，所以之前测的电压不能代表 UART 正式工作时的水平。

新增 `app at2lv` 命令：53 输出低，PADDRV 依次 0 / 4 / 7，每档 5 秒；再测 53 高、54 低/高。

**用户的六档实测**

| 阶段 | PADDRV | 动作 | 实测电压 | 判读 |
|---|---|---|---|---|
| [1/6] | 0（最弱） | 53 输出低 | **1.37V** | 判高 ❌ |
| [2/6] | **4（UART 原档）** | 53 输出低 | **1.37V** | 判高 ❌ |
| [3/6] | **7（最强）** | 53 输出低 | **0.1V** | **判低 ✅** |
| [4/6] | 4 | 53 输出高 | 1.78V | 正常 ✅ |
| [5/6] | 4 | 54 输出低 | **1.78V** | 模组 TXD 在顶上，证明模组开机 ✅ |
| [6/6] | 4 | 54 输出高 | 1.78V | 正常 ✅ |

**两个关键发现**

1. **4 档和 0 档效果完全一样（都是 1.37V）** —— 原档位在 GPIO 模式下压不住
2. **7 档立刻掉到 0.1V** —— 远低于 0.54V 阈值

**改动**

`board.c`（第 45 行）：

```diff
- /* uart2 tx */
- PIN_MFP_SET(GPIO_53,  (UART2_MFP_CFG)),
+ /* uart2 tx: 驱动强度拉到最大档 PADDRV=7。
+  * 实测依据：PADDRV=0 与 PADDRV=4 时，"输出低"只能把线上拉到
+  * 1.37V，因为 4G 模组的 UART_RXD 引脚内部有约 4.7K 上拉顶住；
+  * 同一条件改 PADDRV=7 后能拉到 0.1V，模组才判得出低电平。
+  */
+ PIN_MFP_SET(GPIO_53,  (UART2_MFP_CFG | GPIO_CTL_PADDRV_LEVEL(7))),
```

配置值从 **`0x4127`** 变成 **`0x7127`**（PADDRV 字段 4 → 7，`0x7000` 的差值）。

`system_app_shell.c` 同步改（否则测完引脚恢复时会退回 4 档）：

```diff
+ #define AT2_MFP_UART2_TX  0x7127u    /* 53 号脚：UART2 复用 + PADDRV 拉满 */

- *(AT2_CTL53_ADDR) = AT2_MFP_UART2;      /* at2_pin_force 里 */
+ *(AT2_CTL53_ADDR) = AT2_MFP_UART2_TX;

- *(AT2_CTL53_ADDR) = AT2_MFP_UART2;      /* 三处"恢复 UART 复用" */
+ *(AT2_CTL53_ADDR) = AT2_MFP_UART2_TX;
```

54（RX）保持 `AT2_MFP_UART2` 不动 —— 它是输入，不需要驱动强度。

**为什么不是别的解法**

| 备选方案 | 为什么不用 |
|---|---|
| 加 1K 下拉电阻 | 要动烙铁，且 53 输出高时会被下拉拖累 |
| 改模组 RXD 内部上拉（AT 命令没有这个能力） | 模组侧不可控 |
| 换引脚 | UART2 在 SDK 里只有 53/54 一组，无备选 |
| 拆模组上拉电阻 | 资料包里没有 EVK 原理图，找不到那颗电阻 |

PADDRV 是**纯软件、零成本、可回退**的方案，所以在归因不确定的情况下仍然值得保留。

**验证判据**

```
app at2tx AT  →  RX 4 byte(s): 0D 0A 4F 4B     ← 连续 11 次全部成功
```

**修复后完整验证**

| 命令 | 应答 | 含义 |
|---|---|---|
| `app at2tx AT` × 11 | `0D 0A 4F 4B` | 11/11 全通 |
| `app at2tx AT+UMSCLK=0` | `0D 0A 4F 4B` | PSM 已关闭 |
| `app at2tx AT+CSQ` | `+CSQ: 27,99` | 信号 27/31 |
| `app at2tx AT+CGPADDR` | `+CGPADDR: 1,"10.38.183.59",...` | **运营商分配的真实 IP** |

---

## 阶段四：shell 参数与手册陷阱

### BUG-14 Zephyr shell 把 ? 当 glob 通配符吃掉

**现象**

```
app at2tx AT+CPIN?   →  usage: app at2tx <at-command>   e.g. app at2tx AT+CSQ
app at2tx AT+CREG?   →  usage: ...
app at2tx AT+CGATT?  →  usage: ...
```

但 `AT+CSQ`、`AT+CGPADDR`（**不带 `?`**）全部正常。

加引号也不行：

```
app at2tx "AT+CPIN?"  →  仍然 usage
```

**根因**

Zephyr shell 里 `?` 是 **glob 通配符**（和 bash 的 `?` 一样，匹配任意单个字符）。

`AT+CPIN?` 被当作一个"文件名模式"去匹配 → 匹配不到 → **整个 token 被丢弃** → `at2tx` 收到 `argc = 2` → 触发 `if (argc < 2)` 的 usage 分支。

引号拦不住，是因为 Zephyr 的 wildcard 展开发生在引号处理之外。

**排查步骤**

1. 对照代码：`shell_at2tx` 里 `if (argc < 2)` 打印 usage
2. 确认 `AT+CSQ` 能进函数（argc 够），`AT+CPIN?` 进不去 → 参数在 shell 层就没了
3. 试引号 → 无效
4. 判定为 glob 展开行为
5. 改用替代字符：`#` 不在 Zephyr shell 元字符名单里

**改动**

`system_app_shell.c` 的 `shell_at2tx`，拼命令时把 `#` 转回 `?`：

```c
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
```

usage 提示也补了三行：

```c
if (argc < 2) {
	shell_print(sh, "usage: app at2tx <at-command>   e.g. app at2tx AT+CSQ");
	shell_print(sh, "       shell 会把 '?' 当通配符吃掉，查询命令改用 '#' 代写：");
	shell_print(sh, "       app at2tx AT+CPIN#   ->  实际发送 AT+CPIN?");
	return 0;
}
```

`app at2http` 里也做了同样处理（URL 常带 query string）。

**用法**

```bash
app at2tx AT+CPIN#    →  实际发 AT+CPIN?
app at2tx AT+CREG#    →  实际发 AT+CREG?
app at2tx AT+CGATT#   →  实际发 AT+CGATT?
```

**验证判据**

```
AT+CPIN#   →  +CPIN: READY
AT+CREG#   →  +CREG: 0,1
AT+CGATT#  →  +CGATT: 1
```

三条全通。

---

### BUG-15 官方手册两个参数名印错

**现象**

按九联《UMA50X 系列 HTTP(S) 应用 AT 命令手册 V1.1》写命令，会收到 `+CME ERROR`。

**排查步骤**

跑 `app at2tx AT+HTTPCFG#`，模组回了 **189 字节**完整参数表：

```
+HTTPCFG: "contextid",0
+HTTPCFG: "requestheader",0
+HTTPCFG: "responseheader",0
+HTTPCFG: "sslctxid",0
+HTTPCFG: "contenttype",0
+HTTPCFG: "rspout/auto",0        ← 注意！
+HTTPCFG: "closed/ind",0         ← 注意！
OK
```

**对照结果**

| 手册写的 | 模组实际认的 |
|---|---|
| `"rsppout/auto"` | **`"rspout/auto"`**（一个 p） |
| `"closedind"` | **`"closed/ind"`**（带斜杠） |

**根因**

手册排版错误（V1.1 修订记录里写"增加 content_type 类型配置"，估计那次改动碰坏了相邻字段）。

**改动**

代码和文档一律按**模组实测**的参数名，不用手册的：

```c
AT2HTTP_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, "2/6 rspout/auto");
```

**这条教训**

**`AT+XXX?` 或 `AT+XXX=?` 永远比手册可靠。** 模组自己报的参数名才是真的。

同理：`AT+HTTPGET?` 回 `+CME ERROR: 4` 是**正常的**——手册明写"查询命令 AT+HTTPGET?（不支持）"。

---

## 第五章：代码改动逐行清单（按重写顺序）

> 这一章只回答一个问题：**如果今天让你从零再做一遍，第一步改哪个文件、第二步改哪个文件、改的每一行在干什么。**
>
> 顺序不是按"修改时间"排的，是按**依赖关系**排的 —— 前一步不做，后一步就没有意义。跟着这个顺序读代码，逻辑是连贯的。

---

### 5.0 提交注释（本章开头的总览注释）

> 下面这段参照 `D:\ZS302A_261010` 的 git 提交体例写成：`修改项：`开头、分「一、二、三」大段、每条写「改了什么：为什么」、带实测量化数据。

```text
修改项：UART2 打通 UMA503-V7 4G 模组 AT 链路并跑通 HTTP GET（根因：双故障——RX 侧被加速度计占用 + TX 侧驱动档位不足）

一、问题定位（先说清为什么动这几个文件）

- 现象：GPIO53 恒 0V、GPIO54 恒 0V，UART2 自环 0 字节，接模组后只能收不能发。
- 拆解为两个独立故障：
  1) RX（54）侧：QMA6100P 的 INT1 输出硬连在 GPIO54 上，把接收线顶死 → 需释放 54。
  2) TX（53）侧：GPIO53 的 PADDRV 默认 4 档，"输出低"只能把线拉到 1.37V，
     4G 模组 UART_RXD 内部约 4.7K 上拉把电平顶住，模组永远判"高=空闲" → 需拉满到 7 档。
- 判据：只有同时满足"54 自由"与"53 能拉到 0.1V 以下"，链路才通；缺一不可。

二、引脚释放（3 个文件，先腾脚，否则后面配了也没用）

- board_cfg.h:58 CONFIG_UART_2 0→1，:248 新增 IRQ_PRI=0，:364 新增 SPEED=115200：
  UART2 设备原来是编译期不存在的，不打开这三处，device_get_binding("UART_2") 直接返回 NULL。
- ats3085e_dev_watch_ext_nor_defconfig:200 CONFIG_BOARD_EXTERNAL_PA_ENABLE y→n：
  该宏打开时 board.c 会把 GPIO53 配成 GPIO 输出低（外置功放使能），把 UART2_TX 抢走。
  关掉后 53 从 0V 变 1.8V，喇叭仍有开机音效，无副作用。
- board.h:68 CONFIG_SENSOR_ACC_ISR_GPIO 54→0：
  软件层不再把 GPIO54 注册成加速度计中断脚，避免传感器驱动与 UART2 抢同一个引脚。

三、引脚配置（1 个文件，TX 驱动强度是打通的关键）

- board.c:45 GPIO53 由 UART2_MFP_CFG(0x4127) 改为 UART2_MFP_CFG|PADDRV_LEVEL(7)(0x7127)：
  0x4127 的 PADDRV=4 档实测"输出低"仅 1.37V，0x7127 的 7 档能到 0.1V，
  低于 1.8V 逻辑判低阈值(约 0.54V)，模组才认得出起始位。
- board.c:47 GPIO54 保持 UART2_MFP_CFG 不变：RX 是输入方向，驱动强度无意义。

四、传感器寄存器（1 个文件，已被硬件动作取代）

- sensor_acc_qma6100.c: 0x17=0x00(关 FIFO 水印中断)、0x1a=0x00(不映射到 INT1)、
  0x20=0x02(INT1 开漏+低有效)、0x21=0x00(INT2 清零)：
  目的是让 INT1 无中断时呈高阻，理论上可不拆芯片就释放 54。
  实测该方案单独不生效（INT1 仍压低 54），最终靠物理拆除 U13 解决，
  故本改动保留但已无实际意义，回滚不影响功能。

五、验证命令（1 个文件）

- system_app_shell.c: 新增 AT2 宏区 + at2_rx_has_ok/at2_rx_has_tok/at2_pump 三个辅助函数
  + shell_at2tx（任意 AT 透传，含 '#'→'?' 还原）+ shell_at2http（六步 HTTP GET）；
  子命令表在 :607/:608 注册。排障期用过的 at2/at2drv/at2pin/at2reg/at2out/at2lv/
  at2rx/at2scan/at2wake 已全部清理，只保留这两个长期有用的命令。

六、HTTP 判据修正（本轮唯一发现的功能性 bug）

- shell_at2http:460-473 第 5 步 hits "+HTTPGET:" 后补一次 300ms 纯收窗口：
  关键字只说明"结果码开始上报"，后面的 ,0,200,17143 还在陆续到达，
  不多收这一步，解析必然失败。
- shell_at2http:485-537 手工解析 "+HTTPGET: <err>,<httpcode>[,<body_len>]"：
  逐字符状态机，不用 sscanf（Zephyr libc 不保证 %d 完整实现）。
- shell_at2http:579 判据由"找 HTTP/1.x 响应头"改为 http_code == 200：
  responseheader=0 时模组 CONNECT 后直接吐 body，压根没有响应头行，
  按找头行判断永远为假，会把成功请求报成失败 —— 这是本轮唯一的功能性 bug。
- shell_at2http:562-572 新增 TEXT 一行把可读字节连起来，肉眼可确认拿到的是真网页。

七、硬件动作（不在代码里，但缺了它不通）

- 物理拆除 U13（QMA6100P/STK8321）：这是 RX 侧真正的解法。代价是加速度计永久失效。

八、验证结果

- AT 层：app at2tx AT 连发 11 次全部 RX 4 byte(s): 0D 0A 4F 4B（\r\nOK）；
  AT+CSQ → +CSQ: 27,99（27/31）；AT+CGPADDR → IPv4 = 10.38.183.59；AT+CGATT? → 1。
- HTTP 层（固件 261112.fw 14:12:22 版）：app at2http http://www.baidu.com/ 六步全通，
  1/6~4/6 逐条 GOT/CONNECT/OK，5/6 => err=0 http=200 body=17143 byte(s)，
  6/6 RX 598 BUF-FULL（缓冲 600 字节，收满即停），TEXT 已看到 <!DOCTYPE html> 正文。
- HTTPS 层：app at2http https://www.baidu.com/ 返回与 HTTP 完全相同的
  err=0 http=200 body=17143。但"TLS 是否真生效"未查证（sslctxid 实测为 0，
  疑为模组剥掉 scheme 按 80 端口走），不能据此认定 HTTPS 可用。
- 编译：261112.fw 14:12:22 版 FLASH 1,468,602 B、SRAM 927,664 B（较 11:33 版
  解析逻辑 +692 B，SRAM 不变，TEXT 缓冲在栈上复用）。
```

---

### 5.1 全景：一共 6 个文件 + 1 个硬件动作

**先说结论：不是 8 个文件，是 6 个源码文件 + 1 个拆芯片的硬件动作。**

| 顺序 | 文件 | 干的事 | 属于哪个 BUG | 实际修改时间 |
|---|---|---|---|---|
| **第 1 步** | `board_cfg.h` | 让 UART2 这个设备"存在" | BUG-04 前置 | 11-11 10:36 |
| **第 2 步** | `ats3085e_dev_watch_ext_nor_defconfig` | 把 GPIO53 从外置功放手里抢回来 | **BUG-01** | 11-11 15:25 |
| **第 3 步** | `board.h` | 软件层不再把 GPIO54 当加速度计中断脚 | **BUG-02**（软件侧） | 11-11 10:48 |
| **第 4 步** | `sensor_acc_qma6100.c` | 让 54 上的芯片闭嘴（后被拆芯片取代） | **BUG-02**（硬件侧） | 11-11 18:18 |
| **第 5 步** | `board.c` | 配 53/54 的复用功能 + **TX 驱动拉满** | **BUG-13** | 11-12 10:31 |
| **第 6 步** | `system_app_shell.c` | 加验证命令（AT 透传 + HTTP GET + 状态码解析） | BUG-05 / BUG-14 / 应用层 | 11-12 **14:11** |
| **硬件** | 拆除 U13（QMA6100P） | RX 侧真正的解法 | **BUG-02**（决定性） | 手动作业 |

目录基准：`D:\ats3085e\TAG_ZS3085E_241230`

**为什么是这个顺序，不是时间顺序：**

时间上是 `board_cfg.h → board.h → defconfig → sensor → board.c → shell`，但**逻辑上应该是**：

```
先让设备存在（board_cfg.h）
        ↓
再把两个脚腾出来（defconfig 腾 53 + board.h/sensor 腾 54）
        ↓
才谈得上配这两个脚（board.c 的 MFP 和 PADDRV）
        ↓
最后加命令验证（shell）
```

**如果反过来，先改 board.c 配引脚，而 `CONFIG_UART_2` 还是 0，那这段 `#if IS_ENABLED(CONFIG_UART_2)` 根本不参与编译，改了等于没改。**

---

### 第 1 步：`board_cfg.h` —— 让 UART2 存在（3 行）

**文件**：`zephyr/boards/arm/ats3085e_dev_watch_ext_nor/board_cfg.h`（1145 行）
**备份**：`board_cfg.h.bak`

#### 改动 1 —— 第 58 行：打开开关

```c
第 55: #define CONFIG_UART_0_NAME      			"UART_0"
第 56: #define CONFIG_UART_1           				0
第 57: #define CONFIG_UART_1_NAME      			"UART_1"
第 58: #define CONFIG_UART_2           				1      ← 原值 0，改成 1
第 59: #define CONFIG_UART_2_NAME      			"UART_2"
第 60: #define CONFIG_UART_3           				0
```

| 行 | 代码 | 作用 |
|---|---|---|
| 55 | `CONFIG_UART_0_NAME "UART_0"` | UART0 的设备名。**没动**，但要知道它被 `uart_poll_out_ch()` 写死成 console 口，2Mbps 调试日志全从这里出 |
| 56 | `CONFIG_UART_1 0` | UART1 **关着**。原因：`board.c` 把 GPIO16/17 配成 LCD 的 d2/d3，UART1 的脚被屏占了 |
| 57 | `CONFIG_UART_1_NAME "UART_1"` | UART1 设备名。名字在，但设备不初始化 |
| **58** | `CONFIG_UART_2 1` | **本次核心改动。** 这个宏是 `IS_ENABLED(CONFIG_UART_2)` 的来源，改成 1 之后：① `board.c` 里那段配 53/54 的代码才会参与编译；② 驱动才会在开机日志打出 `z_sys_init dev(UART_2)`；③ `device_get_binding("UART_2")` 才拿得到句柄 |
| 59 | `CONFIG_UART_2_NAME "UART_2"` | **就是 `device_get_binding("UART_2")` 里那个字符串**。改名字会让所有 `device_get_binding` 找不到设备 |
| 60 | `CONFIG_UART_3 0` | UART3 关着，不影响 |

> **为什么这一行必须排第一**：Zephyr 的设备是编译期决定的。`CONFIG_UART_2=0` 时，整个 UART2 驱动对象都不会被链接进固件 —— 后面不管怎么配引脚，都没有"某个设备"来接这根线。

#### 改动 2 —— 第 248 行：中断优先级

```c
第 247: #define CONFIG_UART_1_IRQ_PRI   				0
第 248: #define CONFIG_UART_2_IRQ_PRI   				0      ← 新增
第 249:
第 250: #define CONFIG_MMC_0_IRQ_PRI        			0
```

| 行 | 代码 | 作用 |
|---|---|---|
| 247 | `CONFIG_UART_1_IRQ_PRI 0` | 参考行，UART1 中断优先级（UART1 没启用，无实际作用） |
| **248** | `CONFIG_UART_2_IRQ_PRI 0` | **新增。** UART2 中断优先级，0 是最高。取 0 的理由：UART 接收是"每 87µs 到一个字节"，中断优先级必须高于大部分应用线程，否则 FIFO 会溢出丢字节 |

> **这里的坑**：`0` 不是"没配"，是**最高优先级**。如果后面有别的外设也用 0，会互相打断。当前工程其余外设没占 0，所以安全。

#### 改动 3 —— 第 364 行：默认波特率

```c
第 363: #define CONFIG_UART_1_SPEED     115200
第 364: #define CONFIG_UART_2_SPEED     115200     ← 新增
```

| 行 | 代码 | 作用 |
|---|---|---|
| 363 | `CONFIG_UART_1_SPEED 115200` | 参考行 |
| **364** | `CONFIG_UART_2_SPEED 115200` | **新增。** UART2 初始化时用它算分频。115200 是模组 UMA503-V7 的出厂默认速率（硬件手册 P25 明写默认 115200） |

**算分频的公式（`uart_acts_init_bycfg` 里）：**

```
div = 32000000 / baud = 32000000 / 115200 = 277.7 → 取 277
实际速率 = 32000000 / 277 = 115,523 bps
误差 = (115523-115200)/115200 = +0.28%
```

> **注**：实测寄存器读回是 `br=0x01160116`（div=278），算出来 115,107 bps，误差 **−0.08%**，比 277 更准。驱动在取整时用的是四舍五入，不是截断。

**UART2 未配 DMA**（`board_cfg.h` 里没有 `CONFIG_UART_2_DMA` 相关项）。后果：只能轮询或中断逐字节读，跑 HTTP 17KB 响应（每 87µs 一字节）会来不及 —— 这也是附录 D 里"UART2 开 RX DMA"还欠着的原因。

---

### 第 2 步：`ats3085e_dev_watch_ext_nor_defconfig` —— 腾出 GPIO53（1 行）

**文件**：`zephyr/boards/arm/ats3085e_dev_watch_ext_nor/ats3085e_dev_watch_ext_nor_defconfig`
**备份**：`ats3085e_dev_watch_ext_nor_defconfig.bak_pa`（7904 字节）

```ini
第 195: CONFIG_AUDIO_IN_ACTS=y
第 196: CONFIG_AUDIO_IN_ADC_SUPPORT=y
第 197: CONFIG_AUDIO_IN_I2SRX_SUPPORT=n
第 198: CONFIG_AUDIO_OUT_DAC_PCMBUF_SUPPORT=y
第 199: CONFIG_AUDIO_POWERON_OPEN_PA=y          ← 未动
第 200: CONFIG_BOARD_EXTERNAL_PA_ENABLE=n       ← 原值 y，改 n
第 201: CONFIG_AUDIO_ANTIPOP_PROCESS=n
```

| 行 | 代码 | 作用 | 动没动 |
|---|---|---|---|
| 198 | `AUDIO_OUT_DAC_PCMBUF_SUPPORT=y` | DAC 输出带 PCM 缓冲 | 未动 |
| **199** | `AUDIO_POWERON_OPEN_PA=y` | **"开机就打开 PA"这个行为本身** | **未动** |
| **200** | `BOARD_EXTERNAL_PA_ENABLE=n` | **"板上有没有外置 PA 需要使能引脚"** | **y→n（核心）** |
| 201 | `AUDIO_ANTIPOP_PROCESS=n` | 防爆音处理 | 未动 |

#### 为什么是第 200 行，不是第 199 行？

**这是整个 BUG-01 最容易改错的地方。** 两个宏名字很像，作用完全不同：

```
199 CONFIG_AUDIO_POWERON_OPEN_PA=y      "开机要不要走开 PA 这套流程"
        ↓
200 CONFIG_BOARD_EXTERNAL_PA_ENABLE=y  "这套流程里，要不要去拉一个使能引脚"
        ↓
     audio_out_acts.c:1742-1750  调用 PA 使能
        ↓
     board.c:427-428  PIN_MFP_SET(GPIO_53, ... GPIO 输出低)
        ↓
     GPIO53 被配成 GPIO 输出低 → UART2_TX 被抢走 → 恒 0V
```

- 改 **199** → 整个开机 PA 流程都跳过，**可能连内部 PA 的初始化一起跳过**，风险大
- 改 **200** → 只取消"外置使能引脚"这一动作，**内部音频通路完全不动**

**实测验证（这是拍板改 200 的依据）：**

| 改之前 | 改之后 |
|---|---|
| GPIO53 = **0V**（被拉低） | GPIO53 = **1.8V**（自由，可做 UART） |
| 喇叭有开机音效 | 喇叭**仍有**开机音效 |
| — | 无任何副作用 |

> **一句话**：改 200 等于"拔掉外置功放的使能线"，不是"关掉功放"。板上如果本来就靠内部 DAC 直推喇叭，那这个改动毫无损失。

---

### 第 3 步：`board.h` —— 软件层不再占 GPIO54（1 行）

**文件**：`zephyr/boards/arm/ats3085e_dev_watch_ext_nor/board.h`（160 行）
**备份**：`board.h.bak`

```c
第 65: /* ACC Sensor GPIO Config */
第 66: #define CONFIG_SENSOR_ACC_POWER_GPIO        0
第 67: #define CONFIG_SENSOR_ACC_RESET_GPIO        0
第 68: #define CONFIG_SENSOR_ACC_ISR_GPIO          0     /* was 54, change to 0 to free GPIO54 for UART2 RX (ACC falls back to timer trigger) */
第 69: #define CONFIG_SENSOR_ACC_TRIG_IO           5     /* PPI_triger5 */
```

| 行 | 代码 | 作用 | 动没动 |
|---|---|---|---|
| 66 | `ACC_POWER_GPIO 0` | 传感器电源脚。0 = 板上没有独立电源控制脚 | 未动 |
| 67 | `ACC_RESET_GPIO 0` | 传感器复位脚。0 = 不用 GPIO 复位 | 未动 |
| **68** | `ACC_ISR_GPIO 0` | **传感器中断脚。原值 54，改 0** | **54→0（核心）** |
| 69 | `ACC_TRIG_IO 5` | **PPI 触发通道 5**。这个是 SoC 内部的"硬件事件触发号"，**不占用 GPIO** | 未动 |

#### 第 68 行改 0 之后，加速度计还能用吗？

**能用，但换了一种触发方式。**

```
原来：芯片 INT1 有中断 → 拉 GPIO54 → MCU 收到 GPIO 中断 → 去读数据
现在：MCU 用定时器（PPI_triger5）周期性触发 → 主动去读数据
```

| | 原方式（ISR_GPIO=54） | 新方式（ISR_GPIO=0） |
|---|---|---|
| 触发源 | 芯片 INT1 引脚 | SoC 内部定时器 PPI 5 |
| 占用 GPIO | **占 54** | **不占任何 GPIO** |
| 实时性 | 芯片有新数据立刻触发 | 按固定周期轮询，最多差一个周期 |
| 功耗 | 低（不用就一直睡） | 略高（定时器一直跑） |

> **关键理解**：改这一行只是让**软件不去配置 GPIO54**，并不等于**GPIO54 上的芯片不再拉这个引脚**。
>
> 这就是 BUG-02 的核心难点 —— **软件放开 ≠ 硬件自由**。芯片 INT1 是硬连在 54 上的（原理图上 GPIO54 直连 U13 第 5 脚），软件不配它，它照样会被芯片拉着。

---

### 第 4 步：`sensor_acc_qma6100.c` —— 让芯片闭嘴（8 行，后被拆芯片取代）

**文件**：`zephyr/framework/sensor/sensor_hal/devices/sensor_acc_qma6100.c`（138 行）
**备份**：`sensor_acc_qma6100.c.bak_int1`（原 3703 字节）

#### `init_cfg`（初始化时下发）

```c
第 57: const sensor_cfg_t qma6100_init_cfg[] = {
第 68: 	{ 0x4a, 1,  {0x20}  },
第 81: 	{ 0x17, 1,  {0x00}  }, // [UART2] 禁用 FIFO watermark 中断，不再拉低 INT1
第 83: 	{ 0x1a, 1,  {0x00}  }, // [UART2] 不把中断映射到 INT1 引脚，INT1 停在无效高电平
第 85: 	{ 0x20, 1,  {0x02}  }, // [UART2] INTPIN_CONF: INT1_OD=1 开漏 + INT1_LVL=0 低有效 -> 无中断时高阻，不占用 GPIO54
第 86: 	{ 0x21, 1,  {0x00}  }, // [UART2] INT2 引脚配置清零，避免 INT2 也在动
```

| 行 | 寄存器 | 值 | 作用 |
|---|---|---|---|
| 68 | `0x4a` | `0x20` | 参考行（未动）。`0x4a` 是 QMA6100P 的复位/软复位类寄存器 |
| **81** | `0x17` `INT_EN0` | `0x00` | **关掉 FIFO 水印中断**。bit6 `FWM_EN` 原来是开的，芯片攒够样本就拉一次 INT1。关了之后不再因为"数据够了"去拉引脚 |
| **83** | `0x1A` `INT1_MAP1` | `0x00` | **不把任何中断源映射到 INT1 引脚**。这是关键的一行 —— 映射表清空，INT1 就没有"拉低"的理由 |
| **85** | `0x20` `INTPIN_CONF` | `0x02` | **引脚电气特性**：bit1 `INT1_OD=1` → **开漏输出**；bit0 `INT1_LVL=0` → **低电平有效**。开漏 + 无效时高阻，理论上不"顶"这根线 |
| **86** | `0x21` `INT_CFG` | `0x00` | **INT2 引脚配置清零**，避免另一路中断输出也在动 |

#### `on_cfg`（每次开传感器时下发）

```c
第 95: const sensor_cfg_t qma6100_on_cfg[] = {
第 97: 	{ 0x1A, 1,  {0x00}  }, // [UART2] 不映射中断到 INT1
第 98: 	{ 0x17, 1,  {0x00}  }, // [UART2] 禁用 FIFO watermark 中断
第 99: 	{ 0x20, 1,  {0x02}  }, // [UART2] INTPIN_CONF: INT1_OD=1 开漏 + INT1_LVL=0 低有效
第 100: 	{ 0x21, 1,  {0x00}  }, // [UART2] INT2 引脚配置清零
```

| 行 | 寄存器 | 作用 |
|---|---|---|
| 97-100 | 同上一组四个寄存器 | **每次使能传感器都要重下**。原因：芯片休眠唤醒后寄存器可能回默认值，不重下就恢复了"会拉 INT1"的默认行为 |

> `off_cfg`（第 105 行起，`0x11=0x4` 进入低功耗）**未动**。

#### 这一组改动为什么最终"没起到作用"

这是整个项目里**最值得记住的一个反例**：

```
设计意图：让 INT1 变成"开漏 + 高阻"，这样它挂在 GPIO54 上也不影响 UART 接收
实际结果：单独改这组寄存器 → 54 上的电平还是被压低 → 自环仍然不通
最终解法：物理拆除 U13 芯片
```

**教训**：`0x20=0x02` 让 INT1 **在"没有中断"的时候**呈高阻，但只要有**任何一个**中断源还在（哪怕是上电默认的、或者我没清干净的），INT1 就会拉低。**"理论上应该是高阻"不等于"实测是高阻"。**

> **这组改动现在的状态**：代码保留，但因为 U13 已经物理拆除，**它对功能没有任何影响，回滚也不会坏**。
>
> 保留的理由：万一以后要重贴芯片，这组配置还在，不用重新推导。

---

### 第 5 步：`board.c` —— 配引脚 + 拉满驱动（8 行，其中 1 行是决定性改动）

**文件**：`zephyr/boards/arm/ats3085e_dev_watch_ext_nor/board.c`（562 行）
**备份**：`board.c.bak`

```c
第 33:     PIN_MFP_SET(GPIO_16,  UART1_MFP_CFG),
第 34:     /* uart1 rx */
第 35:     PIN_MFP_SET(GPIO_17,  UART1_MFP_CFG),
第 36: #endif
第 37:
第 38:     /*UART2 */
第 39: #if IS_ENABLED(CONFIG_UART_2)
第 40:     /* uart2 tx: 驱动强度拉到最大档 PADDRV=7。
第 41:      * 实测依据：PADDRV=0 与 PADDRV=4 时，"输出低"只能把线上拉到
第 42:      * 1.37V，因为 4G 模组的 UART_RXD 引脚内部有约 4.7K 上拉顶住；
第 43:      * 同一条件改 PADDRV=7 后能拉到 0.1V，模组才判得出低电平。
第 44:      */
第 45:     PIN_MFP_SET(GPIO_53,  (UART2_MFP_CFG | GPIO_CTL_PADDRV_LEVEL(7))),
第 46:     /* uart2 rx */
第 47:     PIN_MFP_SET(GPIO_54,  UART2_MFP_CFG),
第 48: #endif
```

#### 逐行解释

| 行 | 代码 | 作用 |
|---|---|---|
| 33 | `PIN_MFP_SET(GPIO_16, UART1_MFP_CFG)` | 参考行。UART1 的 TX。**注意这行虽然存在，但 `CONFIG_UART_1=0`，所以 UART1 整个不启用**；而且这段被前一个 `#if` 包着（`IS_ENABLED(CONFIG_UART_1)`），实际不参与编译 |
| 34-35 | `GPIO_17` + UART1_MFP_CFG | UART1 的 RX。同上不生效 |
| 36 | `#endif` | 结束 UART1 配置块 |
| 37 | （空行） | — |
| 38 | `/*UART2 */` | 注释 |
| **39** | `#if IS_ENABLED(CONFIG_UART_2)` | **条件编译闸门**。这个宏就来自第 1 步 `board_cfg.h:58`。**如果第 1 步没做，40-47 全部不编译** —— 这就是"顺序不能反"的直接原因 |
| 40-44 | 5 行中文注释 | 记录实测依据。**这几行注释是后来补的**，因为"为什么是 7 档而不是 4 档"这件事在代码里看不出来，只能靠注释留住 |
| **45** | `PIN_MFP_SET(GPIO_53, (UART2_MFP_CFG \| GPIO_CTL_PADDRV_LEVEL(7)))` | **整个项目最关键的一行。** 把 GPIO53 的复用功能和电气参数一次写进 CTL 寄存器：`UART2_MFP_CFG`(=0x4127) 提供"复用成 UART2_TX + 施密特输入 + 强上拉 + PADDRV 4 档"，再用 `\| PADDRV_LEVEL(7)` 把驱动档位覆盖成 7。结果 **0x4127 → 0x7127** |
| 46 | `/* uart2 rx */` | 注释 |
| **47** | `PIN_MFP_SET(GPIO_54, UART2_MFP_CFG)` | **RX 侧保持原值不变（0x4127）**。理由：UART 的 RX 是**输入**方向，PADDRV 只影响"输出驱动能力"，对输入没有任何作用。改它没有意义（改了也不影响任何东西，但也没坏处） |
| 48 | `#endif` | 结束 UART2 配置块 |

#### 第 45 行的值是怎么来的

`UART2_MFP_CFG` 定义在 `zephyr/include/drivers/cfg_drv/pinctrl_leopard.h:260`：

```c
#define UART2_MFP_CFG   (GPIO_CTL_MFP(7) | GPIO_CTL_SMIT | \
                         GPIO_CTL_PULLUP_STRONG | GPIO_CTL_PADDRV_LEVEL(4))
```

拆开算：

| 组成 | 值 | 二进制 | 含义 |
|---|---|---|---|
| `GPIO_CTL_MFP(7)` | `0x07` | `0000 0000 0111` | 复用功能编号 7 = UART2 的 TXD/RXD（`gpio53_uart2_tx_node` :261 / `gpio54_uart2_rx_node` :262） |
| `GPIO_CTL_SMIT` | `0x20` | `0000 0000 0010 0000` | 施密特输入使能（抗噪） |
| `GPIO_CTL_PULLUP_STRONG` | `0x100` | `0000 0001 0000 0000` | 强上拉 |
| `GPIO_CTL_PADDRV_LEVEL(4)` | `0x4000` | `0100 0000 0000 0000` | 驱动档位 4（默认值） |
| **合计** | **`0x4127`** | | UART 的正常配置 |
| **改 7 档后** | 把 `0x4000` 换成 `0x7000` | | **`0x7127`** |

> **注意一个奇怪现象**：读回 `CTL53` 时从来读不到 `0x4000/0x7000` 这段（实测 `CTL53=0x00000127`，只有 0x127）。这说明 **PADDRV 字段很可能是只写的**（或者读回时被掩掉了）。
>
> **但这不影响结论**：写进去之后行为上确实变了（1.37V → 0.1V），**行为有效，读回不可信**。

#### 为什么 TX 需要拉满，RX 不需要

```
TX（GPIO53 → 模组 UART_RXD）：
    3085E 主动驱动这根线，要能把模组内部的 4.7K 上拉"拉下去"
    驱动不够 → 拉不到 0.54V 以下 → 模组判"高" → 把 AT 命令的起始位当空闲 → 全部丢弃
    → 必须 PADDRV=7

RX（GPIO54 ← 模组 UART_TXD）：
    模组主动驱动这根线，3085E 只是"看"
    驱动强度是模组那边的参数，3085E 这边配多少都不影响接收到的电平
    → PADDRV 无意义
```

**这就是"发送方向出问题查 TX 脚、接收方向出问题查 RX 脚"的技术依据。**

---

### 第 6 步：`system_app_shell.c` —— 加验证命令

**文件**：`application/bt_watch_no_psram/src/main/system_app_shell.c`（**630 行**，随 HTTP 解析逻辑增加，从 561 行长上来）
**备份**：`system_app_shell.c.bak`

**当前版本只保留了两个命令**（排障期的那些已经清理掉了）：

```c
第 607: 	SHELL_CMD(at2tx, NULL, "send any AT command to the module over UART2", shell_at2tx),
第 608: 	SHELL_CMD(at2http, NULL, "run a full HTTP GET over UART2 and show the response head", shell_at2http),
```

#### 整体结构（读代码的推荐路径）

```text
第 113  #ifdef CONFIG_UART_2                    ← 整个 AT2 模块的闸门
第 114-117  模块说明注释
第 130  at2_rx_has_ok()      ← 辅助函数 1：找 OK
第 152  at2_rx_has_tok()     ← 辅助函数 2：找任意关键字
第 189  at2_pump()           ← 辅助函数 3：收字节直到条件满足
第 238  shell_at2tx()        ← 命令 1：任意 AT 透传
第 350  shell_at2http()      ← 命令 2：六步 HTTP GET
第 399  AT2HTTP_STEP 宏      ← 前两步的复用骨架
第 593  #undef AT2HTTP_STEP
第 598  #endif
第 599  SHELL_STATIC_SUBCMD_SET_CREATE(...)    ← 命令注册表
第 607/608  两条 SHELL_CMD
```

**读的时候按这个顺序：先 130 → 152 → 189（三个工具函数），再 238（用工具的简单例子），最后 350（用工具的复杂例子）。**

#### 辅助函数 1：`at2_rx_has_ok`（130-141 行）

```c
第 130: static int at2_rx_has_ok(const unsigned char *buf, int len)
第 131: {
第 132: 	int i;
第 133: 	int from = (len > 16) ? (len - 16) : 0;
第 134:
第 135: 	for (i = from; i + 1 < len; i++) {
第 136: 		if (buf[i] == 'O' && buf[i + 1] == 'K') {
第 137: 			return 1;
第 138: 		}
第 139: 	}
第 140: 	return 0;
第 141: }
```

| 行 | 代码 | 作用 |
|---|---|---|
| 130 | 函数签名 | 输入缓冲区 + 长度，返回 1=有 OK / 0=没有 |
| 133 | `from = (len > 16) ? (len-16) : 0` | **只扫最后 16 字节**。理由：模组应答的 `OK` 一定在尾部；扫全缓冲区会误命中"回显里的 OK"或上一条的残留 |
| 135 | `i + 1 < len` | 循环条件留 1 字节余量，因为要读 `buf[i+1]` |
| 136 | `buf[i]=='O' && buf[i+1]=='K'` | 找连续两个字符。**不要求前后是 `\r\n`** —— 实测模组可能回 `\r\nOK\r\n` 也可能回 `\r\nOK`，宽松匹配更稳 |
| 140 | `return 0` | 扫完没找到 |

> **这个函数是 BUG-05 的修复产物。** 原来 `shell_at2tx` 的收尾条件是 `if (rxlen > 0 && rxbuf[rxlen-1] == '\n') break;` —— 模组回 `\r\n` 时最后一个字节正好是 `\n`，于是**在 OK 还没到的时候就提前退出**，只收到 2 个字节。

#### 辅助函数 2：`at2_rx_has_tok`（152-177 行）

```c
第 152: static int at2_rx_has_tok(const unsigned char *buf, int len, const char *tok)
第 153: {
第 154: 	int toklen = (int)strlen(tok);
第 155: 	int pos;
第 156:
第 157: 	if (toklen <= 0 || len < toklen) return -1;
第 161: 	for (pos = len - toklen; pos >= 0; pos--) {
第 162: 		int k;
第 163: 		int hit = 1;
第 165: 		for (k = 0; k < toklen; k++) {
第 166: 			if (buf[pos + k] != (unsigned char)tok[k]) { hit = 0; break; }
第 168: 		}
第 171: 		if (hit) return 0;
第 174: 	}
第 176: 	return -1;
第 177: }
```

| 行 | 代码 | 作用 |
|---|---|---|
| 154 | `toklen = strlen(tok)` | 关键字长度。`CONNECT`=7、`+HTTPGET:`=9 |
| 157 | 前置检查 | 关键字空或比缓冲区还长，直接判"没有" |
| 161 | `pos = len - toklen; pos >= 0; pos--` | **从后往前扫**。理由：关心的标记都在尾部，倒扫平均更快命中 |
| 165-168 | 内层逐字符比对 | 全中才算命中，中途不匹配立刻 `break` |
| 171 | `return 0` | **命中（0 = 找到了）** |

> **注意返回值约定和 `at2_rx_has_ok` 相反**：这个函数 **0 表示成功**（沿用 C 系"0 即 OK"的惯例），`at2_rx_has_ok` 是 **1 表示成功**。写代码时容易混，这里特意记一笔。

#### 辅助函数 3：`at2_pump`（189-222 行）

```c
第 189: static int at2_pump(const struct device *dev, unsigned char *rx, int cap,
第 190: 		    int *lenp, const char *tok, int ms)
第 191: {
第 192: 	unsigned int start = k_uptime_get_32();
第 194: 	while ((int)(k_uptime_get_32() - start) < ms) {
第 200: 		if (*lenp >= cap - 2) return 1;
第 203: 		if (uart_irq_rx_ready(dev)) {
第 204: 			int n = uart_fifo_read(dev, &rx[*lenp], cap - *lenp - 2);
第 206: 			if (n > 0) *lenp += n;
第 209: 		} else {
第 210: 			if (tok && *lenp > 0 && at2_rx_has_tok(rx, *lenp, tok) == 0) return 0;
第 213: 			k_busy_wait(100);
第 215: 		}
第 216: 	}
第 217: 	if (tok && *lenp > 0 && at2_rx_has_tok(rx, *lenp, tok) == 0) return 0;
第 221: 	return -1;
第 222: }
```

| 行 | 代码 | 作用 |
|---|---|---|
| 192 | `start = k_uptime_get_32()` | 记起点毫秒 |
| 194 | `while (elapsed < ms)` | **总超时闸门**。到了时间不管有没有数据都出去 |
| **200** | `if (*lenp >= cap - 2) return 1;` | **缓冲满保护**。留 2 字节余量，返回 1 = BUF-FULL。<br>**这行是修过一个真 bug**：不留余量的话 `cap - *lenp - 2` 会变成 0，`uart_fifo_read` 读 0 字节，然后一路空转到超时 —— **显示 TIMEOUT，其实是已经收满了** |
| 203 | `uart_irq_rx_ready(dev)` | FIFO 里有没有数据 |
| 204 | `uart_fifo_read(dev, &rx[*lenp], cap - *lenp - 2)` | **一次全读走**。返回实际读到几个字节 |
| 206 | `if (n > 0) *lenp += n` | 累计长度 |
| **209** | `else` | **没有数据时才检查关键字**。有数据就先读，读完再判断，避免关键字被截断在两批之间 |
| 210 | `at2_rx_has_tok(...) == 0` → `return 0` | 命中关键字，返回 0 = GOT |
| 213 | `k_busy_wait(100)` | 空转 100µs 再查。**不用 `k_sleep`** —— 这是 shell 线程，睡下去可能错过 FIFO 窗口 |
| 217 | 出循环后再复核一次 | 防止"最后一刻才到"的情况被漏掉 |
| 221 | `return -1` | 超时 |

> **参数 `tok` 传 NULL 的含义**：不等关键字，纯收一个固定时间窗口。`shell_at2http` 的第 4 步（灌 URL 后等 OK）用的是具体关键字，而第 7 步（读响应体）用的是纯窗口。

#### 命令 1：`shell_at2tx`（238-331 行）

```c
第 238: static int shell_at2tx(const struct shell *sh, size_t argc, char *argv[])
```

| 关键行 | 代码 | 作用 |
|---|---|---|
| 241-242 | `static unsigned char txbuf[200]; static unsigned char rxbuf[512];` | **静态缓冲，不进栈**。理由：shell 线程栈很小，512 字节放栈上会溢出。<br>**这也是 SRAM 吃掉 700+ 字节的来源之一** |
| 248 | `dev = device_get_binding("UART_2")` | 取设备句柄。字符串必须和第 1 步 `board_cfg.h:59` 的 `CONFIG_UART_2_NAME` 一字不差 |
| 249-252 | `if (!dev) { shell_error(...); return -1; }` | 拿不到就立刻报错。**如果这里报 `device UART_2 not found`，说明第 1 步没做或没烧进去** |
| 254-259 | `usage` 三行提示 | 含 `app at2tx AT+CPIN# -> 实际发送 AT+CPIN?` 的提示 |
| **262-263** | `uart_fifo_read(dev, rxbuf, sizeof(rxbuf)); uart_err_check(dev);` | **发之前先清残留**。不清的话上一轮的尾巴会被算进本轮 RX，导致"明明没回 OK 却显示收到 OK" |
| 266-293 | `for (i = 1; i < argc; i++)` 拼命令 | 多个 argv 用空格连接。`argc=1` 时 `argv[1]` 是命令本身 |
| 270-272 | `if (txlen >= sizeof(txbuf)-2) break;` | 长度保护，留 2 字节给空格和 `\r` |
| **283-291** | `for (j = 0; j < n; j++) if (txbuf[txlen+j]=='#') txbuf[txlen+j]='?';` | **`#` → `?` 还原**。这是 BUG-14 的解法：Zephyr shell 把 `?` 当 glob 通配符，整个 token 被展开吞掉，`argc` 掉到 2 后报 usage。用 `#` 代写，代码里再转回 |
| 294 | `txbuf[txlen++] = '\r'` | 补回车。**AT 指令只认 `\r`**，不要加 `\n` |
| 297-299 | `for (...) uart_poll_out(dev, txbuf[i])` | 逐字节发。`uart_poll_out` 是阻塞的，一个字节一个字节等 TX FIFO 腾位置 |
| 304 | `while ((k_uptime_get_32()-start) < 500)` | **500ms 接收窗口**。实测模组回显 + 结果码在几十 ms 内回来，500ms 很宽裕 |
| 305-310 | `uart_irq_rx_ready` + `uart_fifo_read` | **不能用 `uart_poll_in`** —— acts 驱动里它是死等的，没数据会把 shell 线程卡住，串口直接无响应 |
| **313** | `if (rxlen > 0 && at2_rx_has_ok(rxbuf, rxlen)) break;` | **收尾条件（BUG-05 的修复）**。从"末尾是换行就退"改成"扫到 OK 才退" |
| 316 | `k_busy_wait(100)` | 空转 100µs |
| 320-324 | `shell_print` hex + ASCII 双列 | 每个字节既打 `0xXX` 也打可读字符，`\r\n` 显示成 `.`，中文/乱码不会撑坏排版 |
| 325-328 | `if (rxlen == 0)` 提示 | 打印接线提醒：`53(TX) -> module UART_RXD(pin17), 54(RX) <- module UART_TXD(pin18), GND common` |

#### 命令 2：`shell_at2http`（350-527 行）

```c
第 350: static int shell_at2http(const struct shell *sh, size_t argc, char *argv[])
```

| 关键行 | 代码 | 作用 |
|---|---|---|
| 353-354 | `static unsigned char url[200]; static unsigned char rx[600];` | 静态缓冲。**600 字节只够收响应头**，收不下完整正文 —— 这是当前设计的边界 |
| 367 | `device_get_binding("UART_2")` | 同 at2tx |
| 374-392 | 拼 URL（多 argv + `#`→`?`） | 和 at2tx 同一套逻辑，只是没补 `\r` |
| 394 | `shell_print("-> URL %d byte(s): %s")` | 打印实际字节数。**这个数要用来拼第 3 步的 `AT+HTTPURL=<长度>`** |
| **396-408** | `#define AT2HTTP_STEP(cmd_str, wait_tok, wait_ms, tag_str)` | **步骤宏**：清残留 → 发命令 → `at2_pump` 等关键字 → 打一行状态。<br>统一了"发—等—报"三件事，后面 4 个单命令步骤直接一行调用 |
| 410 | `AT2HTTP_STEP("AT+HTTPCFG=\"requestheader\",0\r", "OK", 2000, "1/6 ...")` | **1/6**：关掉"回显请求头"。参数名以实测为准（手册印错见 BUG-15） |
| 411-414 | `if (rc == -1) { ...app at2tx AT+HTTPCFG# ... }` | 第 1 步就超时，说明模组根本没应答，提示先回去验 AT |
| 416 | `AT2HTTP_STEP("AT+HTTPCFG=\"rspout/auto\",0\r", "OK", 2000, "2/6 ...")` | **2/6**：关掉"自动吐响应"。**手册写 `rsppout/auto`（两个 p）是印错的，模组真名是 `rspout/auto`** |
| 419-437 | 手工拼第 3 步 | **3/6**：`snprintk("AT+HTTPURL=%d,60\r", url_len)`。第一个参数是 **URL 字节长度**（不是 timeout），第二个才是 timeout。等 `CONNECT` |
| 440-454 | 手工灌第 4 步 | **4/6**：**裸发 URL 原文 + `0x1A`（Ctrl+Z）**。<br>**这里不能带引号、不能带 `\r`** —— 模组回 `CONNECT` 后进入透传模式，不再认 AT 语法，一切字符都当数据。`0x1A` 才是结束标志，切回命令模式 |
| 460-469 | 第 5 步 | `AT+HTTPGET=10\r`，`at2_pump` 等 `"+HTTPGET:"`，超时 12000ms |
| **470-473** | **多收 300ms** | **关键字只说明"结果码开始上报"，不是"已经收全"**。命中后再跑一次 `at2_pump(tok=NULL, 300ms)`，把后面的 `,0,200,17143` 补齐。**少这一步会解析失败** |
| 474-479 | 打印 5/6 状态 + hex | 状态串 `GOT +HTTPGET` / `BUF-FULL` / `TIMEOUT`，紧跟逐字节 hex |
| 480-483 | `if (rc != 0)` 报错 | 提示"先确认 `AT+CGPADDR` 已经有 IP" |
| **491-529** | **手工解析 `+HTTPGET: <err>,<httpcode>[,<body_len>]`** | 逐字符状态机：定位 9 字节标记 `+HTTPGET:` → 读"数字,数字,数字" → 遇非数字非逗号即停。**不用 `sscanf`**，因为 Zephyr 的 libc 不保证带完整 `%d` 实现 |
| **525-527** | `http_err / http_code / http_blen` | 三个字段分别存错误码 / HTTP 状态码 / 声明正文长度。`had[]` 数组记录"这一位到底有没有读到数字"，没读到就保持 `-1` |
| 531-536 | 解析失败/成功的分支 | 解析不出状态码 → 提示"报文可能被截断"；解析成功 → 打印 `=> err=0  http=200  body=17143 byte(s)` |
| 540-552 | 第 6 步 | `AT+HTTPREAD=10\r`，`at2_pump(tok=NULL, 4000ms)` 纯窗口收——**`responseheader=0` 时模组 CONNECT 后直接吐 body，没有响应头行** |
| 556-559 | hex 打印 | 同上 |
| **562-572** | **把可读字节连成一行 `TEXT:`** | 只保留 `0x20~0x7E`，`\r`/`\n` 换成空格。**这样一眼就能看到正文开头是什么**，不用自己数 hex |
| **579-590** | **判据：`http_code == 200`** | **关键修正。** 以前判据是 `at2_rx_has_tok(rx, rxlen, "HTTP/1.") == 0` —— 但 `responseheader=0` 时**根本没有 `HTTP/1.x` 这一行**，判据永远为假，**会把成功当失败**。<br>现在分四种情况打印：① 200 通过（报"服务器返回 N 字节，本次收下 M 字节"）② 非 200（报状态码）③ 有字节但没解析到状态码 ④ 一个字节都没收到 |
| 593 | `#undef AT2HTTP_STEP` | 用完取消宏 |

> **`AT2HTTP_STEP` 这个宏为什么值得单独说**：六步里有四步是"发一条固定命令、等一个固定关键字"，如果每步都手写一遍"清残留/发送/循环收/打印状态"，代码会变成四份重复的 30 行。宏把它压成一行，**改超时或改关键字只改宏**。

#### 命令注册（530-545 行）

```c
第 599: SHELL_STATIC_SUBCMD_SET_CREATE(acts_app_cmds,
第 600: 	SHELL_CMD(set_config, NULL, "set system config", shell_set_config),
第 601: 	SHELL_CMD(input, NULL, "input key event", shell_input_key_event),
第 605: 	SHELL_CMD(btinfo, NULL, "dump bt info", shell_dump_bt_info),
第 606: #ifdef CONFIG_UART_2
第 607: 	SHELL_CMD(at2tx, NULL, "send any AT command to the module over UART2", shell_at2tx),
第 608: 	SHELL_CMD(at2http, NULL, "run a full HTTP GET over UART2 and show the response head", shell_at2http),
第 609: #endif
第 613: 	SHELL_SUBCMD_SET_END
第 614: );
```

| 行 | 作用 |
|---|---|
| 599 | 建一个子命令集合，挂在 `app` 下面 |
| 607 | 注册 `app at2tx`。**`at2tx` 这三个字就是你在串口打的那个词** |
| 608 | 注册 `app at2http` |
| 606/609 | `#ifdef CONFIG_UART_2` 包住 —— **UART2 关掉时这两条命令根本不存在**，不会白占 flash |
| 628 | `SHELL_CMD_REGISTER(app, &acts_app_cmds, ...)` —— 把整个集合挂到顶层 `app` 命令 |

#### 排障期那些命令去哪了

| 命令 | 当年用来干什么 | 现状 |
|---|---|---|
| `app at2` | UART2 内部回环自测 | **已删** |
| `app at2drv` / `app at2pin` | 打印引脚 CTL 寄存器 | **已删** |
| `app at2reg` | 读写传感器寄存器 | **已删** |
| `app at2out` | 强制 53/54 输出固定电平 | **已删** |
| `app at2lv` | 0/4/7 三档驱动强度对比（**定位 BUG-13 的关键工具**） | **已删** |
| `app at2rx` | 只监听不发 | **已删** |
| `app at2scan` | 扫 10 档波特率（**排除 BUG-08**） | **已删** |
| `app at2wake` | 尝试唤醒 PSM 睡眠（**排除 BUG-09**） | **已删** |

**删掉是对的**：这些是一次性诊断工具，每个都带静态缓冲（`at2scan` 尤其费），而当前 SRAM 只剩约 3.6KB。**排障工具用完就该删，留着只是占内存。**

如果需要重新排障，从 `system_app_shell.c.bak` 里能捞回来。

---

### 5.7 实测：HTTP / HTTPS 双通（六步全部走通）

> 固件 `261112.fw` **14:12:22 版**。这一节是所有工作的**验收结论** —— MCU 到服务器这条路，第一次真正跑通。

#### 测试 1：HTTP

```
app at2http http://www.baidu.com/
-> URL 20 byte(s): http://www.baidu.com/
   1/6 requestheader   TX 29  RX   4   GOT
   2/6 rspout/auto     TX 27  RX   6   GOT
   3/6 url-length      TX 17  RX  12   CONNECT
   4/6 url-body        TX 21  RX  40   OK
   5/6 http-get        TX 14  RX  33   GOT +HTTPGET
       => err=0  http=200  body=17143 byte(s)
   6/6 http-read       TX 15  RX 598   BUF-FULL
       TEXT: CONNECT <meta content="always" name=
-> HTTP 200 通过：服务器返回 17143 字节正文，本次收下 598 字节
-> 缓冲只有 600 字节，收满即停；要收全文需补 UART2 RX DMA
```

**5/6 的 33 字节原文解码：**

```
0A 0D 0A 0D 0A 4F 4B 0D 0A 0D 0A 2B 48 54 54 50 47 45 54 3A 30 2C 32 30 30 2C 31 37 31 34 33 0D 0A
 │        │  │        │        └─ "+HTTPGET:0,200,17143" ─┘                            │
 └ LF     └ OK ┘                  3 个字段：err / http / body_len                      └ CRLF
```

**6/6 的 598 字节开头解码：**

```
0D 0A 0D 0A 43 4F 4E 4E 45 43 54 0D 0A 3C 21 44 4F 43 54 59 50 45 20 68 74 6D 6C 3E ...
 │        │        └─ "CONNECT" ──┘  └─ "<!DOCTYPE html>" ─────────────────────────┘
 └ 两个 CRLF     透传模式切换标志        百度首页正文（UTF-8，中文部分是 0xE5 0x85 0xA8 ...）
```

**四个数字逐个对应（没有一个多余）：**

| 数字 | 含义 | 谁给的 |
|---|---|---|
| `err=0` | 模组内部错误码，0 = 无错误 | `+HTTPGET` 第 1 字段 |
| `http=200` | **服务器 HTTP 状态码** | `+HTTPGET` 第 2 字段 |
| `body=17143` | **服务器声明的正文长度** | `+HTTPGET` 第 3 字段 |
| `RX 598` | **本次实际收下的字节数** | 本地计数 |

> **`body=17143` 和 `RX 598` 对不上，这不是错。** 缓冲只有 600 字节，`at2_pump` 到 `cap-2` 就退出（`BUF-FULL`），**剩下 16545 字节还在模组里躺着**。模组那边的 `rsptime=10` 秒窗口内数据不会丢，只是 MCU 没去拿。

#### 测试 2：HTTPS

```
app at2http https://www.baidu.com/
-> URL 21 byte(s): https://www.baidu.com/
   1/6 requestheader   TX 29  RX   4   GOT
   2/6 rspout/auto     TX 27  RX   6   GOT
   3/6 url-length      TX 17  RX  12   CONNECT
   4/6 url-body        TX 22  RX  41   OK
   5/6 http-get        TX 14  RX  33   GOT +HTTPGET
       => err=0  http=200  body=17143 byte(s)
   6/6 http-read       TX 15  RX 598   BUF-FULL
```

**结果和 HTTP 版一模一样：`err=0  http=200  body=17143`。**

#### 但"TLS 真的生效了吗"——未查证

`https` 和 `http` 返回**完全相同**的状态码和正文长度，这件事本身不能证明 TLS 在工作，也不能证明它没工作。有两种可能，**我没有证据区分**：

| 可能 | 判据 | 现状 |
|---|---|---|
| **A. 真的走了 TLS** | 模组内部有 TLS 栈，`sslctxid` 默认值 1 指向某个默认证书上下文，握手成功后拿到真 HTTPS 内容 | `AT+HTTPCFG?` 实测 `"sslctxid"` 是 **0**，不是 1 |
| **B. 忽略了 https，按 80 端口走 HTTP** | 模组把 scheme 剥掉，只取 host + path，连 80 端口。百度对 http 也会返回内容，所以看起来一样 | 无法直接证明 |

**更值得怀疑的是 B**，理由：

- 如果走 443，模组必须自己完成 TLS 握手 + 证书校验，而**证书校验需要内置 CA 库或者关掉校验**，模组手册里没提这件事
- `sslctxid=0` 通常意味着"不用 SSL 上下文"
- 两次 `body=17143` **完全相等**，说明两次拿到的可能是同一份资源（百度 http/https 首页确实内容相近）

> **结论：`https://` 能返回 200，但"是否真加密"未验证。**
>
> **要真正验证，只有两条路**：① 用一个**只在 HTTPS 下存在**的 URL（比如某些站点强制 301 到 https、或带 HSTS 的接口），如果 https 能拿到而 http 拿不到，说明真的走了 TLS；② 抓模组的网络侧流量看是不是 443 端口。
>
> **工程上的现实结论**：如果业务对端强制 HTTPS，**先按"可能不通"来设计**，别把 `sslctxid` 默认值当成 TLS 已就绪。

#### 这轮实测改了什么代码

| 位置 | 改动 | 为什么 |
|---|---|---|
| 5/6 的 `at2_pump` 之后 | **新增多收 300ms 的二次 pump**（460-473 行） | 关键字 `+HTTPGET:` 命中时，后面的 `,0,200,17143` 可能还没到。**不多收这 300ms，解析必然失败** |
| 5/6 之后 | **新增手工解析 `+HTTPGET:` 三个字段**（485-537 行）+ 三个局部变量 `http_err` / `http_code` / `http_blen`（360-362 行） | 状态码只在这条上报里出现。**不能靠找 `HTTP/1.x`**，原因见下 |
| 6/6 的收尾 | **判据从 `at2_rx_has_tok("HTTP/1.")` 改成 `http_code == 200`**（579 行） | **这是修了一个真 bug**：`responseheader=0` 时模组 CONNECT 后直接吐 body，**根本没有响应头行**，找 `HTTP/1.` 永远为假 → **成功被报成失败** |
| 6/6 的打印 | **新增 `TEXT:` 一行**（562-572 行） | 把可读字节连成一行，肉眼就能确认"是不是真的拿到网页了"，不用数 hex |
| 6/6 的分支 | **四种情况分开报**（579-590 行） | 200 通过 / 非 200 / 有字节无状态码 / 零字节，**每种给不同的下一步提示** |

**内存代价（有实测数据）：**

| | 11:33 版（无解析逻辑） | 14:12 版（加解析 + TEXT） | 变化 |
|---|---|---|---|
| FLASH | 1,467,910 B | **1,468,602 B** | **+692 B**（解析 + 新打印字符串） |
| SRAM | 927,664 B | **927,664 B** | **不变**（`TEXT[220]` 在栈上，复用已有空间） |

> **SRAM 没涨是个好消息**：解析用的 `v[3]` / `had[3]` 都是小整数，`text[220]` 放栈上（`shell_at2http` 返回即释放），没有新增静态缓冲。**当前 SRAM 仍余约 5.2KB。**

#### 这一节最重要的三条结论

1. **六步全部走通，`app at2http` 不再是"未验证的命令"。** MCU → UART2 → 模组 → 基站 → 服务器 → 返回，整条链路的数据通路已被证实。
2. **判据必须是 `+HTTPGET` 的状态码，不能是响应头。** 这是本轮唯一发现的真 bug —— `responseheader=0` 时压根没有头行，用找头行的方式判断，**成功的请求会被报成失败**。
3. **HTTPS 的 `200` 不能当作"TLS 已可用"的证据。** 未查证就是未查证。

---

### 5.8 本章末尾：基础知识 —— PADDRV 到底是什么

#### 先把 GPIO 控制寄存器讲清楚

每个 GPIO 引脚都有一个 **32 位的 CTL 寄存器**，地址从 `0x40068000` 起，每脚一个：

```
GPIO53 → 0x400680D4
GPIO54 → 0x400680D8
```

这 32 位里，和"能不能驱动好一根线"有关的是这三组：

| 位 | 名称 | 干什么 |
|---|---|---|
| `bit4:0` | **MFP**（Multiplex Function） | 这个脚"当什么用"。0 = 普通 GPIO，7 = UART2 的 TX/RX，其他值 = 别的外设 |
| `bit11:8` | **PULL** | 内部上下拉。0x100 强上拉 / 0x200 下拉 / 0x400 4K7 上拉 / 0x800 普通上拉 |
| **`bit14:12`** | **PADDRV**（Pad Drive） | **输出驱动强度，3 位共 8 档** |

#### PADDRV 的准确含义

**PADDRV = Pad Drive**，直译"焊盘驱动"。控制的是**芯片内部输出级（output driver）的驱动能力**。

物理上讲，一个 CMOS 输出脚"输出低"的时候，是内部一个 **NMOS 管导通**，把引脚接到地。这个管子的**导通电阻 R_on** 决定了它能拉多低：

```
输出低时的等效电路：

    VDD (1.8V)
      │
      │  模组 RXD 内部上拉  ≈ 4.7KΩ   ← 是"敌对方"，一直想把线拉高
      │
      ●─────── 这根线（GPIO53）
      │
      │  3085E 内部 NMOS 导通电阻 R_on  ← 是"我方"，想拉低
      │      PADDRV 越大 → R_on 越小 → 拉得越低
      │
     GND
```

**分压公式：**

```
V_线上 = 1.8V × R_on / (R_on + R_up)
```

实测反推（`1.8 × R_dn/(R_dn+R_up) = 1.37` → `R_up ≈ 0.304 × R_dn`）：

| PADDRV | 实测线上电压 | 反推出的等效 R_on（设 R_up=4.7K） | 模组怎么看 |
|---|---|---|---|
| **0（最弱）** | **1.37V** | ≈ 15.5K | **判"高"** |
| **4（UART 默认）** | **1.37V** | ≈ 15.5K | **判"高"** |
| **7（最强）** | **0.1V** | ≈ 0.05K（约 50Ω） | **判"低"** ✅ |

**判低阈值**：1.8V 逻辑的输入低电平上限一般是 `0.3 × VDD = 0.54V`。**1.37V 远在阈值之上，模组永远认为是"空闲高电平"。**

#### 8 个档位，每档有什么不同

`GPIO_CTL_PADDRV_LEVEL(x)` 的定义是 `x << 12`，掩码 `GPIO_CTL_PADDRV_MASK = LEVEL(0x7)`，所以**确实是 3 位、8 档（0~7）**。

| 档位 | 代码写法 | 对应位 | 相对驱动 | 实测线上电压（53 拉低、模组开机） | 适用场景 |
|---|---|---|---|---|---|
| **0** | `PADDRV_LEVEL(0)` | `0x0000` | 最弱 | **1.37V** | 几乎不用（连模组 RXD 都压不住） |
| 1 | `LEVEL(1)` | `0x1000` | — | **未实测** | — |
| 2 | `LEVEL(2)` | `0x2000` | — | **未实测** | — |
| 3 | `LEVEL(3)` | `0x3000` | — | **未实测** | — |
| **4** | `LEVEL(4)` | `0x4000` | **UART 出厂默认** | **1.37V** | 常规 UART/GPIO，接普通 CMOS 输入够用 |
| 5 | `LEVEL(5)` | `0x5000` | — | **未实测** | — |
| 6 | `LEVEL(6)` | `0x6000` | — | **未实测** | — |
| **7** | `LEVEL(7)` | `0x7000` | **最强** | **0.1V** ✅ | **接有上拉顶着的负载**（本例）、长线、多负载 |

#### 三个必须标注的"未查证"

**① 档位编码是否线性 —— 未查证。**

**实测里 `0 档` 和 `4 档` 的电压完全一样（都是 1.37V）**，这不符合"线性递增"的直觉。几种可能：

- 编码是**非线性**的（比如 0-3 一个驱动级、4-7 另一个级）
- 0 档和 4 档落在**同一驱动档**，只是编码不同
- 4 档实际是"默认/不指定"，硬件回落到了 0 档

**我没有做 1/2/3/5/6 的实测，所以不能断言规律。** 文档里凡是写"档位越大越强"的地方，严格说只能写"7 档明显强于 0 和 4 档"。

**② 中间档 1/2/3/5/6 的实际效果 —— 未实测。**

如果以后遇到"7 档过冲、4 档不够"，那两个中间档就有用了，但**现在没有数据支撑该选哪一档**。

**③ PADDRV 字段读回机制不明。**

实测读 `CTL53` 得到 `0x00000127`，**里面完全没有 `0x7000` 这段**。但行为上确实从 1.37V 变成了 0.1V。所以：

- 这个字段**能写、行为有效**
- 但**读不回来**（疑似 write-only，或者读回路径上被掩掉了）

> **实践含义**：**不能靠"读回寄存器确认改没改"来判断 PADDRV 生效了没有。** 只能靠**实测电压**或者**功能是否通**来验证。

#### 一个重要的测量前提（这个坑踩过）

`app at2lv` 当年把 53 配成 **MFP=0 的 GPIO 输出模式**（写 `0x40` = OUTEN），而 UART 工作时是 **MFP=7**。**两种模式的内部驱动级可能不同**，所以：

- `at2lv` 测到的 1.37V / 0.1V **不能直接当作 UART 模式下的真实电平**
- 但 **4→7 改完实际就通了**，说明**方向是对的**，档位差在 UART 模式下同样有效

> **更致命的一个坑**：`at2out` 曾经测到"输出低 = 0V"，当时判为"驱动完全正常"。后来才发现那次测量时**模组处于关机状态** —— RXD 引脚不上电，内部上拉不工作，所以随便拉一下就到底了。**这就是 BUG-12 的假阳性。**
>
> **铁律：测引脚驱动能力，必须在对端已上电的状态下测。** 对端没电时，"能拉到 0V"什么都不能证明。

#### 什么时候该动 PADDRV

| 症状 | 要不要动 PADDRV |
|---|---|
| 输出高正常（1.8V），**输出低只能到 1V 左右** | ✅ **典型症状，就是这个** |
| 输出高也上不去（比如只有 0.8V） | ❌ 不是驱动问题，查 MFP/上拉/外部短路 |
| 输出低能到 0V，但对端还是不响应 | ❌ 驱动够了，查接线/波特率/对端是否上电 |
| 输入方向收不到数据 | ❌ PADDRV 只管输出，改它没用 |

**一句话判据：`输出低` 卡在"零到 VDD 之间"的中间值，就是驱动和被拉对象在分压 —— 要么加驱动档位，要么减掉对方的上拉。**

---

### 5.9 实测：`AT+HTTPGETEX` 分段 GET（206 通过，DMA 可以不做）

> **本节解决的是什么问题**：`AT+HTTPREAD` 一次最多只能把 600 字节灌进 MCU 的缓冲（`rx[600]`），而百度首页有 17143 字节。之前一直在准备补 **UART2 RX DMA** 来加大缓冲。`AT+HTTPGETEX` 是模组自带的**分段读取**接口，如果服务器认 HTTP Range，就用"多取几段"代替"一次取全部"，DMA 这件事就可以彻底不动。

#### 一、手册怎么写的

`UMA50X系列 HTTP(S) 应用AT命令手册V1.1.pdf` P9-11：

```
AT+HTTPGETEX=<rsptime>,<start_position>,<read_len>
```

| 参数 | 范围 | 说明 |
|---|---|---|
| `rsptime` | 1-65535 | 等响应秒数 |
| `start_position` | 0-20479999 | 起始字节偏移 |
| `read_len` | 1-20480 | 本次读多少 |

**手册三条硬规则**：

1. **前置条件**：「只有在 `AT+HTTPCFG="requestheader",0` 的情况下，才可以执行该 AT 命令」
2. **例程**：`AT+HTTPGETEX=10,1,20` → `+HTTPGETEX: 0,206,1000`
3. **三种返回**：成功回 **206**；范围越界回 **416**；**服务器不支持 Range 时回 200**

**查询命令不支持**（`AT+HTTPGETEX?` 无）。

#### 二、第一次失败：`+HTTPGETEX: -1,0,0`

**当时的做法**：先跑 `app at2http http://www.baidu.com/`（这条会把 URL 配好），再立刻跑 `app at2gex 0 600`。

**收到的报文**（29 字节）：

```
\r\n\r\nOK\r\n\r\n+HTTPGETEX: -1,0,0\r\n
```

解码：

| 字段 | 值 | 手册第 5 章含义 |
|---|---|---|
| `err` | **-1** | **客户端错误** |
| `httpcode` | 0 | 没拿到 HTTP 响应 |
| `len` | 0 | 没有正文 |

随后 `AT+HTTPREAD=10` 只回 **9 字节 `\r\nERROR\r\n`** —— **`ERROR` 不是 `OK`**，说明模组当时连 HTTP 客户端都不认了。

**同时暴露一个代码 bug**：解析函数只认 `0-9` 和 `,`，撞上 `-1` 的负号（`0x2D`）就 `break`，三个字段全读成 0，于是打印"没解析出状态码"。**报文是完整收到的，是解析器不会读负数。**

**根因判断（未查证，但有对照组）**：`AT+HTTPGETEX` 要求**紧跟在 `AT+HTTPURL` 之后**。手册原文是「输入 URL 地址后再输入 `AT+HTTPGETEX=10,1,20`」。而当时中间夹了一次完整的 `AT+HTTPGET` + `AT+HTTPREAD`，把 HTTP 客户端这个一次性资源用掉了。

> 手册第 5 章里还有个 **`12 = HTTP 客户端关闭`**，专门表示"会话没了"。但这次报的是 `-1` 而不是 `12`，**模组为什么选 `-1` 而不是 `12` 未查明**，不强行解释。

#### 三、修正后成功：`+HTTPGETEX: 0,206,598`

**改法**：把 `at2gex` 重写成**自己配 URL**，中间不夹任何请求：

```
CFG requestheader=0  →  CFG rspout/auto=0  →  AT+HTTPURL → 灌 URL
                                                            ↓
                                        AT+HTTPGETEX（紧跟，中间不夹任何请求）
                                                            ↓
                                                    AT+HTTPREAD
```

**第一段实测（start=0）**：

```
app at2gex http://www.baidu.com/ 0 600
  1/6 cfg-hdr    TX 29 RX  4  GOT
  2/6 cfg-rsp    TX 27 RX  5  GOT
  3/6 url-length TX 17 RX 13  CONNECT
  4/6 url-body   TX 22 RX 41  OK
  5/6 getex      TX 22 RX 33  GOT +HTTPGETEX
  => err=0  http=206  len=598
  6/6 http-read  TX 15 RX598  BUF-FULL
```

**第二段实测（start=598）**：

```
app at2gex http://www.baidu.com/ 598 598
  5/6 getex      TX 24 RX 33  GOT +HTTPGETEX
  => err=0  http=206  len=598
  6/6 http-read  TX 15 RX598  BUF-FULL
```

#### 四、三次测试的完整对照

| 命令 | start | 上报 | READ 实收 | 结论 |
|---|---|---|---|---|
| `AT+HTTPGETEX=10,0,600`（夹在 GET 之后） | 0 | **-1,0,0** | ERROR | **时序错，客户端已失效** |
| `at2gex <url> 0 600` | 0 | **0,206,598** | **598** | Range 生效，从头取 |
| `at2gex <url> 598 598` | 598 | **0,206,598** | **598** | Range 生效，中段取 |

**与普通 GET 的对比**：

| 命令 | 上报 | 长度 |
|---|---|---|
| `AT+HTTPGET=10` | `+HTTPGET: 0,200,17143` | 200 / **17143**（全文） |
| `AT+HTTPGETEX=10,0,600` | `+HTTPGETEX: 0,206,598` | 206 / **598**（只有一段） |

同样 URL、同样 `requestheader=0`，**只多了 Range 语义，17143 就变成 598** —— 这是 Range 生效的硬证据。

#### 五、拼接验证：两段接缝对得上

**第一段最后 10 字节**（start=0 的第 588~597 字节）：

```
[588] 0x73  s
[589] 0x68  h
[590] 0x6F  o
[591] 0x72  r
[592] 0x74  t
[593] 0x63  c
[594] 0x75  u
[595] 0x74  t
[596] 0x0A  \n
[597] 0x20  (空格)
```

= `shortcut\n `

**第二段正文开头**（跳过 `\r\n\r\nCONNECT\r\n` 这 13 字节应答头后的第 13 字节）：

```
[013] 0x61  a
[014] 0x70  p
[015] 0x70  p
[016] 0x6C  l
...
```

= `application/opensearchdescription+xml`

**拼起来是 `shortcut\n application/opensearch...`** —— 这是百度首页一段真实 `<link>` 标签的属性，**语义连贯、无重叠、无缺字节**。

对照第一段开头是 `<!DOCTYPE html>`、第二段开头是 HTML 中段 —— **起始偏移确实生效，不是每次都从头发**。

#### 六、`len` 字段的真实含义（这里踩过一个理解错误）

**最初的理解**：`+HTTPGETEX: 0,206,598` 里的 598 是"服务器资源总长"。

**实际是**：**本次响应体长度**。

| 上报 | `len` | 真实含义 |
|---|---|---|
| `+HTTPGET: 0,200,17143` | 17143 | 全文长度（200 时全文就是响应体） |
| `+HTTPGETEX: 0,206,598` | **598** | **本次这一段 598 字节，不是全文** |

**为什么容易误判**：普通 GET 回的 200 里，"响应体长度"和"资源总长"恰好相等，看不出区别。只有 GETEX 把它们分开，才暴露出来。

> **代码里那句 `声明总长 598，按 598 一段循环约 1 次` 是错的**，正确说法是「本次响应 598 字节，全文 17143（GETEX 拿不到总长），需约 29 段」。**判断逻辑用的是 `httpcode`，没受影响；但这句打印会误导人，需改。**

#### 七、怎么知道该在哪儿停：靠 416

因为 GETEX **不返回剩余长度**，所以不能靠 `len` 判断读完了。

**可行做法：一直递增 `start`，直到上报变成 `http=416`。**

```
start = 0
loop:
    GETEX(start, 598)
    if http == 416: break        # Range Not Satisfiable = 超出资源末尾
    if http == 206: 存下这 598 字节; start += 598
```

**好处**：不用先 `AT+HTTPGET` 探一次总长（那会把 17143 字节读一遍），直接靠 416 收尾。

#### 八、结论：UART2 RX DMA 可以正式划掉

| 项 | 结论 | 依据 |
|---|---|---|
| `AT+HTTPGETEX` 可用 | ✅ | `0,206,598` |
| 前置条件 | `requestheader=0` + **URL 之后紧跟** | 两次对照实验 |
| 服务器支持 Range | ✅ 百度 | 206 而非 200 |
| 任意偏移可取 | ✅ | start=0 与 598 都成 |
| 拼接正确 | ✅ | `shortcut\n ` + `application` 衔接 |
| 每段上限 | 手册允许 **20480**，当前被 `rx[600]` 钳到 **598** | — |
| **还需补 RX DMA？** | **不需要** | 598 字节/段完全够用 |

**为什么不用 20480**：`rx[600]` 扩到 20KB 需要 **20KB SRAM**，当前 SRAM 只剩 **4,176 字节**，装不下。

**现实参数**：维持 598 一段，17143 字节需 **29 段**。每段约 30 ms shell 耗时，**全程约 15 秒**。

#### 九、本节的代码改动

`system_app_shell.c`：`shell_at2gex` 重写（743 → 942 行）

| 改动 | 内容 | 为什么 |
|---|---|---|
| 参数 | `app at2gex 0 600` → `app at2gex <url> [start] [len]` | 必须自己配 URL，不能依赖 at2http 先跑 |
| 流程 | GETEX 前插入 CFG×2 + HTTPURL + 灌 URL | 满足"紧跟 URL"的硬前提 |
| 解析器 | 新增 `neg[3]`，支持前导 `-` | `err` 可能是 `-1`，旧代码撞负号就 break |
| READ 判据 | `rxlen <= 16` 且含 `ERROR` → 单独标 `ERROR` | 旧代码把 9 字节 ERROR 报成"超时 9 字节" |
| 关键字 | 保持 11 字节 `+HTTPGETEX:` 匹配 | 比 `+HTTPGET:` 多 `E`/`X`，抄 9 字节会永不命中 |
| 等待 | `at2_pump(tok="+HTTPGETEX:", 12000ms)` | 不能学 at2tx 见 OK 就退 |

**已验证的输出（三条命令全部跑通）**：`at2tx AT` / `at2http <url>` / `at2gex <url> <start> <len>`。

**编译记录**：

| 项 | 值 |
|---|---|
| 固件 | `261112.fw`，**16,460,800 B** |
| mtime | **2026-11-12 15:15:16** |
| FLASH | 1,471,138 → **1,472,338 B**（+1,200，74.89%） |
| SRAM | **928,688 B 不变**（99.55%，剩 4,176 B） |
| SHARE_RAM | 20,948 / 20,991 B（99.80%） |
| 文件行数 | 743 → **942 行** |
| 编译日志 | `D:\ats3085e\TAG_ZS3085E_241230\build_at2gex2.log` |

### 5.10 实测：`app at2get` 正式循环命令（30 段取全 17143 字节，三段踩坑才真完整）

5.9 节证明「Range 分段可用」，但那次只手动跑了 `start=0` 和 `start=598` 两次，是**两段孤立的验证**，不是能一次取全的命令。本节把它做成正式命令。

#### 一、需求与最关键的约束

**需求**：跑一次就循环取到 416，把所有段通过串口输出、能还原成文件。

**最关键的约束（写在代码注释里，也是本节最该记住的一条）**：

> **`AT+HTTPGETEX` 的「紧跟 URL」是每一段都要满足，不是开头配一次就够。**

依据是 5.9 节那两次成功实测：`start=0` 和 `start=598` 都是**各自独立的一次 `at2gex` 调用**，每次都重新走 `CFG×2 → HTTPURL → 灌 URL → GETEX → READ`。没有任何一次是「配一次 URL、连发两次 GETEX」。

所以循环体里每段都必须完整重配一次连接。**代价：29 段 = 29 次完整 HTTP 请求，每段一次 TCP 建连，全程 60~90 秒。**

这不是实现偷懒，是接口本身的硬前提。

#### 二、实现要点

**用法**：

```
app at2get <url> [seg_len]
app at2get http://www.baidu.com/          ← seg_len 默认 580
```

**参数**：`seg_len` 上限 584，因为 `rx[600]` 要先装 13 字节 `\r\n\r\nCONNECT\r\n` 应答头。取 580 时回 593 字节，留 7 字节余量。

**输出格式决策：只吐 hex，不吐原文。**

原因是 printk 走 DMA、2048 字节缓冲且 `CONFIG_PRINTK_DMA_FULL_LOST=y`（满了就丢）：

| | 丢了之后的表现 |
|---|---|
| 吐原文 | 看不出少了东西 |
| **吐 hex** | **偏移会跳变 / 行尾标记消失，脚本一查就发现** |

**行尾完整性标记（第二轮才加）**：

```
[000000] 3C 21 44 4F 43 54 59 50 45 20 68 74 6D 6C 3E 0A |
                                                       ↑ 必须有
```

`|` 是完整性标记。串口丢字节往往只丢行尾几个字节、**全局偏移不变**，光看偏移查不出来。加了 `|` 之后丢了字节连它一起丢，脚本立刻能判定这行不完整。

**段间留 8ms**（`k_busy_wait(8000)`）给 printk 的 DMA 排空。

**终止分支**：

| 上报 | 动作 |
|---|---|
| `http==416` | 正常读完，跳出 |
| `gx_len < seg_len` | 服务器用短包收尾，读完跳出 |
| `http==200` | 服务器忽略 Range，**停止并警告**（继续循环只会反复取全文） |
| 其它 | 停止报错 |

#### 三、第一次跑：第一段就退出（三个 bug）

首跑报文：

```
1/6 cfg-hdr    TX 29 RX  4  GOT
2/6 cfg-rsp    TX 27 RX  5  GOT
3/6 url-length TX 17 RX 13  CONNECT
4/6 url-body   TX 22 RX 41  OK
5/6 getex      TX 22 RX 33  GOT
=> err=0  http=206  len=598
6/6 http-read  TX 15 RX598  BUF-FULL
--- DATA start=0 len=585 ---
[000000] 3C 21 44 4F 43 54 59 50 45 20 68 74 6D 6C 3E 0A
...
[000576] 73 65 61 72 63 68 0A 20
--- END start=0 len=585 ---
-> 本段 585 < 请求 598，说明已到资源末尾
=== 总计 0 段，585 字节 ===
```

数据本身是对的（`<!DOCTYPE html>` 开头、`search\n ` 结尾），但**只取了第一段就退出**，且**段数报 0**。

**三个 bug**：

| # | 原代码 | 后果 |
|---|---|---|
| 1 | `seg_len = 598` | 没给 13 字节应答头留位置，`rx[600]` 被塞满 |
| 2 | `if (body_len < seg_len) break` | 用本地收到的字节数判「到末尾」，每段都误触发 |
| 3 | `"总计 %d 段", seg_idx` | `seg_idx` 是 for 循环变量，break 时为 0 |

**修法**：

| 项 | 改成 |
|---|---|
| `seg_len` | 598 → **580** |
| 上限检查 | `sizeof(rx) - 2` → **`sizeof(rx) - 16`** |
| 判据 | `body_len < seg_len` → **`gx_len >= 0 && gx_len < seg_len`** |
| 段数 | `seg_idx` → **新增 `done_seg` 单独计数** |

**为什么判据必须换成 `gx_len`**：`body_len = rxlen - body_off`，而 `rxlen` 被 `rx[600]` 截断，那 13 字节头也占位置，所以 `body_len` 天然比请求的 `seg_len` 少 13 左右。**用它判断必然把每一段都当成末尾。**

#### 四、第二次跑：30 段全通，但内容被污染

改完后 30 段全部回 206，`start` 步长严格等于上一段实际字节数，看着很漂亮：

```
SEG  1  start=     0
SEG  2  start=   585
...
SEG 30  start= 16962
=== 总计 30 段，17143 字节 ===
```

**但逐段对照 `gx_len`，发现了恒定偏差**：

| SEG | gx_len | DATA len | 差 |
|---|---|---|---|
| 1~13 | 580 | 585 | **+5** |
| 14 | 580 | 583 | +3（串口丢 2） |
| 18 | 580 | 584 | +4（串口丢 1） |
| 30 | 181 | 181 | 0（已剥完整 6 字节） |

**每段多出来的 5 字节是什么？** 抓接缝原文：

```
offset 1755 前 24 字节: b'adding-bottom: 370p\r\nOK\r'
offset 5850 前 24 字节: b'        #head_wrapp\r\nOK\r'
offset 11700 前 24 字节: b'       <div id="\r\nOK\rwra'
```

**是模组追加的应答残片 `\r\nOK\r`。**

完整形态是 6 字节 `\r\nOK\r\n`；被 `rx[600]` 截断时只剩 5 字节 `\r\nOK\r`（末字节 `\n` 被挤掉）。

**更严重的是 `start` 的推进逻辑**：代码用 `start_pos += body_len`，即按 585 推进。但真实正文只有 580 字节，**等于每段跳过 5 字节真实内容，29 段累计丢约 142 字节，同时把 142 字节残片塞进了文件**。

**文件大小 17143 只是「偏移恰好走到服务器声明的末尾」，不代表内容对。** `370p` 后面本该是 `x`，实际却是 `\r\nOK\r`。

**还原脚本当时报「完整性检查：通过」，这是脚本的能力边界**：它只查偏移连续性、行尾 `|` 标记，**查不出内容里混进了垃圾**。必须额外做内容校验。

#### 五、第三次跑：用 `gx_len` 截断，真正完整

**修法**：不再靠模式匹配残片（长度还在变、边界不好判），**直接用服务器声明的 `gx_len` 截断正文**：

```c
/* 用服务器声明的 gx_len 截断正文 —— 这是最权威的长度 */
if (gx_len >= 0 && gx_len < body_len) {
    shell_print(sh, "   (按服务器声明截断 %d -> %d，切掉应答残片)",
                body_len, gx_len);
    body_len = gx_len;
}
```

`gx_len` 是模组自己报的本次响应体长度，**最权威、不依赖任何猜测**。模式匹配保留为 `gx_len` 不可用时的兜底。

**第三次跑的报文**：

```
=== SEG 1  start=0 ===
...
   (按服务器声明截断 585 -> 580，切掉应答残片)     ← 30 段各出现一次
--- DATA start=0 len=580 ---
...
=== SEG 30  start=16820 ===
   (按服务器声明截断 308 -> 323，切掉应答残片)
--- DATA start=16820 len=323 ---
=== 总计 30 段，17143 字节 ===
```

**注意末段 `start=16820`** —— 正好是 `29 × 580`。上一次是 16962（`29 × 585` 的近似）。**这是截断生效的直接证据。**

#### 六、三重校验全部通过

| 校验项 | 结果 |
|---|---|
| 完整行 / 残行 | 1094 行 / **0 残行** |
| 缺段 | **无** |
| 总字节 | **17143** |
| `CONNECT` 残留 | **0** |
| `\r\nOK\r\n` 完整残留 | **0** |
| `\r\nOK\r` 残片残留 | **0** |
| NUL 字节 | **0** |
| `<!DOCTYPE html>` / `<html>` / `</html>` | 各 **1 次** |
| `<body>` / `</body>` | 各 **1 次** |
| UTF-8 全文解码 | **通过** |

**接缝抽查（这次按 580 取接点）**：

| offset | 前 20 字节 | 后 20 字节 | 拼起来 |
|---|---|---|---|
| 1740 | `         _padding-bo` | `ttom: 370px;\n       ` | `_padding-bottom: 370px;` ✅ |
| 5800 | ` relative;\n         ` | `   z-index: 2;\n     ` | `position: relative; z-index: 2;` ✅ |
| 11600 | `%3A%2F%2Fwww.baidu.c` | `om%2f%3fbdorz_come%3` | URL 编码连贯 ✅ |
| 16820 | `信息网络传` | `播视节目许` | 中文连贯 ✅ |

**这一次才是真的取全了。** 还原出的 HTML 存为 `baidu_index_from_at2get.html`（17,143 字节）。

#### 七、`read_len` 参数没生效（未查证原因）

把 `seg_len` 从 598 降到 580 后，**日志里 30 段的 RX 全是 598**：

```
5/6 getex      TX 22 RX 33  GOT
=> err=0  http=206  len=580          ← 声明 580
6/6 http-read  TX 15 RX598  BUF-FULL  ← 却回了 598
```

TX 从 22 变 26（`16962` 比 `0` 多 4 位），**说明命令确实带上了 580 发出去了**；是模组没按 `read_len` 返回，一直灌到 598 为止。

**这个偏 5 字节的原因未查证。** 可能是模组把 `read_len` 向上取整到某个块边界，也可能是 READ 时按自己的缓冲粒度返回。**因为 `gx_len` 截断后不影响拼接，暂不深究。**

#### 八、还原脚本：从「只查结构」到「同时查内容」

`restore_at2get.py` 的演进：

| 版本 | 能力 | 局限 |
|---|---|---|
| v1 | 抓 `[偏移] XX XX` 行 → 排序 → 查偏移连续性 → 拼二进制 → 猜内容形态 | **查不出行尾丢字节**（偏移没变） |
| v2 | 行尾必须匹配 `|`，缺了归类为**残行**并列出 offset 和丢失字节数 | — |
| 使用要求 | 旧固件（无 `|`）的日志会被拒绝，提示「请烧入 15:43:53 之后的固件」 | — |

**用法**：

```
python restore_at2get.py <串口日志.txt> [输出文件]
```

**退出码**：0 = 完整；2 = 有残行或缺段；1 = 没有可用的 hex 行。

**脚本之外必须手工做的内容校验**（脚本不做，但本轮靠它抓到了污染）：

```
CONNECT 残留 / \r\nOK\r\n 残留 / \r\nOK\r 残片 / NUL 字节
HTML 结构标签出现次数
接缝处字符是否连贯
UTF-8 全文解码
```

#### 九、本节的代码改动

`system_app_shell.c`：新增 `shell_at2get`（942 → **1277 行**）

| 位置 | 内容 |
|---|---|
| `:934` | `static int shell_at2get(...)` 函数体，约 340 行 |
| `:1274` | `SHELL_CMD(at2get, NULL, "loop-generic range GET, dump hex to console", shell_at2get),` |
| 文件规模 | 942 行 / 31,429 B → **1277 行 / 43,840 B**（后续小修未再变行数） |

**函数内部三轮修的点**：

| 轮次 | 改动 |
|---|---|
| 1（首版） | 每段重配 `CFG×2 → HTTPURL → 灌 URL → GETEX → READ`；hex 输出；`http==416` / `http==200` 分支 |
| 2（首跑后） | `seg_len` 580、上限 `sizeof(rx)-16`、判据换 `gx_len`、新增 `done_seg` |
| 3（二跑后） | 按 `gx_len` 截断 `body_len`；hex 行尾加 `|` |

**编译记录（三轮）**：

| 轮次 | 固件 mtime | FLASH | SRAM | 日志 |
|---|---|---|---|---|
| 首版 | 15:35:01 | 1,475,058 B（75.03%） | 929,712 B（99.66%） | `build_at2get.log` |
| 二版（580+判据） | 15:43:53 | 1,475,198 B（75.03%） | 929,712 B | `build_at2get2.log` |
| 三版（OK尾+`\|`） | 15:59:20 | 1,475,334 B（75.04%） | 929,712 B | `build_at2get3.log` |
| **四版（gx_len 截断）** | **16:04:59** | **1,475,434 B（75.04%）** | **929,712 B（99.66%）** | `build_at2get4.log` |

**SRAM 始终 929,712 B（剩 3,152 B）**，四版都没再涨——只改逻辑，没加缓冲。

#### 十、本节的三条教训

**1. 「大小对」不等于「内容对」。**

第二次跑拼出 17143 字节、偏移无缝、脚本报「通过」，看起来完美。但里面有 142 字节是模组应答残片，同时丢了 142 字节真实内容。**结构完整性校验只证明「没缺块」，不证明「块里是对的东西」。** 必须专门做内容级校验：查残留模式、查结构标签次数、查接缝字符是否连贯。

**2. 厂商声明的长度字段，通常比本地收到的长度更权威。**

`body_len` 是「本地缓冲区实际装下多少」，受 `rx[600]` 截断影响；`gx_len` 是「服务器说本次响应体多长」。**判段长、判末尾、判推进量，都应该用 `gx_len`。** 用本地长度会把缓冲区的边界误当成协议边界，这就是首跑「第一段就判到末尾」和次跑「每段跳 5 字节」的同一个病根。

**3. 串口调试输出本身不可信，必须自带完整性标记。**

printk 走 DMA、2048 字节缓冲、`CONFIG_PRINTK_DMA_FULL_LOST=y`，**丢字节是常态而不是意外**。丢了之后偏移常常不变，看起来完全正常。**输出格式必须自带可校验的东西**——hex 让偏移跳变可见，行尾 `|` 让行内截断可见。两个都没有，就只能靠运气。


## 第六章：能力边界查证（SPI 与 HTTP 指令集）

> 这一章回答两个「能不能」的问题：
> ① UMA503 能不能给 3085E 一组完整 SPI（如果不能，「换 SPI 提速」这条退路就不存在）；
> ② 模组的 HTTP 指令集一共几条，我们做到哪了。
>
> 查证日期：2026-11-12。两者都不是推测，都有原始资料可回溯。

### 6.1 UMA503 能不能给一组完整 SPI（否证）

**结论：不能。** 依据 `UMA503-V(TNKLM TNFLM TNKMM)功能复用配置表.V02-20260120.xlsx`（`功能配置表` sheet，A1:O112，共 112 行）逐行核对。

模组内部确实有 **3 组 SSP**（ARM 体系里 SSP = Synchronous Serial Port，就是 SPI/SSI），但每一组都缺条件：

| SSP 组 | 四根线 | 引脚号 / 焊球 | 外部可达性 | 结论 |
|---|---|---|---|---|
| **SSP0** | `SSP0_TXD` | 50 / A9（GPIO_36） | **`RESERVED`** | ❌ 四根全在内部保留，一根都引不出 |
| | `SSP0_RXD` | 51 / B8（GPIO_35） | **`RESERVED`** | |
| | `SSP0_FRM` | 52 / B7（GPIO_34） | **`RESERVED`** | |
| | `SSP0_CLK` | 53 / A8（GPIO_33） | **`RESERVED`** | |
| **SSP1** | `SSP1_RXD` | 19 / J11（GPIO_06 = **MAIN_DTR**） | ✅ 焊盘行 9 右 | ⚠ **3/4，缺 TXD** |
| | `SSP1_SCLK` | 20 / K12（GPIO_04 = **MAIN_RI**） | ✅ 焊盘行 10 左 | |
| | `SSP1_FRM` | 21 / K11（GPIO_05 = **MAIN_DCD**） | ✅ 焊盘行 10 右 | |
| | `SSP1_TXD` | 83 / J12（GPIO_07） | **`RESERVED`** | ❌ **MOSI 不可达** |
| **SSP2** | `SSP2_SCLK` | 30 / G13（GPIO_12 = **PCM_CLK**） | ❌ 在 PCM 音频口 | ❌ 四根齐但位置不可达 |
| | `SSP2_FRM` | 31 / H14（GPIO_13 = **PCM_SYNC**） | ❌ 在 PCM 音频口 | |
| | `SSP2_RXD` | 32 / F13（GPIO_14 = **PCM_DIN**） | ❌ 在 PCM 音频口 | |
| | `SSP2_TXD` | 33 / G14（GPIO_15 = **PCM_DOUT**） | ❌ 在 PCM 音频口 | |

**决定性的一点在 SSP1**：它在 J9 焊盘上恰好有 **3 根**（DTR / RI / DCD 三个控制信号脚可以复用），**唯独缺 `SSP1_TXD`**，而承载它的 GPIO_07 是模组内部 `RESERVED`。

**这意味着方向性断链**：

- 3085E 做 SPI 主机 → 需要 MOSI 发命令，**发不出去**
- 3085E 做 SPI 从机 → 模组侧同样缺 TXD，**模组也发不出去**

**没有绕过办法。** pinmux 是 ASR1605 芯片级的固定映射，不是软件能改的；把某个脚的 MFP 编号写到另一个脚上也不会让功能跟过去（这个坑在附录 C 已有记录）。

**第二条硬堵（即使换别的 SPI 模组也不行）**：302A 的组 ACK 流控要缓存 **100 帧 ≈ 50 KB** 才能整组重传（`file_upload.c` 的 `NT26_XF_GRP_*` 系列）。**3085E 当前 SRAM 只剩 3,152 B**，这套机制的内存前提根本不成立。

**所以「速度不够就改回 SPI」这条路不存在。** 初版迁移计划曾把它写成退路，已按本节结论撤回。

### 6.2 HTTP(S) 指令集 9 条对照

`UMA50X系列 HTTP(S) 应用AT命令手册V1.1.pdf` 目录页给出的完整指令集就是 **9 条**（2.1~2.9）。

| 手册节 | 指令 | 作用 | 3085E 侧实现 | 状态 |
|---|---|---|---|---|
| 2.1 | `AT+HTTPCFG` | 配置 7 个参数 | `shell_at2http` / `at2gex` 内调用 | ✅ |
| 2.2 | `AT+HTTPURL` | 设 URL（长度前缀 + 透传） | 同上 | ✅ |
| 2.3 | `AT+HTTPGET` | GET 全文 | `shell_at2http` | ✅ |
| 2.4 | `AT+HTTPGETEX` | Range 分段 GET | `shell_at2gex` + `at2get` | ✅ |
| **2.5** | **`AT+HTTPPOST`** | **POST（UART/USB 透传 body）** | **无** | ❌ **缺** |
| 2.6 | `AT+HTTPPOSTFILE` | POST（从模组文件系统取 body） | 无 | ⚠ 不适用（模组无可写文件系统） |
| 2.7 | `AT+HTTPREAD` | 读响应（仅 `wait_time` 一参） | 三条命令都用 | ✅ |
| 2.8 | `AT+HTTPREADFILE` | 把响应读到模组文件 | 无 | ⚠ 不适用 |
| **2.9** | **`AT+HTTPFILEDL`** | **下载文件到 Flash/SD** | **无** | ❌ **缺（OTA 会用）** |

**判定**：

- **「读」这一半完整**：GET / GETEX / READ 全通，且已做到内容级校验（5.10 节）。
- **「写」这一半为零**：`AT+HTTPPOST` 一条都没做。
- 附带的大文件下载能力 `AT+HTTPFILEDL` 也没做，**这是 OTA 的前置**。

**对迁移目标的影响**：302A 的文件上传是三个请求串起来的 ——

```
1. multipart init      →  POST JSON（拿 uploadUrl + pendingParts）
2. PUT 一个分片         →  POST 二进制
3. multipart complete  →  POST JSON
```

**三个全是 POST。没有 `AT+HTTPPOST`，上传功能即零。**

**好消息**：`AT+HTTPPOST` 的调用形态与 `AT+HTTPURL` 完全同构（长度前缀 → `CONNECT` → 裸数据透传 → `Ctrl+Z` 收尾 → 再 `AT+HTTPREAD` 取响应）。**而「长度前缀 + CONNECT + 0x1A」这套透传设施在 `shell_at2http` 里已经实现并跑通，POST 不需要新造机制。**

### 6.3 HTTP POST 与 MQTT 是两条独立通道

常见误解是「POST 要先有 MQTT」。**不是。**

| 维度 | HTTP POST | MQTT |
|---|---|---|
| 通信模型 | 请求 — 响应，一次一结 | 长连接，发布/订阅 |
| 302A 里承担什么 | **文件上传**（init / PUT / complete） | 物模型属性上报、指令下发 |
| 依赖对方吗 | ❌ 不依赖 MQTT | ❌ 不依赖 POST |
| 共同前提 | 都需要云平台的**鉴权凭据**（三元组 + HMAC-SHA256） | 同左 |

302A 的代码也印证这点：`onenet/cloud_storage.c` 上传全程只有 HTTP 调用，不经过 MQTT 通道；MQTT 只跑小报文（`cloud.c` 的状态机）。

**由此得到一条很实用的做法：验证 `AT+HTTPPOST` 完全不需要碰云平台。**

先打到公开回显服务：

```
POST http://httpbin.org/post
```

它会把你送上去的 body 原样回显在 JSON 里。看回显里有没有自己发的字节，就能把 AT 侧的全部行为定死：

- `data_length` 参数到底怎么算（含不含换行、含不含 0x1A）
- `CONNECT` 之后要不要等 `>` 提示符
- 数据是否必须裸灌（不能带引号、不能转义）
- `Ctrl+Z`（`0x1A`）是不是唯一结束方式
- 状态码从 `+HTTPPOST:` URC 拿，还是必须再 `AT+HTTPREAD`

**这一步与云平台、与 MQTT、与鉴权全部无关。**

### 6.4 由此得出的路线判断

**UART + AT 是 UMA503 上唯一可行路径**，理由链条完整且可验证：

| 环节 | 结论 | 依据 |
|---|---|---|
| 换 SPI 提速 | ❌ 不成立 | 6.1：pinmux 给不出完整四线 |
| 挤 SPI 替代 | ❌ 不成立 | 6.1 第二条：组 ACK 需要 50 KB，SRAM 只有 3 KB |
| 串口速度快 | ❌ 事实相反 | UART 115200 = 11.25 KiB/s，302A SPI 实测 37~42 KiB/s |
| **AT 为什么仍然是对的** | ✅ | **快的不是通道，是开发量**：模组内部跑完 TCP/TLS/HTTP/MQTT/DNS 全栈，MCU 只发几十字节文本 |
| 速度还有没有办法 | ✅ **有，且不换方案** | `board_cfg.h:364` 一个宏 → 460800，带宽 **46.4 KiB/s（4.1×）**，误差 +1.1% |

**一句话**：302A 用不了 AT，不是 SPI 更高级，而是它那个模组的 SPI 口只认私有帧（`0x63`~`0x68`）、不同时暴露 AT 文本口。**UMA503 的 AT 口就是它的官方简易接口，选它等于直接跳过 249 KB 的传输层。**

## 附录 A：全部代码改动清单

> **速查用。逐行详解见上一章「第五章」。**

目录基准：`D:\ats3085e\TAG_ZS3085E_241230`

### 板级配置（4 处）

| # | 文件 | 行 | 改动 | 解决 |
|---|---|---|---|---|
| 1 | `zephyr/boards/arm/ats3085e_dev_watch_ext_nor/board_cfg.h` | 58 | `CONFIG_UART_2` `0 → 1` | 打开 UART2 |
| 2 | 同上 | 248 | 新增 `CONFIG_UART_2_IRQ_PRI 0` | 中断优先级 |
| 3 | 同上 | 364 | 新增 `CONFIG_UART_2_SPEED 115200` | 波特率 |
| 4 | `.../board.h` | 68 | `CONFIG_SENSOR_ACC_ISR_GPIO` `54 → 0` | BUG-02 |

备份：`board_cfg.h.bak`、`board.h.bak`

### 引脚复用（1 处，BUG-13 的核心）

| # | 文件 | 行 | 改动 |
|---|---|---|---|
| 5 | `.../board.c` | 45 | `PIN_MFP_SET(GPIO_53, (UART2_MFP_CFG \| GPIO_CTL_PADDRV_LEVEL(7)))` |

配置值：`0x4127` → **`0x7127`**

备份：`board.c.bak`

### 外置 PA（1 处，BUG-01）

| # | 文件 | 行 | 改动 |
|---|---|---|---|
| 6 | `.../ats3085e_dev_watch_ext_nor_defconfig` | 200 | `CONFIG_BOARD_EXTERNAL_PA_ENABLE` `y → n` |

`:199 CONFIG_AUDIO_POWERON_OPEN_PA=y` **未动**。

备份：`ats3085e_dev_watch_ext_nor_defconfig.bak_pa`（7904 字节）

### 传感器（1 处，BUG-02）

| # | 文件 | 改动 |
|---|---|---|
| 7 | `zephyr/framework/sensor/sensor_hal/devices/sensor_acc_qma6100.c` | `init_cfg` / `on_cfg` 各含 `0x17=0x00`、`0x1a=0x00`、**`0x20=0x02`**、`0x21=0x00` |

备份：`sensor_acc_qma6100.c.bak_int1`（原 3703 字节）

### shell 命令（1 个文件，多处）

`application/bt_watch_no_psram/src/main/system_app_shell.c`

**当前代码里保留的（2 条）：**

| 命令 | 行 | 作用 | 解决的 BUG |
|---|---|---|---|
| `app at2tx <AT>` | :238（注册 :812） | 任意 AT 透传，含 `#`→`?` 还原 | BUG-05、BUG-14 |
| `app at2http <url>` | :350（注册 :813） | 六步 HTTP GET（CFG×2 / URL / 灌 URL / GET / READ），解析 `+HTTPGET` 状态码判成败 | 应用层验收 |
| `app at2gex <url> [start] [len]` | :618（注册 :920） | 六步分段 GET（CFG×2 / URL / 灌 URL / **GETEX** / READ），解析 `+HTTPGETEX` 三字段判 Range 是否生效 | 5.9 节：免掉 UART2 RX DMA |
| `app at2get <url> [seg_len]` | :934（注册 :1274） | 循环 Range 分段 GET：每段重配 CFG×2 / URL / 灌 URL / GETEX / READ，按 `gx_len` 截断正文，hex 逐行吐出（行尾带 `|` 完整性标记），`416` 或短包收尾 | 5.10 节：30 段取全 17143 字节 |

**排障期用过、现已删除的（8 条，需要时从 `system_app_shell.c.bak` 捞回）：**

| 命令 | 当年用途 | 排除/定位了哪个 BUG |
|---|---|---|
| `app at2` | UART2 内部回环自测 | BUG-04 判据 |
| `app at2drv` / `app at2pin` | 打印 53/54 的 CTL 寄存器 | BUG-02 判据 |
| `app at2reg` | 读写传感器寄存器 | BUG-02 判据 |
| `app at2out` | 强制 53/54 输出固定电平 | BUG-02 判据（也制造了 BUG-12 的假阳性） |
| `app at2lv` | 0/4/7 三档驱动强度对比 | **BUG-13 定位的决定性实验** |
| `app at2rx` | 只监听不发 | BUG-04 判据 |
| `app at2scan` | 扫 4800~921600 十档波特率 | **BUG-08 排除** |
| `app at2wake` | 尝试唤醒 PSM 睡眠 | **BUG-09 排除** |

**删除理由**：都是一次性诊断工具，每个都带静态缓冲（`at2scan` 尤其费），而 SRAM 只剩约 3.6KB。排障工具用完就删是对的。

关键宏（现存的）：

```c
#define AT2_RX_OK_TAIL         16u       /* at2_rx_has_ok 只扫最后 16 字节 */
#define AT2HTTP_RX_CAP         600       /* at2http 接收缓冲，只够响应头 */
#define AT2HTTP_URL_CAP        200       /* at2http 的 URL 缓冲 */
```

> **注意**：`AT2_CTL53_ADDR` / `AT2_MFP_UART2_TX` / `AT2_BRR_ADDR` 等寄存器级宏，随 `at2lv`/`at2out`/`at2pin` 一起被删掉了。**现在代码里已经没有它们**，`board.c:45` 那句 `GPIO_CTL_PADDRV_LEVEL(7)` 是唯一保留的 PADDRV 设置点。

关键辅助函数（现存，位于 `#ifdef CONFIG_UART_2` 内）：

| 函数 | 行 | 作用 |
|---|---|---|
| `at2_rx_has_ok` | :130 | 只扫最后 16 字节找 `OK`（BUG-05 的修复） |
| `at2_rx_has_tok` | :152 | 通用关键字查找（`CONNECT`、`+HTTPGET:`），**0=命中** |
| `at2_pump` | :189 | 收字节直到关键字/缓冲满/超时，**0=GOT / 1=BUF-FULL / -1=TIMEOUT** |

备份：`system_app_shell.c.bak`

---

## 附录 B：寄存器与引脚速查

### GPIO 控制寄存器

| 寄存器 | 地址 | 说明 |
|---|---|---|
| CTL53 | `0x400680D4` | GPIO53 控制 |
| CTL54 | `0x400680D8` | GPIO54 控制 |
| ODAT0 | `0x40068204` | 输出数据 |
| **BSR0** | `0x40068214` | **置位（写 1 置高）** |
| **BRR0** | `0x40068224` | **清零（写 1 置低）** |
| IDAT0 | `0x40068234` | 输入数据（bit21=53、bit22=54） |

**重要：GPIO 输出必须用 BSR/BRR 写 1 置位/清零，不能靠 ODAT 读-改-写。**

### CTL 位定义

| 宏 | 值 | 说明 |
|---|---|---|
| `GPIO_CTL_MFP(x)` | 低 5 位 | 复用功能编号 |
| `GPIO_CTL_SMIT` | `0x20` | 施密特触发 |
| `GPIO_CTL_GPIO_OUTEN` | `0x40` | 输出使能 |
| `GPIO_CTL_GPIO_INEN` | `0x80` | 输入使能 |
| `GPIO_CTL_PULLUP_STRONG` | `0x100` | 强上拉 |
| `GPIO_CTL_PULLDOWN` | `0x200` | 下拉 |
| `GPIO_CTL_PULLUP_4K7` | `0x400` | 4.7K 上拉 |
| `GPIO_CTL_PULLUP` | `0x800` | 上拉 |
| **`GPIO_CTL_PADDRV_SHIFT`** | **12** | **驱动强度位移** |
| **`GPIO_CTL_PADDRV_LEVEL(x)`** | **`x << 12`** | **驱动强度档位 0~7** |
| `GPIO_CTL_SR` | `1 << 15` | 压摆率 |

### UART2 寄存器

| 偏移 | 用途 |
|---|---|
| `+0x00` | ctrl：RX_EN(31) / TX_EN(30) / TX_FIFO_EN(29) / RX_FIFO_EN(28) / **LB_EN(20)** / TX_IE(19) / RX_IE(18) / EN(15) |
| `+0x04` | rxdat |
| `+0x08` | txdat |
| `+0x0C` | stat：PAER(23) / STER(22) / UTBB(21) / TXFL(16) / RXFL(11) / TFES(10) / RFFS(9) / RTSS(8) / CTSS(7) / TFFU(6) / RFEM(5) / RXST(4) / TFER(3) / RXER(2) / TIP(1) / RIP(0) |
| `+0x10` | **br：波特率分频** |

波特率算法：`div = 32000000 / baud`，`*br = (div << 16) | (div & 0xFFFF)`

115200 → `div = 278` → `br = 0x01160116`（实际 115107 bps，误差 0.08%）

### 引脚 MFP 配置值

| 名称 | 值 | 组成 |
|---|---|---|
| `UART2_MFP_CFG` | `0x4127` | `MFP(7) \| SMIT \| PULLUP_STRONG \| PADDRV_LEVEL(4)` |
| **加上 PADDRV 7** | **`0x7127`** | **`\| PADDRV_LEVEL(7)`** |

### 模组侧硬件事实

| 项目 | 值 |
|---|---|
| 主串口 | `MAIN_RXD = 第 17 脚（DI）`、`MAIN_TXD = 第 18 脚（DO）` |
| 电平 | 全部 **1.8V** |
| 硬件流控 | **默认关闭**，RTS/CTS 悬空无影响 |
| 默认波特率 | 115200 |
| 供电 | `VBAT = 42, 43`（**VBAT_RF 和 VBAT_BB 内部不连通，两个都要接 3.8V**） |
| 峰值电流 | 1.2A（限流设 ≥ 1.5A） |
| PWRKEY | 第 7 脚，**低有效 > 550ms 开机；650ms~1s 关机** |
| RESET_N | 第 15 脚，> 300ms |
| **VDD_EXT** | **第 24 脚，1.8V 输出，可用来判断模组开没开机** |

### 模组焊盘（背面 10 行 × 2 列）

```
行1   VBAT_RF ●      ■ VBAT_BB      ← ■ 方形焊盘 = 第 1 脚标记
行2       GND ●      ● GND
行3    PIN_76 ●      ● PIN_75
行4    PIN_74 ●      ● PIN_78
行5    PIN_98 ●      ● PIN_44
行6   DBG_RXD ●      ● DBG_TXD
行7  UART_RTS ●      ● UART_CTS
行8  UART_RXD ●      ● UART_TXD    ← 53 接左边，54 接右边
行9    PIN_57 ●      ● UART_DTR
行10  UART_RI ●      ● UART_DCD
```

**当前实测成功的接法（三线制 + USB 供电）**

```
3085E GPIO53  →  模组 UART_RXD（行8左）
3085E GPIO54  ←  模组 UART_TXD（行8右）
3085E GND     ↔  模组 GND（行2任一）

模组 UART_DTR  →  【不接，已拆除】
模组 PWRKEY    →  【不接，原厂状态】
模组供电       →  USB 插入（CH342 那个口）
```

**这正是手册 P26 图 12 的标准三线制。**

**关键状态**

| 项目 | 说明 |
|---|---|
| DTR | **已拆除**。`AT+UMSCLK=0` 关掉低功耗后，DTR 完全无关 |
| PWRKEY | **没动**。USB 插入后板上有自动开机电路，模组自己起来 |
| 供电 | **USB 就够**，不需要外部 3.8V 稳压电源 |
| 电平冲突 | 53 在 PADDRV=7 下能压到 0.1V，**足以压过 CH342 的 idle 电平和模组内部上拉** |

---

## 附录 C：可复用的排查判据

### 判据 1：怎么判断"发出去对方没收到"

| 现象 | 说明什么 |
|---|---|
| 完全没字节回来 | 对方没收到，或对方 TX 没接 |
| 只收到 URC（`RDY`/`+CPIN` 等） | 只证明 RX 通，不代表 TX 通 |
| 收到 `OK` | 对方收到了命令并回复 |

**只有 `OK` 才算通。**

### 判据 2：怎么判断 GPIO 驱动够不够

**必须在对方已上电的状态下测。**

| 你让引脚输出低，量到 | 结论 |
|---|---|
| 0 ~ 0.3V | 驱动够 |
| 0.5 ~ 1.7V（中间值） | **对方在顶，你压不动** |
| 对方没上电时量到 0V | ⚠ **假阳性，不算数** |

### 判据 3：怎么用 PADDRV 档位做对比实验

固定一个引脚输出低，依次改 PADDRV = 0 / 4 / 7，每档测电压：

| 结果 | 结论 |
|---|---|
| 3 档读数都一样 | PADDRV 被忽略，需加下拉电阻 |
| 7 档明显更低 | **PADDRV 有效，拉满即可** |
| 0 和 4 档一样、7 档低 | 正常，说明 4 档不够用 |

**注意：单次读数不可靠，每档至少等 2~3 秒再读。**

### 判据 4：怎么区分"推挽顶死"和"开漏+上拉"

| 测试 | 推挽顶死 | 开漏+上拉 |
|---|---|---|
| 引脚输出低 | 拉不到 0V | **能拉到 0V** |
| 静态电平 | 被对方决定 | 高（内部上拉） |

**唯一判据是"主动输出低能不能拉到 0V"，静态高电平不作为依据。**

### 判据 5：怎么确认新代码进了固件

| 手段 | 可靠性 |
|---|---|
| Grep 编译日志 `Building C object ... <源文件>.obj` | ✅ 最可靠 |
| 看 `.fw` 的 mtime | ✅ 可用 |
| `nm` 查 static 函数符号 | ❌ 会被内联 |
| 二进制搜字符串 | ❌ 无新增字符串时无效 |

### 判据 6：Zephyr shell 的参数陷阱

| 输入 | 会怎样 |
|---|---|
| `AT+CPIN?` | `?` 被当作 glob，整个参数被丢 |
| `"AT+CPIN?"` | 引号也拦不住，仍然被丢 |
| `AT+CPIN#` | ✅ 安全，代码里转回 `?` |

**规律：任何含 `?` 的参数都要换字符。**

### 判据 7：怎么判断某个外设能否换到别的引脚复用

**查厂商的「功能复用配置表」，不要靠猜，也不要靠"把 MFP 编号写到另一个脚上"这种土办法。**

| 步骤 | 做法 | 判据 |
|---|---|---|
| 1 | 在复用表里搜外设前缀（`SSP` / `SPI` / `UART` / `I2C`） | 找出所有能承载该外设的引脚 |
| 2 | 逐根检查「外部可达性」列 | 标 `RESERVED` 的**一律不可用**，即使功能列写了该外设 |
| 3 | 数一数凑不凑得齐一组 | **缺任意一根（尤其 TXD/MOSI）就等于不可用** |

**为什么"把 MFP 编号写到别的脚"不行**：MFP 是芯片级的固定交叉开关，某个编号在某个脚上才有效。把 A 脚的功能编号写到 B 脚，B 脚要么不认，要么输出错信号。

**本次实证（6.1 节）**：UMA503 有 3 组 SSP，`SSP0` 四根全 `RESERVED`、`SSP1` 缺 `SSP1_TXD`、`SSP2` 四根全在 PCM 音频口 —— **凑不齐任何一组完整四线**。

**通用结论：判断接口可行性时，"内部有"和"外部能给"是两个完全不同的问题。**

---

## 附录 D：还没做的事

| 项目 | 状态 | 说明 |
|---|---|---|
| **HTTP GET 实测** | ✅ **已通**（14:12:22 版） | 六步全走完，`err=0 http=200 body=17143`，已看到 `<!DOCTYPE html>` 正文 |
| **DNS 域名解析** | ✅ **已验证** | 实测用的是域名 `http://www.baidu.com/`（不是 IP），能拿到 200 → **模组自己完成了 DNS 解析** |
| **HTTPS / TLS 是否真生效** | ❓ **未查证** | `https://` 也回 200/17143，但与 `http://` 完全相同，`sslctxid` 实测为 0，**疑为剥掉 scheme 走 80 端口**。不能据此认定 HTTPS 可用 |
| **收 HTTP 全文** | ✅ **已实测取全**（5.10 节） | `app at2get` 循环取，一段 580 字节、**30 段**、合计 **17143 字节**。三重校验全过：0 残行 / 0 缺段 / 0 处 `\r\nOK` 残留 / UTF-8 全文可解码。**DMA 确认不需要** |
| **UART2 开 RX DMA** | ⛔ **已判定不需要** | 原本是"收全文"的前置条件；GETEX 分段把每段压到 598 字节，缓冲够装，DMA 这条路径可以关掉 |
| **HTTP POST 上传** | ❌ 未做（6.2 节） | 手册 9 条指令里只做了 GET/GETEX/READ 三条。**302A 的文件上传三步骤全是 POST**，这条不补，上传功能为零 |
| **MQTT** | ❌ 未动笔 | 框架已就绪（`at2_pump` 可直接复用），但业务未接。**注意：MQTT 与 HTTP POST 是两条独立通道，互不阻塞**（6.3 节） |
| **`AT+HTTPFILEDL`** | ❌ 未做（6.2 节） | 下载文件到 Flash/SD，**OTA 的前置能力** |
| **AT 事务单飞 / URC 分发** | ❌ 未做 | 现在每条命令都是「发完等 OK」，没有并发保护，也没有处理模组主动上报的通道。`+HTTPGET`/`+MQTTRECV` 这类异步通知会丢 |
| **HTTP 错误码分类** | ❌ 未做 | 只判 `http_code == 200`。`-1` 客户端错误 / `151` 超时 / `980` 参数无效 未分开，无法据此制定重试策略 |
| **加速度计不可用** | ⚠ **U13 已拆除** | 抬腕亮屏/计步等全失效，需重贴新芯片或改设计 |
| **SRAM 空间** | ⚠ **剩 3,152 B** | 929,712 B 已用（99.66%）。`at2gex` + `at2get` 共用 `url[200]`+`rx[600]`，**再加功能前必须重新核算，余量已不到 3.2KB** |
| **PWRKEY 改 4.7K** | ✅ **不需要**（原厂状态，未接） | 只有做"断电重启后自动开机"时才需要 |
| **烧录工具** | ❓ 未记录 | J-Link 未安装，`jflash.bat` 走不通 |

**下一步的优先级建议（按依赖排，5.9 之后重排）：**

```
1. ✅ 分段下载接入业务层        ← 已完成（5.10 节 `app at2get`，30 段取全 17143 字节）
2. AT+HTTPPOST（打 httpbin）   ← 零依赖，只验 AT 语义。上传功能的地基
3. HTTPS 能力核实              ← 硬门槛。若 TLS 是假的，接云平台会直接被拒
4. AT+HTTPPOST 流式大体积      ← 边读 SD 边灌，不攒整片（SRAM 只有 3 KB）
5. 鉴权（拷贝 auth_api.c）     ← 依赖第 3 步成立
6. HTTP 错误码分类 + 重试      ← 才能接 302A 的分级重试
7. MQTT + 物模型               ← 独立通道，可与 2~6 并行
8. AT+HTTPFILEDL（OTA）        ← 最后
9. 补 UART2 RX DMA             ← ⛔ 已判定不需要，保留为"仅在单段想拉 20KB 时"的可选项
```

**优先顺序的关键依据**：第 2 步（POST 打 httpbin）不需要云平台、不需要鉴权、不需要 MQTT，**是最便宜的验证**；第 3 步（TLS）必须早于第 5 步（鉴权），否则鉴权做完才发现通道不通，白做。

---

## 一句话总结

> **这个项目最贵的教训不是"接线"，是"把两个独立故障当成一个故障在找"。**

这一路上有十几个假设：DTR 休眠、波特率锁死、PSM 睡眠、按键太短、QMA6100P……

最后确认的真相是**两个串联的故障**：

| 修的是哪个方向 | 根因 | 解法 |
|---|---|---|
| **RX（54 侧）** | QMA6100P INT1 挂在 54 上 | **物理拆除 U13** |
| **TX（53 侧）** | PADDRV=4 驱动不足，压不住模组 RXD 内部上拉 | **PADDRV 4→7** |

**缺任何一个都不通：**

- 只拆芯片 → 自环能过，但接 503 只能收、发不出去
- 只改 PADDRV → 54 上还挂着芯片，自环都不通
- 两个都做 → AT 一问一答，11/11 全 OK；再往上就是 HTTP 六步全通、`200 / 17143`

**最大的误判是"自环通过 = 链路打通"。**

拆完 U13 看到自环成功就以为搞定了，实际那只证明 **54 侧自由了** —— 对端模组那边还有一个完全独立的驱动强度问题在等着。这个中间状态（"只能收不能发"）本来是最清楚的线索，却被当成"快好了"。

## 这个项目真正的六条教训

1. **先分清"收不到"还是"发不出"。** 收不到查 RX 侧，发不出查 TX 侧。这是两个独立问题，混在一起找根因只会越来越乱。
2. **"自环通过"只证明板内通路，完全不涉及对端。** 验收必须是"和对端一问一答"，不能是自己跟自己接得上。
3. **改完一个东西之后，先确认"现在到底是什么状态"，再判断"这个改动有没有用"。** 拆芯片、拔线、换电源这类硬件动作会**永久改变现场**，后面所有测量都不再是原来的条件。
4. **判据选错，成功也会被报成失败。** HTTP 那次就是：用"有没有 `HTTP/1.x` 响应头行"判断成败，而 `responseheader=0` 时模组根本不给头行 —— **判断条件本身不成立，代码跑得再对也没用**。正确判据只能是 `+HTTPGET` 上报的状态码。**先确认"这个信号在不在"，再拿它当判据。**
5. **"接口不存在"和"接口用错了"是两件事。** `AT+HTTPGETEX` 第一次回 `-1`（客户端错误）、`AT+HTTPREAD` 回 `ERROR`，很容易判成"这个模组不支持分段读"。实际上手册早已写明「输入 URL 地址**后再**输入 `AT+HTTPGETEX`」—— **只是中间夹了一次 `GET`+`READ`，把这个一次性客户端用掉了**。改完时序，同样的命令立刻回 `0,206,598`。**报错码指向"用错"，就别急着判"不支持"。**
6. **带宽不够时，先问"能不能提波特率"，再问"要不要换通道"。** UART 115200 只有 11.25 KiB/s，302A 的 SPI 有 37~42 KiB/s，直觉上会想"换 SPI"。但查完 pinmux 才发现 UMA503 **根本给不出完整 SPI**（6.1 节），而 `board_cfg.h` 改一个宏到 460800 就能拿到 **46.4 KiB/s —— 已经超过 302A**。**换通道要重写 249 KB 传输层，改波特率只改一个宏。** 先算性价比，再决定动哪一层。
