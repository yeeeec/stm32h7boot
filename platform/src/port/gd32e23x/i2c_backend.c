/**
 * @file i2c_backend.c
 * @brief GD32E23x I2C backend implementation (interrupt-driven).
 */
#include "gd32e23x.h"

#include "platform/i2c.h"

/** I2C1 on PF6(SCL) and PF7(SDA). */
#define I2C_PERIPH    I2C1
#define I2C_RCU       RCU_I2C1
#define I2C_GPIO_RCU  RCU_GPIOF
#define I2C_GPIO_PORT GPIOF
#define I2C_SCL_PIN   GPIO_PIN_6
#define I2C_SDA_PIN   GPIO_PIN_7
#define I2C_AF        GPIO_AF_0

/**
 * @brief I2C internal state machine.
 */
typedef enum {
    I2C_IDLE = 0,
    I2C_WRITE_ADDR,
    I2C_WRITE_DATA,
    I2C_READ_ADDR,
    I2C_READ_DATA
} i2c_state_t;

/** Current I2C state. */
static volatile i2c_state_t g_i2c_state = I2C_IDLE;
/** Current 8-bit address (including R/W bit). */
static uint8_t g_i2c_addr = 0;
/** Current transfer buffer. */
static uint8_t *g_i2c_buf = NULL;
/** Total transfer length. */
static volatile uint16_t g_i2c_len = 0;
/** Current transfer count. */
static volatile uint16_t g_i2c_cnt = 0;

/**
 * @brief TX completion callback (weak).
 */
__attribute__((weak)) void i2c_callback_tx_done(void) {
}

/**
 * @brief RX completion callback (weak).
 */
__attribute__((weak)) void i2c_callback_rx_done(void) {
}

/**
 * @brief Error callback (weak).
 *
 * @param flags Implementation-defined error flags.
 */
__attribute__((weak)) void i2c_callback_error(uint32_t flags) {
    (void) flags;
}

static void i2c_stop_bus(void);

/**
 * @brief Force STOP on the bus and reset driver state.
 */
static void i2c_stop_bus(void) {
    i2c_stop_on_bus(I2C_PERIPH);

    i2c_interrupt_disable(I2C_PERIPH, I2C_INT_EV | I2C_INT_BUF | I2C_INT_ERR);

    g_i2c_state = I2C_IDLE;
}

/**
 * @brief Initialize I2C peripheral and GPIO.
 */
void platform_i2c_init(void) {
    rcu_periph_clock_enable(I2C_GPIO_RCU);
    rcu_periph_clock_enable(I2C_RCU);

    gpio_af_set(I2C_GPIO_PORT, I2C_AF, I2C_SCL_PIN | I2C_SDA_PIN);

    gpio_mode_set(I2C_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, I2C_SCL_PIN | I2C_SDA_PIN);
    gpio_output_options_set(I2C_GPIO_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ,
                            I2C_SCL_PIN | I2C_SDA_PIN);

    i2c_deinit(I2C_PERIPH);

    i2c_clock_config(I2C_PERIPH, 100000, I2C_DTCY_2);

    i2c_mode_addr_config(I2C_PERIPH, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, 0);

    i2c_enable(I2C_PERIPH);

    i2c_ack_config(I2C_PERIPH, I2C_ACK_ENABLE);

    nvic_irq_enable(I2C1_EV_IRQn, 1);
    nvic_irq_enable(I2C1_ER_IRQn, 1);
}

/**
 * @brief Start an interrupt-driven I2C write transaction.
 *
 * @param addr7 7-bit slave address.
 * @param buf Data buffer (TX).
 * @param len Length in bytes.
 * @return Platform status code.
 */
Plat_Status_t platform_i2c_write_it(uint8_t addr7, const uint8_t *buf, uint16_t len) {
    if (g_i2c_state != I2C_IDLE) {
        return PLAT_ERR_BUSY;
    }

    if (len == 0 || buf == NULL) {
        return PLAT_ERR_INVALID_PARAM;
    }

    g_i2c_addr  = (uint8_t) (addr7 << 1);
    g_i2c_buf   = (uint8_t *) buf;
    g_i2c_len   = len;
    g_i2c_cnt   = 0;
    g_i2c_state = I2C_WRITE_ADDR;

    i2c_interrupt_enable(I2C_PERIPH, I2C_INT_EV | I2C_INT_BUF | I2C_INT_ERR);

    i2c_start_on_bus(I2C_PERIPH);

    return PLAT_OK;
}

