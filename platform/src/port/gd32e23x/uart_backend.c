/**
 * @file uart_backend.c
 * @brief GD32E23x UART backend implementation (USART + DMA + lwrb).
 */
#include "gd32e23x.h"

#include "lwrb/lwrb.h"
#include "platform/uart.h"
#include <string.h>

/** UART RX buffer size for USART0. */
#define UART_RX_BUF_0_SIZE 64
/** UART RX buffer size for USART1. */
#define UART_RX_BUF_1_SIZE 32
/** UART TX DMA staging buffer size. */
#define UART_TX_BUF_SIZE 128

/**
 * @brief UART backend context.
 */
typedef struct {
    /** Hardware mapping. */
    uint32_t usart_periph;
    rcu_periph_enum rcu_usart;
    rcu_periph_enum rcu_gpio;
    uint32_t gpio_port;
    uint32_t tx_pin;
    uint32_t rx_pin;
    uint32_t gpio_af;

    /** DMA mapping. */
    uint32_t dma_periph; /**< DMA_CHx (unused mapping placeholder). */
    rcu_periph_enum rcu_dma;
    dma_channel_enum tx_dma_ch;
    dma_channel_enum rx_dma_ch;
    IRQn_Type dma_irqn;

    /** Runtime data. */
    lwrb_t *rx_rb;       /**< Registered RX ring buffer (owned by service). */
    uint8_t *rx_raw_buf; /**< DMA circular raw buffer. */
    uint16_t rx_buf_size;
    uint16_t rx_old_pos; /**< Last processed DMA position. */

    uint8_t tx_buf[UART_TX_BUF_SIZE]; /**< Local buffer for TX stability. */
    volatile bool tx_busy;
} uart_backend_t;

/** USART0 RX DMA raw buffer. */
static uint8_t g_uart_rx_buf_0[UART_RX_BUF_0_SIZE];
/** USART1 RX DMA raw buffer. */
static uint8_t g_uart_rx_buf_1[UART_RX_BUF_1_SIZE];

/** UART backend contexts. */
static uart_backend_t g_uart_ctx[PLAT_UART_MAX];

static void uart_init_hardware(uart_backend_t *ctx, uint32_t baud);
static void uart_init_dma_rx(uart_backend_t *ctx);
// static void uart_check_rx_dma(uart_backend_t *ctx);
static void uart_handle_tx_complete(Plat_UART_ID_t id);

/**
 * @brief Initialize a UART channel.
 *
 * @param id Platform UART identifier.
 * @param baud_rate UART baud rate.
 * @return Platform status code.
 */
Plat_Status_t platform_uart_init(Plat_UART_ID_t id, uint32_t baud_rate) {
    if (id >= PLAT_UART_MAX) {
        return PLAT_ERR_INVALID_PARAM;
    }

    uart_backend_t *ctx = &g_uart_ctx[id];
    memset(ctx, 0, sizeof(uart_backend_t));

    /** Setup static configuration (hardcoded map). */
    if (id == PLAT_UART_DISPLAY) {
        ctx->usart_periph = USART0;
        ctx->rcu_usart    = RCU_USART0;
        ctx->rcu_gpio     = RCU_GPIOA;
        ctx->gpio_port    = GPIOA;
        ctx->tx_pin       = GPIO_PIN_9;
        ctx->rx_pin       = GPIO_PIN_10;
        ctx->gpio_af      = GPIO_AF_1;

        ctx->rcu_dma = RCU_DMA;
        /** ctx->dma_periph = DMA_CH2; Assumption: RX=CH2 */
        ctx->rx_dma_ch = DMA_CH2;
        ctx->tx_dma_ch = DMA_CH1; /**< Assumption: TX=CH1 */
        ctx->dma_irqn  = DMA_Channel1_2_IRQn;

        ctx->rx_raw_buf  = g_uart_rx_buf_0;
        ctx->rx_buf_size = UART_RX_BUF_0_SIZE;
    } else if (id == PLAT_UART_SENSOR) {
        ctx->usart_periph = USART1;
        ctx->rcu_usart    = RCU_USART1;
        ctx->rcu_gpio     = RCU_GPIOA;
        ctx->gpio_port    = GPIOA;
        ctx->tx_pin       = GPIO_PIN_2;
        ctx->rx_pin       = GPIO_PIN_3;
        ctx->gpio_af      = GPIO_AF_1;

        ctx->rcu_dma = RCU_DMA;
        /** ctx->dma_periph = DMA_CH4; Assumption: RX=CH4 */
        ctx->rx_dma_ch = DMA_CH4;
        ctx->tx_dma_ch = DMA_CH3; /**< Assumption: TX=CH3 */
        ctx->dma_irqn  = DMA_Channel3_4_IRQn;

        ctx->rx_raw_buf  = g_uart_rx_buf_1;
        ctx->rx_buf_size = UART_RX_BUF_1_SIZE;
    }

    uart_init_hardware(ctx, baud_rate);
    uart_init_dma_rx(ctx);

    return PLAT_OK;
}

