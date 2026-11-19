# SPI V11A：实机失败分析与主机修复

日期：2026-11-18，Asia/Shanghai。

完整分析、现场证据与复测说明：

`D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644/docs/SPI_HARDWARE_CIRCULAR_V11A_RESULT_20261118.md`

## 现场结论

- 首版 V11 的 1 块和 10 块测试通过；500 块测试 good=43、tx_blocks=46、ret=-5，失败于主机驱动检查阶段。从机此轮接收 46 块、bad=0，仍为 RUN=1、NODESCFETCH=0。
- 14:41 的后续 5000 块命令没有开始，因为前次错误锁住链路。
- 用户补充的 14:51 日志来自两端重启后：DMA 完成首块，ACK 的 CRC 检查返回 -1。用户当时没有确认从机本次 READY，且缺少配套 COM6 日志。sid=0xFFFF 不能证明整块全为 0xFF，首块错误的原因尚未定位。

## 已修复及已验证

ATS 旧驱动先读 SPI 状态、后读 DMA 完成事件，存在将旧 RX_EMPTY=0 与新完成事件拼接而误报 -5 的竞态。以生产驱动 ARM 代码在模型中复现，V11A 改为先读 DMA 事件/计数，再读 SPI 状态。是否就是现场第 46 次传输的触发原因仍需新日志验证。

新增驱动失败的 stage/sample/halted 诊断、ACK 失败时两个逻辑帧各 16 字节帧头、故障后再次执行时的 BLOCKED 提示。保留异常检测、CRC、超时、首错停机及停止失败隔离，不自动重试。

修改文件：

- `TAG_ZS3085E_241230/zephyr/drivers/spi/spi_acts_spi2_circular.h`
- `TAG_ZS3085E_241230/application/bt_watch_no_psram/src/main/spi_link_circular.h`
- 两个 V11 主机测试文件，新增 V11A 检查/打包脚本及说明。

185 项软件回归通过：主机环 13、命令 11、旧 DMA 14、旧命令 10、READY 32、从机环 19、从机旧路径 86。额外 13 项优化模型重复检查通过，不重复计数；模拟器 Thumb IT hook 兼容选项仅用于模型，生产 SDK 参数未变。

ATS SDK 编译成功、0 警告，SRAM 仍为 932272/932864 B，余量 592 B。UMA 代码和固件不变。V11A 尚未上板，不能宣称两类现场错误都已解决，也没有有效长测吞吐数据。

## 产物与复测

```text
新 ATS：D:\ats3085e\artifacts\ATS3085E_SPI_CIRCULAR_V11A_20261118.fw
配套 UMA：D:\UMA503\UMA503-V7_OPENCPU_OC_2025.03.14_1644\artifacts\UMA503_SPI_CIRCULAR_V11_ABOOT_20261118.zip
```

只需更新主机，再共同重启两端。确认 COM6 本次 `SPI11 DESC_MEMTEST PASS` 和 `SPI DMA READY v11 circular` 后，依次执行：

```text
app spi2circ 1 0 4000000
app spi2circ 10 0 4000000
app spi2circ 500 0 4000000
app spi2circ 5000 0 4000000
```

主机 open 行须有 `rev=11a`。每轮 PASS 后至少等 2 秒收齐从机汇总；失败即停止该轮。成功需 good=N、tx_blocks=N+2、ret=0、rx_starts=1；从机块数增量 N+2、bad=0、starts=1/rearm=0。

若再失败，需要同一轮重启至失败的 COM4/COM6 日志。首块错误看 ACK 和两段帧头；驱动错误看 stage/sample/halted。故障后再次发命令会明确提示 BLOCKED，仍须共同重启恢复。

完整输入日志、复现记录、测试、编译、ELF/map、源码快照、独立补丁、manifest 和 SHA256 保存在 `artifacts/spi_circular_v11a_20261118/`。原有未提交改动及 V10/V11 包保留，没有 Git 提交或设备操作。