/**
 * @brief Start an interrupt-driven I2C read transaction.
 *
 * @param addr7 7-bit slave address.
 * @param buf Data buffer (RX).
 * @param len Length in bytes.
 * @return Platform status code.
 */
Plat_Status_t platform_i2c_read_it(uint8_t addr7, uint8_t *buf, uint16_t len) {
    if (g_i2c_state != I2C_IDLE) {
        return PLAT_ERR_BUSY;
    }

    if (len == 0 || buf == NULL) {
        return PLAT_ERR_INVALID_PARAM;
    }

    g_i2c_addr  = (uint8_t) ((addr7 << 1) | 1);
    g_i2c_buf   = buf;
    g_i2c_len   = len;
    g_i2c_cnt   = 0;
    g_i2c_state = I2C_READ_ADDR;

    i2c_interrupt_enable(I2C_PERIPH, I2C_INT_EV | I2C_INT_BUF | I2C_INT_ERR);

    i2c_ack_config(I2C_PERIPH, I2C_ACK_ENABLE);

    i2c_start_on_bus(I2C_PERIPH);

    return PLAT_OK;
}

/**
 * @brief I2C1 event interrupt handler.
 */
void I2C1_EV_IRQHandler(void) {
    switch (g_i2c_state) {
        case I2C_WRITE_ADDR:
        case I2C_READ_ADDR:
            if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_SBSEND)) {
                i2c_master_addressing(I2C_PERIPH, g_i2c_addr,
                                      (g_i2c_state == I2C_READ_ADDR) ? I2C_RECEIVER
                                                                     : I2C_TRANSMITTER);

                if (g_i2c_state == I2C_WRITE_ADDR) {
                    g_i2c_state = I2C_WRITE_DATA;
                } else {
                    g_i2c_state = I2C_READ_DATA;
                }
            }
            break;

        case I2C_WRITE_DATA:
            if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_ADDSEND)) {
                i2c_interrupt_flag_clear(I2C_PERIPH, I2C_INT_FLAG_ADDSEND);
            }

            if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_TBE)) {
                if (g_i2c_cnt < g_i2c_len) {
                    i2c_data_transmit(I2C_PERIPH, g_i2c_buf[g_i2c_cnt++]);
                } else {
                    if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_BTC)) {
                        i2c_stop_bus();
                        i2c_callback_tx_done();
                    }
                }
            }
            break;

        case I2C_READ_DATA:
            if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_ADDSEND)) {
                if (g_i2c_len == 1) {
                    i2c_ack_config(I2C_PERIPH, I2C_ACK_DISABLE);
                }

                i2c_interrupt_flag_clear(I2C_PERIPH, I2C_INT_FLAG_ADDSEND);

                if (g_i2c_len == 1) {
                    i2c_stop_on_bus(I2C_PERIPH);
                }
            }

            if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_RBNE)) {
                if (g_i2c_cnt == g_i2c_len - 2) {
                    i2c_ack_config(I2C_PERIPH, I2C_ACK_DISABLE);
                } else if (g_i2c_cnt == g_i2c_len - 1) {
                    i2c_stop_on_bus(I2C_PERIPH);
                }

                g_i2c_buf[g_i2c_cnt++] = i2c_data_receive(I2C_PERIPH);

                if (g_i2c_cnt == g_i2c_len) {
                    i2c_stop_bus();
                    i2c_callback_rx_done();
                }
            }
            break;

        default:
            i2c_stop_bus();
            break;
    }
}

/**
 * @brief I2C1 error interrupt handler.
 */
void I2C1_ER_IRQHandler(void) {
    uint32_t error_flags = 0;

    if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_AERR)) {
        error_flags |= (1U << 0);
        i2c_interrupt_flag_clear(I2C_PERIPH, I2C_INT_FLAG_AERR);
    }

    if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_BERR)) {
        error_flags |= (1U << 1);
        i2c_interrupt_flag_clear(I2C_PERIPH, I2C_INT_FLAG_BERR);
    }

    if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_LOSTARB)) {
        error_flags |= (1U << 2);
        i2c_interrupt_flag_clear(I2C_PERIPH, I2C_INT_FLAG_LOSTARB);
    }

    if (i2c_interrupt_flag_get(I2C_PERIPH, I2C_INT_FLAG_OUERR)) {
        error_flags |= (1U << 3);
        i2c_interrupt_flag_clear(I2C_PERIPH, I2C_INT_FLAG_OUERR);
    }

    i2c_stop_bus();

    i2c_callback_error(error_flags);
}