/**
 * @brief Register RX ring buffer (lwrb object) from service layer.
 *
 * @param id Platform UART identifier.
 * @param rb Pointer to initialized lwrb object.
 */
void platform_uart_register_rx_rb(Plat_UART_ID_t id, lwrb_t *rb) {
    if (id >= PLAT_UART_MAX) {
        return;
    }
    g_uart_ctx[id].rx_rb = rb;
}

/**
 * @brief Initialize GPIO/USART/NVIC for a UART instance.
 *
 * @param ctx UART backend context.
 * @param baud Baud rate.
 */
static void uart_init_hardware(uart_backend_t *ctx, uint32_t baud) {
    rcu_periph_clock_enable(ctx->rcu_gpio);
    rcu_periph_clock_enable(ctx->rcu_usart);
    rcu_periph_clock_enable(ctx->rcu_dma);

    gpio_af_set(ctx->gpio_port, ctx->gpio_af, ctx->tx_pin | ctx->rx_pin);
    gpio_mode_set(ctx->gpio_port, GPIO_MODE_AF, GPIO_PUPD_PULLUP, ctx->tx_pin | ctx->rx_pin);
    gpio_output_options_set(ctx->gpio_port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            ctx->tx_pin | ctx->rx_pin);

    usart_deinit(ctx->usart_periph);
    usart_baudrate_set(ctx->usart_periph, baud);
    usart_word_length_set(ctx->usart_periph, USART_WL_8BIT);
    usart_stop_bit_set(ctx->usart_periph, USART_STB_1BIT);
    usart_parity_config(ctx->usart_periph, USART_PM_NONE);
    usart_transmit_config(ctx->usart_periph, USART_TRANSMIT_ENABLE);
    usart_receive_config(ctx->usart_periph, USART_RECEIVE_ENABLE);

    usart_dma_receive_config(ctx->usart_periph, USART_DENR_ENABLE);
    usart_dma_transmit_config(ctx->usart_periph, USART_DENT_ENABLE);

    usart_enable(ctx->usart_periph);

    /** Enable DMA IRQ. */
    nvic_irq_enable(ctx->dma_irqn, 1);
}

/**
 * @brief Initialize UART RX DMA in circular mode and enable HT/FT interrupts.
 *
 * @param ctx UART backend context.
 */
static void uart_init_dma_rx(uart_backend_t *ctx) {
    dma_parameter_struct dma_init_struct;

    dma_deinit(ctx->rx_dma_ch);
    dma_struct_para_init(&dma_init_struct);

    dma_init_struct.direction    = DMA_PERIPHERAL_TO_MEMORY;
    dma_init_struct.memory_addr  = (uint32_t) ctx->rx_raw_buf;
    dma_init_struct.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_8BIT;
    dma_init_struct.number       = ctx->rx_buf_size;
    dma_init_struct.periph_addr  = (uint32_t) &USART_RDATA(ctx->usart_periph);
    dma_init_struct.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
    dma_init_struct.priority     = DMA_PRIORITY_ULTRA_HIGH;

    dma_init(ctx->rx_dma_ch, &dma_init_struct);

    /** Enable circular mode. */
    dma_circulation_enable(ctx->rx_dma_ch);

    /** Enable Half/Full transfer interrupts. */
    dma_interrupt_enable(ctx->rx_dma_ch, DMA_INT_HTF);
    dma_interrupt_enable(ctx->rx_dma_ch, DMA_INT_FTF);

    ctx->rx_old_pos = 0;
    dma_channel_enable(ctx->rx_dma_ch);
}

// /**
//  * @brief Update RX ring buffer using DMA position.
//  *
//  * This function copies incremental data from DMA raw buffer into the
//  * registered lwrb ring buffer (owned by service layer).
//  *
//  * @param ctx UART backend context.
//  */
// static void uart_check_rx_dma(uart_backend_t *ctx) {
//     if (ctx == NULL || ctx->rx_rb == NULL) {
//         return;
//     }

