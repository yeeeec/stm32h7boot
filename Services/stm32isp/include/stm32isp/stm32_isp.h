#ifndef STM32_ISP_H
#define STM32_ISP_H

/*
 * Portable host-side driver for the STM32 ROM USART bootloader (AN3155).
 *
 * The host controls the target's BOOT0 and NRST pins and communicates with
 * the bootloader through a UART. Configure the UART before calling this
 * module. Most STM32 ROM bootloaders use 8 data bits, even parity, 1 stop bit
 * (8E1). Check AN2606 for the UART instance/pins supported by the target MCU.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STM32_ISP_MAX_WRITE_SIZE       256u
#define STM32_ISP_DEFAULT_TIMEOUT_MS   1000u
#define STM32_ISP_DEFAULT_ERASE_MS     120000u

/* Return values expected from the registered UART callbacks. */
#define STM32_ISP_PORT_OK               0
#define STM32_ISP_PORT_TIMEOUT         -1

typedef enum {
    STM32_ISP_OK = 0,
    STM32_ISP_ERROR_INVALID_ARGUMENT = -1,
    STM32_ISP_ERROR_IO = -2,
    STM32_ISP_ERROR_TIMEOUT = -3,
    STM32_ISP_ERROR_NACK = -4,
    STM32_ISP_ERROR_PROTOCOL = -5,
    STM32_ISP_ERROR_UNSUPPORTED = -6,
    STM32_ISP_ERROR_VERIFY = -7,
    STM32_ISP_ERROR_NOT_SYNCHRONIZED = -8
} stm32_isp_status_t;

/*
 * UART callbacks return STM32_ISP_PORT_OK on success,
 * STM32_ISP_PORT_TIMEOUT on timeout, and any other value on I/O failure.
 * uart_read must read exactly 'length' bytes or return an error.
 */
typedef int (*stm32_isp_uart_write_fn)(const uint8_t *data,
                                       size_t length,
                                       uint32_t timeout_ms,
                                       void *user_context);
typedef int (*stm32_isp_uart_read_fn)(uint8_t *data,
                                      size_t length,
                                      uint32_t timeout_ms,
                                      void *user_context);

/* level_high is the actual electrical output level driven on the pin. */
typedef void (*stm32_isp_gpio_write_fn)(bool level_high,
                                        void *user_context);
typedef void (*stm32_isp_delay_ms_fn)(uint32_t delay_ms,
                                      void *user_context);

typedef struct {
    stm32_isp_uart_write_fn uart_write;
    stm32_isp_uart_read_fn uart_read;
    stm32_isp_gpio_write_fn boot0_write;
    stm32_isp_gpio_write_fn nrst_write;
    stm32_isp_delay_ms_fn delay_ms;
    void *user_context;
} stm32_isp_port_t;

typedef struct {
    stm32_isp_port_t port;
    uint32_t command_timeout_ms;
    uint32_t erase_timeout_ms;
    bool synchronized;
    bool commands_known;
    bool supports_read_memory;
    bool supports_erase;
    bool supports_extended_erase;
} stm32_isp_t;

/* Initialize the context. This function does not access any hardware. */
stm32_isp_status_t stm32_isp_init(stm32_isp_t *isp,
                                  const stm32_isp_port_t *port);

/* Optional timeout override. Passing 0 keeps the current value. */
void stm32_isp_set_timeouts(stm32_isp_t *isp,
                            uint32_t command_timeout_ms,
                            uint32_t erase_timeout_ms);

/*
 * Drive BOOT0 high, pulse active-low NRST, and wait for the ROM bootloader.
 * This only changes pins; call stm32_isp_sync() afterwards.
 */
stm32_isp_status_t stm32_isp_enter_bootloader(stm32_isp_t *isp);

/* Send the AN3155 synchronization byte (0x7F) and wait for ACK (0x79). */
stm32_isp_status_t stm32_isp_sync(stm32_isp_t *isp);

/*
 * Read the ROM bootloader version and supported command list.
 * commands may be NULL if only the version/capabilities are required.
 * command_count receives the total number of commands reported, even when
 * command_capacity is smaller.
 */
stm32_isp_status_t stm32_isp_get_commands(stm32_isp_t *isp,
                                          uint8_t *bootloader_version,
                                          uint8_t *commands,
                                          size_t command_capacity,
                                          size_t *command_count);

/* Read the 16-bit STM32 product ID returned by the Get ID command. */
stm32_isp_status_t stm32_isp_get_id(stm32_isp_t *isp,
                                    uint16_t *product_id);

/* Low-level AN3155 memory operations. */
stm32_isp_status_t stm32_isp_read_memory(stm32_isp_t *isp,
                                         uint32_t address,
                                         uint8_t *data,
                                         size_t length);

stm32_isp_status_t stm32_isp_write_memory(stm32_isp_t *isp,
                                          uint32_t address,
                                          const uint8_t *data,
                                          size_t length);

/* Select Extended Erase (0x44) when supported, otherwise Erase (0x43). */
stm32_isp_status_t stm32_isp_mass_erase(stm32_isp_t *isp);

/* Ask the ROM bootloader to execute code at address. */
stm32_isp_status_t stm32_isp_go(stm32_isp_t *isp,
                                uint32_t address);

/*
 * Program an image in <=256-byte packets. start_address must be 4-byte
 * aligned. A non-aligned final packet is padded with erased value 0xFF.
 * When verify_after_write is true, each packet is read back and compared.
 * The caller must perform erase first when required.
 */
stm32_isp_status_t stm32_isp_program_image(stm32_isp_t *isp,
                                           uint32_t start_address,
                                           const uint8_t *image,
                                           size_t image_size,
                                           bool verify_after_write);

/* Drive BOOT0 low, pulse active-low NRST, and boot user Flash. */
stm32_isp_status_t stm32_isp_reset_to_flash(stm32_isp_t *isp);

const char *stm32_isp_status_string(stm32_isp_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* STM32_ISP_H */
