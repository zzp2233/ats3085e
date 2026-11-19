/* SPI2 角色互换：第一阶段只检查主机空闲配置；第二阶段提供主机单帧收发入口。 */
#ifndef SPI_ACTS_ENTRY_H_
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
struct spi_config;
struct spi_buf_set;

/* V11 session owns the SPI lock from open to close, same thread/wake lock.
 * RX is a 2x512-byte hardware RELOAD ring; TX is finite to bound SPI clocks.
 * transfer follows one peer READY token. Always close after a transfer error.
 * Open unwinds its own errors. TX/RX caller buffers must contain 512 bytes.
 */
int spi_acts_spi2_circular_open(const struct device *dev, const struct spi_config *config);
int spi_acts_spi2_circular_transfer(const struct device *dev, const uint8_t *tx, uint8_t *rx);
int spi_acts_spi2_circular_close(const struct device *dev);

/* V10 hardware SPI2 + PDMA, 256/512-byte full duplex, Mode 3 / 8-bit.
 * Thread context, caller holds wake lock. 100 kHz..8 MHz requested clock.
 * Hardware CS only, no HOLD/LOCK/async. Internal RAM bounce buffers, allocated
 * SDK DMA channels, 40 ms deadline, RX+TX counters and wire idle checked.
 * Returns a negative error on failure; a STOP fault requires reboot.
 */
int spi_acts_spi2_transceive_dma(const struct device *dev,
                               const struct spi_config *config,
                               const uint8_t *tx, uint8_t *rx, size_t len);

/* 段边界保留完整的 503 双样本 FIFO 项，段长只能取偶数。 */
#define SPI_ACTS_SPI2_BURST_DEFAULT 8u
#define SPI_ACTS_SPI2_BURST_MAX 16u

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