//     uint32_t rem      = dma_transfer_number_get(ctx->rx_dma_ch);
//     uint32_t curr_pos = ctx->rx_buf_size - rem;

//     if (curr_pos == ctx->rx_old_pos) {
//         return;
//     }

//     if (curr_pos > ctx->rx_old_pos) {
//         uint32_t len = curr_pos - ctx->rx_old_pos;
//         (void) lwrb_write(ctx->rx_rb, &ctx->rx_raw_buf[ctx->rx_old_pos], len);
//     } else {
//         uint32_t len1 = ctx->rx_buf_size - ctx->rx_old_pos;
//         uint32_t len2 = curr_pos;
//         (void) lwrb_write(ctx->rx_rb, &ctx->rx_raw_buf[ctx->rx_old_pos], len1);
//         (void) lwrb_write(ctx->rx_rb, &ctx->rx_raw_buf[0], len2);
//     }

//     ctx->rx_old_pos = curr_pos;
// }

/**
 * @brief Send data using DMA TX.
 *
 * @param id Platform UART identifier.
 * @param data Pointer to data buffer.
 * @param len Length in bytes.
 * @return Platform status code.
 */
Plat_Status_t platform_uart_send_data(Plat_UART_ID_t id, const uint8_t *data, uint16_t len) {
    if (id >= PLAT_UART_MAX || !data || len == 0) {
        return PLAT_ERR_INVALID_PARAM;
    }

    uart_backend_t *ctx = &g_uart_ctx[id];

    if (ctx->tx_busy) {
        return PLAT_ERR_BUSY;
    }

    if (len > UART_TX_BUF_SIZE) {
        return PLAT_ERR_INVALID_PARAM;
    }

    memcpy(ctx->tx_buf, data, len);
    ctx->tx_busy = true;

    dma_parameter_struct dma_init_struct;
    dma_deinit(ctx->tx_dma_ch);
    dma_struct_para_init(&dma_init_struct);

    dma_init_struct.direction    = DMA_MEMORY_TO_PERIPHERAL;
    dma_init_struct.memory_addr  = (uint32_t) ctx->tx_buf;
    dma_init_struct.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_8BIT;
    dma_init_struct.number       = len;
    dma_init_struct.periph_addr  = (uint32_t) &USART_TDATA(ctx->usart_periph);
    dma_init_struct.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
    dma_init_struct.priority     = DMA_PRIORITY_HIGH;

    dma_init(ctx->tx_dma_ch, &dma_init_struct);
    dma_interrupt_enable(ctx->tx_dma_ch, DMA_INT_FTF);
    dma_channel_enable(ctx->tx_dma_ch);

    return PLAT_OK;
}

/**
 * @brief Check TX busy.
 *
 * @param id Platform UART identifier.
 * @return true if busy, false otherwise.
 */
bool platform_uart_is_tx_busy(Plat_UART_ID_t id) {
    if (id >= PLAT_UART_MAX) {
        return false;
    }
    return g_uart_ctx[id].tx_busy;
}

/**
 * @brief Read data from RX ring buffer (registered by service layer).
 *
 * This also triggers a DMA position check to ensure latest bytes are written
 * into the ring buffer.
 *
 * @param id Platform UART identifier.
 * @param data Destination buffer.
 * @param len Max bytes to read.
 * @return Number of bytes read.
 */
uint16_t platform_uart_read_data(Plat_UART_ID_t id, uint8_t *data, uint16_t len) {
    if (id >= PLAT_UART_MAX || data == NULL || len == 0) {
        return 0;
    }

    uart_backend_t *ctx = &g_uart_ctx[id];

    // /* Ensure latest DMA data is moved into the registered ring buffer. */
    // uart_check_rx_dma(ctx);

    if (ctx->rx_rb == NULL) {
        return 0;
    }

    return (uint16_t) lwrb_read(ctx->rx_rb, data, len);
}

/**
 * @brief Handle TX DMA transfer complete and clear busy flag.
 *
 * @param id Platform UART identifier.
 */
static void uart_handle_tx_complete(Plat_UART_ID_t id) {
    uart_backend_t *ctx = &g_uart_ctx[id];
    ctx->tx_busy        = false;
}

