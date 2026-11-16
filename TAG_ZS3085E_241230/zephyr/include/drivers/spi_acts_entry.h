/* SPI2 角色互换：第一阶段只检查主机空闲配置；第二阶段提供主机单帧收发入口。 */
#ifndef SPI_ACTS_ENTRY_H_
#define SPI_ACTS_ENTRY_H_
#include <device.h>    /* 使用由驱动注册的设备对象，不由调用者自行分配。 */
#include <stdint.h>    /* 收发入口需要明确宽度的定长类型。 */

/**
  * @brief  将本板 SPI2 配为 Mode 3、8 位主机空闲状态并输出诊断。
  * @param  dev：[输入]通过 CONFIG_SPI_2_NAME 绑定的 Actions SPI2 设备，不可为空。
  * @retval 0：配置字段回读符合预期；-EINVAL：空设备；-ENOTSUP：非本阶段支持的实例或配置；-EIO：回读不符。
  * @note   仅在线程中调用，会等待总线锁；调用前须保证 UMA503 已运行从机检查固件。
  *         不写数据寄存器、不启动 FIFO，保留主机空闲配置；不能据此认定接线或收发通过。
  */
int spi_acts_spi2_entry_check(const struct device *dev);
#endif    /* 头文件保护结束。 */
