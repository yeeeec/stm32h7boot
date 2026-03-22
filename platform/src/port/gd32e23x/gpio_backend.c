/**
 * @file gpio_backend.c
 * @brief GD32E23x GPIO backend implementation.
 */
#include "gd32e23x.h"

#include "platform/gpio.h"

/**
 * @brief Initialize all GPIO pins used by the platform.
 *
 * This implementation enables the GPIOB clock and configures:
 * - PB2 as output (BLE LED)
 * - PB8~PB13 as outputs (K1, FAN, VAL1~4)
 *
 * @return PLAT_OK on success.
 */
Plat_Status_t platform_gpio_init(void) {
    rcu_periph_clock_enable(RCU_GPIOB);

    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_2);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);

    uint32_t control_pins =
        GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;

    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, control_pins);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, control_pins);

    gpio_bit_reset(GPIOB, GPIO_PIN_2 | control_pins);

    return PLAT_OK;
}

/**
 * @brief Convert platform GPIO ID to GD32 pin definition.
 *
 * @param id Platform GPIO identifier.
 * @return GD32 GPIO_PIN_x value, or 0 if the ID is invalid.
 */
static inline uint32_t gpio_get_pin(Plat_GPIO_ID_t id) {
    switch (id) {
        case PLAT_GPIO_BLE_LED:
            return GPIO_PIN_2;
        case PLAT_GPIO_K1_CTRL:
            return GPIO_PIN_8;
        case PLAT_GPIO_FAN_CTRL:
            return GPIO_PIN_9;
        case PLAT_GPIO_VAL1_CTRL:
            return GPIO_PIN_10;
        case PLAT_GPIO_VAL2_CTRL:
            return GPIO_PIN_11;
        case PLAT_GPIO_VAL3_CTRL:
            return GPIO_PIN_12;
        case PLAT_GPIO_VAL4_CTRL:
            return GPIO_PIN_13;
        default:
            return 0;
    }
}

/**
 * @brief Write GPIO output level.
 *
 * @param id Platform GPIO identifier.
 * @param level Target output level.
 */
void gpio_write_level(Plat_GPIO_ID_t id, Plat_GPIO_Level_t level) {
    uint32_t pin = gpio_get_pin(id);

    if (pin == 0) {
        return;
    }

    if (level == PLAT_GPIO_HIGH) {
        gpio_bit_set(GPIOB, pin);
    } else {
        gpio_bit_reset(GPIOB, pin);
    }
}

/**
 * @brief Read GPIO output level.
 *
 * This reads the output data register (OCTL) and returns the currently
 * configured output state.
 *
 * @param id Platform GPIO identifier.
 * @return Current output level.
 */
Plat_GPIO_Level_t gpio_read_level(Plat_GPIO_ID_t id) {
    uint32_t pin = gpio_get_pin(id);

    if (pin == 0) {
        return PLAT_GPIO_LOW;
    }

    if (gpio_output_bit_get(GPIOB, pin) == SET) {
        return PLAT_GPIO_HIGH;
    } else {
        return PLAT_GPIO_LOW;
    }
}

/**
 * @brief Toggle GPIO output level.
 *
 * @param id Platform GPIO identifier.
 */
void gpio_toggle_pin(Plat_GPIO_ID_t id) {
    uint32_t pin = gpio_get_pin(id);

    if (pin == 0) {
        return;
    }

    if (gpio_output_bit_get(GPIOB, pin) == SET) {
        gpio_bit_reset(GPIOB, pin);
    } else {
        gpio_bit_set(GPIOB, pin);
    }
}