/**
 * @brief DMA interrupt handler for channels 1 & 2.
 *
 * Used for channels 1 & 2.
 *
 * Assumption mapping:
 * - USART0 TX = CH1
 * - USART0 RX = CH2
 */
void DMA_Channel1_2_IRQHandler(void) {
    if (dma_interrupt_flag_get(DMA_CH1, DMA_INT_FLAG_FTF)) {
        dma_interrupt_flag_clear(DMA_CH1, DMA_INT_FLAG_G);
        uart_handle_tx_complete(PLAT_UART_DISPLAY);
    }

    // if (dma_interrupt_flag_get(DMA_CH2, DMA_INT_FLAG_HTF) ||
    //     dma_interrupt_flag_get(DMA_CH2, DMA_INT_FLAG_FTF)) {
    //     dma_interrupt_flag_clear(DMA_CH2, DMA_INT_FLAG_G);
    //     uart_check_rx_dma(&g_uart_ctx[PLAT_UART_DISPLAY]);
    // }

    if (dma_interrupt_flag_get(DMA_CH2, DMA_INT_FLAG_HTF)) {
        dma_interrupt_flag_clear(DMA_CH2, DMA_INT_FLAG_HTF);

        (void) lwrb_write(g_uart_ctx[PLAT_UART_DISPLAY].rx_rb,
                          &g_uart_ctx[PLAT_UART_DISPLAY].rx_raw_buf[0],
                          g_uart_ctx[PLAT_UART_DISPLAY].rx_buf_size / 2);
    }

    if (dma_interrupt_flag_get(DMA_CH2, DMA_INT_FLAG_FTF)) {
        dma_interrupt_flag_clear(DMA_CH2, DMA_INT_FLAG_FTF);

        (void) lwrb_write(g_uart_ctx[PLAT_UART_DISPLAY].rx_rb,
                          &g_uart_ctx[PLAT_UART_DISPLAY]
                               .rx_raw_buf[g_uart_ctx[PLAT_UART_DISPLAY].rx_buf_size / 2],
                          g_uart_ctx[PLAT_UART_DISPLAY].rx_buf_size / 2);
    }
}

/**
 * @brief DMA interrupt handler for channels 3 & 4.
 *
 * Assumption mapping:
 * - USART1 TX = CH3
 * - USART1 RX = CH4
 */
void DMA_Channel3_4_IRQHandler(void) {
    if (dma_interrupt_flag_get(DMA_CH3, DMA_INT_FLAG_FTF)) {
        dma_interrupt_flag_clear(DMA_CH3, DMA_INT_FLAG_G);
        uart_handle_tx_complete(PLAT_UART_SENSOR);
    }

    // if (dma_interrupt_flag_get(DMA_CH4, DMA_INT_FLAG_HTF) ||
    //     dma_interrupt_flag_get(DMA_CH4, DMA_INT_FLAG_FTF)) {
    //     dma_interrupt_flag_clear(DMA_CH4, DMA_INT_FLAG_G);
    //     uart_check_rx_dma(&g_uart_ctx[PLAT_UART_SENSOR]);
    // }

    if (dma_interrupt_flag_get(DMA_CH4, DMA_INT_FLAG_HTF)) {
        dma_interrupt_flag_clear(DMA_CH4, DMA_INT_FLAG_HTF);

        (void) lwrb_write(g_uart_ctx[PLAT_UART_SENSOR].rx_rb,
                          &g_uart_ctx[PLAT_UART_SENSOR].rx_raw_buf[0],
                          g_uart_ctx[PLAT_UART_SENSOR].rx_buf_size / 2);
    }

    if (dma_interrupt_flag_get(DMA_CH4, DMA_INT_FLAG_FTF)) {
        dma_interrupt_flag_clear(DMA_CH4, DMA_INT_FLAG_FTF);

        (void) lwrb_write(
            g_uart_ctx[PLAT_UART_SENSOR].rx_rb,
            &g_uart_ctx[PLAT_UART_SENSOR].rx_raw_buf[g_uart_ctx[PLAT_UART_SENSOR].rx_buf_size / 2],
            g_uart_ctx[PLAT_UART_SENSOR].rx_buf_size / 2);
    }
}

/**
 * @brief USART0 interrupt handler (unused).
 */
void USART0_IRQHandler(void) {
}

/**
 * @brief USART1 interrupt handler (unused).
 */
void USART1_IRQHandler(void) {
}
