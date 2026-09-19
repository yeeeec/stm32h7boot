/**
 * @file stm32h743_rom_boot.h
 * @brief STM32H743XIH6 System-Memory UART bootloader host driver.
 *
 * Fixed target: device ID 0x0450, 2-MiB internal Flash, 16 x 128-KiB
 * sectors. Board-specific UART and GPIO details are supplied through the
 * port callbacks and are intentionally not part of this driver contract.
 */
#ifndef STM32H743_ROM_BOOT_H
#define STM32H743_ROM_BOOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STM32H743_ROM_BOOT_DEVICE_ID          0x0450U
#define STM32H743_ROM_BOOT_FLASH_BASE         0x08000000UL
#define STM32H743_ROM_BOOT_FLASH_SIZE         (2UL * 1024UL * 1024UL)
#define STM32H743_ROM_BOOT_FLASH_END_EXCLUSIVE \
    (STM32H743_ROM_BOOT_FLASH_BASE + STM32H743_ROM_BOOT_FLASH_SIZE)
#define STM32H743_ROM_BOOT_SECTOR_SIZE        (128UL * 1024UL)
#define STM32H743_ROM_BOOT_SECTOR_COUNT       16U
#define STM32H743_ROM_BOOT_BANK2_FIRST_SECTOR 8U
#define STM32H743_ROM_BOOT_MAX_TRANSFER       256U
#define STM32H743_ROM_BOOT_WRITE_ALIGNMENT    4U
#define STM32H743_ROM_BOOT_MAX_COMMAND_COUNT  32U

typedef enum
{
    STM32H743_ROM_BOOT_STATUS_OK = 0,
    STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT,
    STM32H743_ROM_BOOT_STATUS_INVALID_STATE,
    STM32H743_ROM_BOOT_STATUS_OUT_OF_RANGE,
    STM32H743_ROM_BOOT_STATUS_IO_ERROR,
    STM32H743_ROM_BOOT_STATUS_TIMEOUT,
    STM32H743_ROM_BOOT_STATUS_NACK,
    STM32H743_ROM_BOOT_STATUS_PROTOCOL_ERROR,
    STM32H743_ROM_BOOT_STATUS_NOT_SUPPORTED,
    STM32H743_ROM_BOOT_STATUS_WRONG_DEVICE,
    STM32H743_ROM_BOOT_STATUS_VERIFY_FAILED
} stm32h743_rom_boot_status_t;

typedef enum
{
    STM32H743_ROM_BOOT_UART_APPLICATION = 0,
    STM32H743_ROM_BOOT_UART_ROM_8E1
} stm32h743_rom_boot_uart_mode_t;

typedef stm32h743_rom_boot_status_t (*stm32h743_rom_boot_configure_uart_fn)(
    void *context, stm32h743_rom_boot_uart_mode_t mode);
typedef stm32h743_rom_boot_status_t (*stm32h743_rom_boot_transmit_fn)(
    void *context, const uint8_t *data, uint32_t size, uint32_t timeout_ms);
typedef stm32h743_rom_boot_status_t (*stm32h743_rom_boot_receive_fn)(
    void *context, uint8_t *data, uint32_t size, uint32_t timeout_ms);
typedef stm32h743_rom_boot_status_t (*stm32h743_rom_boot_set_boot0_fn)(void *context, int high);
typedef stm32h743_rom_boot_status_t (*stm32h743_rom_boot_set_reset_fn)(void *context,
                                                                       int asserted);
typedef void (*stm32h743_rom_boot_delay_ms_fn)(void *context, uint32_t delay_ms);

typedef struct
{
    void *context;
    stm32h743_rom_boot_configure_uart_fn configure_uart;
    stm32h743_rom_boot_transmit_fn transmit;
    stm32h743_rom_boot_receive_fn receive;
    stm32h743_rom_boot_set_boot0_fn set_boot0;
    stm32h743_rom_boot_set_reset_fn set_reset;
    stm32h743_rom_boot_delay_ms_fn delay_ms;
} stm32h743_rom_boot_port_t;

typedef struct
{
    uint32_t command_timeout_ms;
    uint32_t write_timeout_ms;
    uint32_t erase_timeout_ms;
    uint32_t reset_settle_ms;
    /**
     * Additional delay after a V9.1 ROM erase touching Bank 2. Set this to
     * the board-validated worst-case erase completion guard. A zero value
     * rejects Bank-2 erase when ROM version is 0x91.
     */
    uint32_t v91_bank2_erase_guard_ms;
} stm32h743_rom_boot_config_t;

typedef struct
{
    uint8_t bootloader_version;
    uint16_t device_id;
    uint8_t command_count;
    uint8_t commands[STM32H743_ROM_BOOT_MAX_COMMAND_COUNT];
} stm32h743_rom_boot_info_t;

typedef enum
{
    STM32H743_ROM_BOOT_SESSION_CLOSED = 0,
    STM32H743_ROM_BOOT_SESSION_SYNCED,
    STM32H743_ROM_BOOT_SESSION_READY,
    STM32H743_ROM_BOOT_SESSION_FAULTED
} stm32h743_rom_boot_session_t;

typedef struct
{
    stm32h743_rom_boot_port_t port;
    stm32h743_rom_boot_config_t config;
    stm32h743_rom_boot_info_t info;
    stm32h743_rom_boot_session_t session;
    int initialized;
} stm32h743_rom_boot_t;

stm32h743_rom_boot_status_t Stm32H743RomBoot_Init(
    stm32h743_rom_boot_t *device, const stm32h743_rom_boot_port_t *port,
    const stm32h743_rom_boot_config_t *config);
stm32h743_rom_boot_status_t Stm32H743RomBoot_Enter(stm32h743_rom_boot_t *device);
stm32h743_rom_boot_status_t Stm32H743RomBoot_GetInfo(
    stm32h743_rom_boot_t *device, stm32h743_rom_boot_info_t *info);
stm32h743_rom_boot_status_t Stm32H743RomBoot_Read(
    stm32h743_rom_boot_t *device, uint32_t address, void *data, uint32_t size);
/** Erases every 128-KiB sector intersecting [address, address + size). */
stm32h743_rom_boot_status_t Stm32H743RomBoot_Erase(
    stm32h743_rom_boot_t *device, uint32_t address, uint32_t size);
/** Writes arbitrary data; address must be 4-byte aligned, final data is padded with 0xFF. */
stm32h743_rom_boot_status_t Stm32H743RomBoot_Write(
    stm32h743_rom_boot_t *device, uint32_t address, const void *data, uint32_t size);
stm32h743_rom_boot_status_t Stm32H743RomBoot_Verify(
    stm32h743_rom_boot_t *device, uint32_t address, const void *data, uint32_t size);
stm32h743_rom_boot_status_t Stm32H743RomBoot_Leave(stm32h743_rom_boot_t *device);

#ifdef __cplusplus
}
#endif

#endif
