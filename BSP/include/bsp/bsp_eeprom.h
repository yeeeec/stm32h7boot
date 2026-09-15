/**
 * @file bsp_eeprom.h
 * @brief AT24C128 EEPROM 的板级 I2C 绑定。
 */
#ifndef BSP_EEPROM_H
#define BSP_EEPROM_H

#include <stdint.h>

#include "firmware/status.h"

struct at24;

#define BSP_EEPROM_CAPACITY_BYTES (16UL * 1024UL)
#define BSP_EEPROM_PAGE_SIZE_BYTES 64U

/** 绑定已安装 EEPROM 时同步消费的配置。 */
typedef struct
{
    uint8_t device_address_7bit; /**< 由原理图确定的 0x50..0x57 地址。 */
    uint32_t write_timeout_ms;   /**< AT24 内部写周期的最大时间。 */
} bsp_eeprom_config_t;

/**
 * @brief 将 I2C1 绑定到板级 AT24C128 设备。
 *
 * @param[in] config 已确认的板级地址和 Timeout。BSP 会复制两个值，不保留该指针。
 *
 * @return 成功时返回 FIRMWARE_STATUS_OK。
 * @return 配置为 NULL 时返回 FIRMWARE_STATUS_INVALID_ARGUMENT。
 * @return I2C1 处于 Reset 或已经绑定时返回 FIRMWARE_STATUS_INVALID_STATE。
 * @return 其他情况返回驱动校验状态。
 *
 * @note 在定义板级 GPIO 绑定前不会控制 WP。调用此 API 时，实际板卡必须
 *       保持 EEPROM 可写。
 */
firmware_status_t BSP_EepromInit(const bsp_eeprom_config_t *config);

/**
 * @brief 返回已初始化的 AT24 驱动实例。
 *
 * @return 驱动绑定后返回 BSP 持有的静态生命周期对象，绑定前返回 NULL。
 *         非 NULL 结果不能覆盖 BSP_EepromInit() 探测失败；初始化失败后调用者
 *         必须忽略该对象。
 *
 * @note 调用者不得释放或重新初始化返回对象。
 */
struct at24 *BSP_EepromDevice(void);

/* Synchronous convenience calls retained for simple bare-metal users. They
 * execute the same AT24 driver instance returned by BSP_EepromDevice(). */
firmware_status_t BSP_EepromProbe(void);
firmware_status_t BSP_EepromRead(uint32_t address, void *data, uint32_t size);
firmware_status_t BSP_EepromWrite(uint32_t address, const void *data, uint32_t size);
int BSP_EepromIsInitialized(void);

#endif
