/* ATS3085E SPI2 主机专用入口，覆盖空闲配置检查、GPIO 诊断、分段 FIFO、有限 DMA 和循环接收。 */
#ifndef SPI_ACTS_ENTRY_H_    /* 防止多个应用协议头重复引入这些声明。 */
#define SPI_ACTS_ENTRY_H_
#include <device.h>    /* 使用由驱动注册的设备对象，不由调用者自行分配。 */
#include <stddef.h>    /* 收发长度使用 size_t。 */
#include <stdint.h>    /* 收发入口需要明确宽度的定长类型。 */

/**
  * @brief  将本板 SPI2 配为 Mode 3、8 位主机空闲状态并输出诊断。
  * @param  dev：[输入]通过 CONFIG_SPI_2_NAME 绑定的 Actions SPI2 设备，不可为空。
  * @retval 0：配置字段回读符合预期；-EINVAL：空设备；-ENOTSUP：非本阶段支持的实例或配置；-EIO：回读不符。
  * @note   仅在线程中调用，会等待总线锁；调用前须保证 UMA503 已运行从机检查固件。
  *         不写数据寄存器、不启动 FIFO，保留主机空闲配置；不能据此认定接线或收发通过。
  */
int spi_acts_spi2_entry_check(const struct device *dev);

/**
  * @brief  用本板 SPI2 的四根 GPIO 线执行一次低速 Mode 3 双向诊断。
  * @param  dev：[输入]绑定的 Actions SPI2 设备，不可为空。
  * @param  tx：[输入]可读发送缓冲区，至少 len 字节，不可为空。
  * @param  rx：[输出]可写接收缓冲区，至少 len 字节，不可为空。
  * @param  len：[输入]传输字节数，范围 1～256。
  * @param  hz：[输入]时钟频率上限，范围 100000～400000 Hz。
  * @retval 0：收发完成；-EINVAL：参数无效；-ENOTSUP：非本板 SPI2；-EBUSY：总线或引脚不可用；-ETIMEDOUT：传输超时。
  * @note   线程调用并保持唤醒。切换前获取 SPI2 总线锁，结束回到原复用。允许中断造成时钟拉长。
  */
int spi_acts_spi2_gpio_transceive(const struct device *dev, const uint8_t *tx,
                                  uint8_t *rx, size_t len, uint32_t hz);
struct spi_config;     /* 配置实参采用公共 SPI API 类型，本头只需要指针类型。 */
struct spi_buf_set;    /* 分段收发通过公共缓冲集合描述两个方向。 */

/* 循环会话的锁覆盖整个 open 到 close 区间，每次 transfer 前由上层消费一个 READY。 */
/**
  * @brief  取得 SPI2 总线并启动跨帧持续工作的接收环。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @param  config：[输入]非空，主机、Mode 3、8 位、硬件片选 0，请求 100 kHz～8 MHz；实际频率还受 HOSC 分频档限制。
  * @retval 0 表示可收发。-EINVAL 为配置无效，-EBUSY 为会话已开启，-ENOTSUP 为平台、实例或存储区不符，-ENODEV 为 DMA 设备不可用，-EIO 为隔离或重载位异常。申请、时钟、配置与启动错误原样返回。
  * @note   持唤醒锁的同一线程配对调用 open、transfer 和 close。成功后跨帧持有设备锁；申请过程失败时自行回收已取得的资源。
  */
int spi_acts_spi2_circular_open(const struct device *dev, const struct spi_config *config);
/**
  * @brief  按当前环槽交换一个固定块并核对硬件完成边界。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @param  tx：[输入]非空且可读的 512 字节，函数先复制到内部 SRAM。
  * @param  rx：[输出]非空且可写的 512 字节，仅在整块校验通过后交付。
  * @retval 0 表示交付 512 字节，-EPERM 为会话无效，-EINVAL 为空缓冲，-EIO 为硬件状态、事件或计数不符，-ETIMEDOUT 为超过 100 ms，其他负值来自 DMA。无平台支持时返回 -ENOTSUP。
  * @note   调用者先消费对端的一次 READY。每次只启动有限 TX，RX 保持重载；进入硬件故障分支后必须 close。
  */
int spi_acts_spi2_circular_transfer(const struct device *dev, const uint8_t *tx, uint8_t *rx);
/**
  * @brief  关断 SPI 请求并检查两路 PDMA 是否已停止，然后释放会话锁。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @retval 0 表示通道均已停止，-EINVAL 为会话或设备不匹配，-ENOTSUP 为平台不支持；停止失败优先返回 RX 错误，否则返回 TX 错误。
  * @note   释放前先拉高片选。任一停止失败会保留两个通道及静态缓冲区供隔离，后续 DMA 使用须待重启。
  */
int spi_acts_spi2_circular_close(const struct device *dev);

/* 单次 DMA 不跨帧持锁，同样在确认剩余计数和 SPI 线端空闲以后交付数据。 */
/**
  * @brief  用两个有限长度的 PDMA 通道完成一帧 SPI2 全双工交换。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @param  config：[输入]非空，主机、Mode 3、8 位、硬件片选 0，请求 100 kHz～8 MHz；实际频率还受 HOSC 分频档限制。
  * @param  tx：[输入]非空，可读至少 len 字节，同步调用期间有效。
  * @param  rx：[输出]非空，可写至少 len 字节，通过全部完成检查后才复制到此处。
  * @param  len：[输入]一帧的字节数，只接受 256 或 512。
  * @retval 0 表示已交付整帧。-EINVAL 为配置或长度无效，-ENOTSUP 为平台、实例或内部 SRAM 条件不符，-ENODEV 为 DMA 设备不可用，-EIO 为硬件状态异常，-ETIMEDOUT 为完成或停止回读超时。时钟和 DMA 驱动错误原样返回。
  * @note   线程持唤醒锁调用。先取得总线锁，再布置 RX 和 TX，轮询期间允许中断；任一路无法确认停机时，两通道与静态缓冲区保留至重启。
  */
int spi_acts_spi2_transceive_dma(const struct device *dev,
                               const struct spi_config *config,
                               const uint8_t *tx, uint8_t *rx, size_t len);

/* 段边界保留完整的 503 双样本 FIFO 项，段长只能取偶数。 */
#define SPI_ACTS_SPI2_BURST_DEFAULT 8u    /* 分段诊断的缺省段上限为 8 字节。 */
#define SPI_ACTS_SPI2_BURST_MAX 16u       /* 每段最多 16 字节，调用时还要求偶数。 */

/**
  * @brief  SPI2 硬件主机分段作全双工收发，整个请求只拉低一次 CS#。
  * @param  dev、config：[输入]SPI2 设备及 8 位主机配置。
  * @param  tx_bufs、rx_bufs：各含一个非空、等长缓冲区，长度为 1～256 字节。
  * @param  burst_gap_us：段间最短停顿，范围 1～1000 µs。
  * @param  burst_bytes：每段最多发送的字节数，范围 2～16，必须为偶数。
  * @retval 0：成功；负数：无效参数、实例不支持或硬件收发失败。
  */
int spi_acts_spi2_transceive_paced(const struct device *dev,
                                  const struct spi_config *config,
                                  const struct spi_buf_set *tx_bufs,
                                  const struct spi_buf_set *rx_bufs,
                                  unsigned int burst_gap_us, unsigned int burst_bytes);
#endif    /* 头文件保护结束。 */
