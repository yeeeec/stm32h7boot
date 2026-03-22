#ifndef PLATFORM_GPIO_H
#define PLATFORM_GPIO_H

#include "definitions.h" /**< 引用之前的 Plat_Status_t 等定义 */

/** 1. 定义业务抽象 ID (App 层只看这个) */
typedef enum {
    PLAT_GPIO_BLE_LED = 0, /**< 对应 PB2 */
    PLAT_GPIO_K1_CTRL,     /**< 对应 PB8 */
    PLAT_GPIO_FAN_CTRL,    /**< 对应 PB9 */
    PLAT_GPIO_VAL1_CTRL,   /**< 对应 PB10 */
    PLAT_GPIO_VAL2_CTRL,   /**< 对应 PB11 */
    PLAT_GPIO_VAL3_CTRL,   /**< 对应 PB12 */
    PLAT_GPIO_VAL4_CTRL,   /**< 对应 PB13 */

    PLAT_GPIO_MAX /**< 计数用，必须在最后 */
} Plat_GPIO_ID_t;

/** 2. 定义电平状态 */
typedef enum { PLAT_GPIO_LOW = 0, PLAT_GPIO_HIGH = 1 } Plat_GPIO_Level_t;

/** 3. 接口声明 */

#endif

/**
 * @brief Init pins.
 *
 * @return
 */
Plat_Status_t platform_gpio_init(void);

/**
 * @brief Write level.
 *
 * @param id
 * @param level
 */
void gpio_write_level(Plat_GPIO_ID_t id, Plat_GPIO_Level_t level);

/**
 * @brief Read level.
 *
 * @param id
 * @return
 */
Plat_GPIO_Level_t gpio_read_level(Plat_GPIO_ID_t id);

/**
 * @brief Toggle pin.
 *
 * @param id
 */
void gpio_toggle_pin(Plat_GPIO_ID_t id);