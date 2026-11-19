# ATS3085E SPI V11：硬件循环 DMA 交付说明

日期：2026-11-18，Asia/Shanghai。

> 当日实机补充：首版 V11 短测通过但 500 块长测失败，另有重启后的首块 CRC 错误。已交付 ATS V11A 修复及诊断包，配套 UMA V11 不变。详见 [V11A 结果说明](SPI_HARDWARE_CIRCULAR_V11A_RESULT_20261118.md)。以下保留首版交付记录。

两端代码已修改，ATS/UMA SDK 编译成功，共 180 项软件测试通过；尚未烧录、上板或取得 V11 吞吐结果。完整设计、硬件资料依据、错误处理和验收说明见 UMA 项目：

`D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644/docs/SPI_HARDWARE_CIRCULAR_V11_20261118.md`

## 实现范围

| 方向 | 本轮实现 |
|---|---|
| UMA503 TX / RX | 各两个硬件描述符形成 A → B → A 环，正常块间不重装、不重启 |
| ATS3085E RX | 硬件 RELOAD，1024 字节环形区分为两个 512 字节半区，每条命令启动一次 |
| ATS3085E TX | 有限 512 字节 DMA，每块配置和启动，以限定 SCK 数量 |

主机全双工模式下，`SPI2_BC` 不能限制循环 TX 的时钟长度，所以没有把主机 TX 也改为循环。硬件循环不意味着无限连续时钟；仍保留每块一个 READY 许可、CRC/序号检查和 cache 所有权管理。

V11-D 参考稿把旧代码的 `NODESC` 配置误当成硬件能力限制。UMA 本地 ASR1605 手册 §11.3.2 明确支持描述符获取；新实现按该机制建立硬件环，而不是每块软件重新启动 DMA。

线上块为 512 字节，包含两个 256 字节逻辑帧，有效载荷 480 字节。由于从机 TX FIFO 会预取下一槽，V11 确认延迟为两块；请求确认 N 块，实际发送 N+2 块。方向标识改为主机 `0xC3` / 从机 `0x3C`，两端必须配套更新，不能混用 V10 协议。

## 代码和已有改动

代码前缀为 `TAG_ZS3085E_241230/`：

- 新增 `application/bt_watch_no_psram/src/main/spi_link_circular.h`：`spi2circ` 命令、两块延迟确认及结果计数。
- 新增 `zephyr/drivers/spi/spi_acts_spi2_circular.h`：RX RELOAD 和有限 TX 的 open/transfer/close；持有现有 SPI 锁，检查半区事件、剩余长度、FIFO 与线空闲；停止失败时保留资源。
- 修改 `application/bt_watch_no_psram/src/main/system_app_shell.c`：注册新命令并防止新旧模式混用。
- 修改 `zephyr/drivers/spi/spi_acts.c`、`zephyr/include/drivers/spi_acts_entry.h`：接入驱动和接口。
- 修改 `zephyr/drivers/spi/spi_acts_spi2_dma.h`：共用 RX 缓冲从 512 扩为 1024 字节；旧接口仍只允许 256/512 字节。
- 新增 `tests/test_spi_circular_dma_v11.py`、`tests/test_spi_circular_host_v11.py`。

本轮基线和独立补丁见 `artifacts/spi_circular_v11_20261118/before/`、`before_hashes.json`、`this_session.patch`。原有未提交代码、时钟修改、旧协议、旧文档和测试保留，没有 Git 提交或 reset。旧 V10 固件包保持原哈希。

## 构建与校验

ATS 编译成功、0 警告；UMA 编译成功，28 条已有 SDK 警告，SPI 修改无新增警告。180 项软件测试包括从机环 19、主机环 10、新命令 9、从机旧路径 86、主机旧 DMA 14、旧命令 10、READY 32。测试执行生产源码编译的 ARM 代码及 MMIO/DMA 模型，不能代替真实 DMA/cache/电气时序测试。

ATS SRAM 为 **932272 / 932864 字节，链接余量仅 592 字节**；SHARE_RAM 为 20948 / 20991 字节。UMA APP 静态 PSRAM 为 8992 / 32768 字节，余量 23776 字节。链接余量不代表运行时堆或栈水位，进一步增加缓冲前应重新核算。

复现编译：

```powershell
$spiPython = 'C:/Actions/ZEPHYR_SDK/1.10/Python38/python.exe'
$spiRoot = 'D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644'
& $spiPython "$spiRoot/artifacts/spi_circular_v11_20261118/build_v11.py" ats
& $spiPython "$spiRoot/artifacts/spi_circular_v11_20261118/build_v11.py" uma
```

本目录 `artifacts/spi_circular_v11_20261118/` 保存编译/测试日志、退出码、ELF/bin/map、源码快照、manifest 和 SHA256。UMA `tests/package_spi_circular_v11.py` 负责两端打包及 CRC、二进制身份、布局、源文件保留和旧包哈希检查；不操作设备，拒绝覆盖同名交付包。ATS 专有内层包装与 SDK 输出比对，外层 ATF 及成员检查 CRC。

## 配套固件和上板命令

```text
D:\ats3085e\artifacts\ATS3085E_SPI_CIRCULAR_V11_20261118.fw
D:\UMA503\UMA503-V7_OPENCPU_OC_2025.03.14_1644\artifacts\UMA503_SPI_CIRCULAR_V11_ABOOT_20261118.zip
```

SHA256 记录在两端 `artifacts/spi_circular_v11_20261118/SHA256.txt`。本轮没有打开串口或烧录。使用现有工具更新两端后共同重启，保持 GPIO50 → GPIO61 的 READY 接线。先确认从机出现 `SPI11 DESC_MEMTEST PASS` 和 `SPI DMA READY v11 circular`，再逐条执行：

```text
app spi2circ 1 0 4000000
app spi2circ 10 0 4000000
app spi2circ 500 0 4000000
app spi2circ 5000 0 4000000
```

参数依次为确认块数、块间额外等待毫秒、SPI 频率。默认 `1 0 4000000`；允许范围分别为 1–10000、0–1000、100000–8000000。允许范围不代表所有频率都已上板验证。每轮 PASS 后至少等 2 秒收齐从机日志，再执行下一轮。

成功条件：

- 主机 `SPI11 DMA open` 显示 `rx_reload=1 ring=1024 tx_finite=512`；close 显示 `blocks=N+2 stop=0/0 rx_starts=1`。
- 主机 `SPI11 RESULT` 显示 `good=N target=N tx_blocks=N+2 ret=0 PASS`。
- 从机 `blocks` 本轮增量为 N+2，`bad=0`；`SPI11 RING` 显示 `starts=1 rearm=0`，CSR 中 RUN=1、NODESCFETCH=0。计数为自启动起累计值；starts 不包括启动前的有限描述符自检。
- 波形确认每次 CS 恰好 4096 个 SCK 周期、CS 外无多余时钟、每块只有一个 READY 许可，暂停时从机环继续保持 RUN。

5000 个确认块对应 10000 个有效逻辑帧和 2400000 字节有效载荷，可与 V10 等量测试比较。没有预先承诺性能提升百分比。

任一错误后保存两端日志并共同重启，不自动重试。本次启动中不能混用 `spi2circ` 与 `spi2swapready` / `spi2swapblock2`。回退可直接使用保留的 V10 两端包；或将 UMA `LINK_DMA_CIRCULAR` 显式设为 0 后重新编译打包，主机使用旧 `spi2swapblock2` 命令。
