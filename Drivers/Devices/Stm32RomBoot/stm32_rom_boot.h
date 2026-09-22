#ifndef STM32_ROM_BOOT_H
#define STM32_ROM_BOOT_H

#include <stdint.h>

#include "stm32_rom_boot_devices.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define STM32_ROM_BOOT_MAX_TRANSFER      256U
#define STM32_ROM_BOOT_MAX_COMMAND_COUNT 32U
#define STM32_ROM_BOOT_MAX_ERASE_UNITS   256U

    typedef enum
    {
        STM32_ROM_BOOT_STATUS_OK = 0,
        STM32_ROM_BOOT_STATUS_INVALID_ARGUMENT,
        STM32_ROM_BOOT_STATUS_INVALID_STATE,
        STM32_ROM_BOOT_STATUS_OUT_OF_RANGE,
        STM32_ROM_BOOT_STATUS_IO_ERROR,
        STM32_ROM_BOOT_STATUS_TIMEOUT,
        STM32_ROM_BOOT_STATUS_NACK,
        STM32_ROM_BOOT_STATUS_PROTOCOL_ERROR,
        STM32_ROM_BOOT_STATUS_NOT_SUPPORTED,
        STM32_ROM_BOOT_STATUS_WRONG_DEVICE,
        STM32_ROM_BOOT_STATUS_VERIFY_FAILED
    } stm32_rom_boot_status_t;

    typedef enum
    {
        STM32_ROM_BOOT_UART_APPLICATION = 0,
        STM32_ROM_BOOT_UART_ROM_8E1
    } stm32_rom_boot_uart_mode_t;

    typedef stm32_rom_boot_status_t (*stm32_rom_boot_configure_uart_fn)(
        void *context, stm32_rom_boot_uart_mode_t mode);

    typedef stm32_rom_boot_status_t (*stm32_rom_boot_transmit_fn)(void *context,
                                                                  const uint8_t *data,
                                                                  uint32_t size,
                                                                  uint32_t timeout_ms);

    typedef stm32_rom_boot_status_t (*stm32_rom_boot_receive_fn)(void *context, uint8_t *data,
                                                                 uint32_t size,
                                                                 uint32_t timeout_ms);

    typedef stm32_rom_boot_status_t (*stm32_rom_boot_set_boot0_fn)(void *context, int high);

    typedef stm32_rom_boot_status_t (*stm32_rom_boot_set_reset_fn)(void *context, int asserted);

    typedef void (*stm32_rom_boot_delay_ms_fn)(void *context, uint32_t delay_ms);

    typedef struct
    {
        void *context;

        stm32_rom_boot_configure_uart_fn configure_uart;
        stm32_rom_boot_transmit_fn transmit;
        stm32_rom_boot_receive_fn receive;
        stm32_rom_boot_set_boot0_fn set_boot0;
        stm32_rom_boot_set_reset_fn set_reset;
        stm32_rom_boot_delay_ms_fn delay_ms;
    } stm32_rom_boot_port_t;

    typedef struct
    {
        stm32_rom_boot_target_t target;

        uint32_t command_timeout_ms;
        uint32_t write_timeout_ms;
        uint32_t erase_timeout_ms;
        uint32_t reset_settle_ms;

        /*
         * STM32H743 ROM bootloader V9.1 workaround inherited from
         * the original H743-specific implementation.
         *
         * Used only when:
         *
         * 1. selected profile enables
         *    STM32_ROM_BOOT_DEVICE_QUIRK_H743_V91_BANK2_ERASE_GUARD
         *
         * 2. bootloader version == 0x91
         *
         * 3. erase operation touches Bank 2
         *
         * Zero means Bank-2 erase is rejected instead of assuming
         * an arbitrary safe delay.
         */
        uint32_t v91_bank2_erase_guard_ms;

    } stm32_rom_boot_config_t;

    typedef struct
    {
        uint8_t bootloader_version;
        uint16_t device_id;

        uint8_t command_count;
        uint8_t commands[STM32_ROM_BOOT_MAX_COMMAND_COUNT];

    } stm32_rom_boot_info_t;

    typedef enum
    {
        STM32_ROM_BOOT_SESSION_CLOSED = 0,
        STM32_ROM_BOOT_SESSION_SYNCED,
        STM32_ROM_BOOT_SESSION_READY,
        STM32_ROM_BOOT_SESSION_FAULTED

    } stm32_rom_boot_session_t;

    typedef struct
    {
        stm32_rom_boot_port_t port;
        stm32_rom_boot_config_t config;

        const stm32_rom_boot_device_profile_t *profile;

        stm32_rom_boot_info_t info;
        stm32_rom_boot_session_t session;

        int initialized;

    } stm32_rom_boot_t;

    stm32_rom_boot_status_t Stm32RomBoot_Init(stm32_rom_boot_t *device,
                                              const stm32_rom_boot_port_t *port,
                                              const stm32_rom_boot_config_t *config);

    stm32_rom_boot_status_t Stm32RomBoot_Enter(stm32_rom_boot_t *device);

    stm32_rom_boot_status_t Stm32RomBoot_GetInfo(stm32_rom_boot_t *device,
                                                 stm32_rom_boot_info_t *info);

    stm32_rom_boot_status_t Stm32RomBoot_Read(stm32_rom_boot_t *device, uint32_t address,
                                              void *data, uint32_t size);

    stm32_rom_boot_status_t Stm32RomBoot_Erase(stm32_rom_boot_t *device, uint32_t address,
                                               uint32_t size);

    stm32_rom_boot_status_t Stm32RomBoot_Write(stm32_rom_boot_t *device, uint32_t address,
                                               const void *data, uint32_t size);

    stm32_rom_boot_status_t Stm32RomBoot_Verify(stm32_rom_boot_t *device, uint32_t address,
                                                const void *data, uint32_t size);

    stm32_rom_boot_status_t Stm32RomBoot_Leave(stm32_rom_boot_t *device);

#ifdef __cplusplus
}
#endif

#endif