#include "platform/platform_therapy.h"

#include <stddef.h>

#include "bsp/bsp_isp_gpio.h"
#include "bsp/bsp_isp_uart.h"
#include "platform/platform_system.h"
#include "stm32h743_rom_boot.h"

#define PLATFORM_THERAPY_COMMAND_TIMEOUT_MS 1000U
#define PLATFORM_THERAPY_WRITE_TIMEOUT_MS   2000U
#define PLATFORM_THERAPY_ERASE_TIMEOUT_MS   120000U
#define PLATFORM_THERAPY_RESET_SETTLE_MS    50U
#define PLATFORM_THERAPY_V91_BANK2_GUARD_MS 60000U

static stm32h743_rom_boot_t s_target;
static uint8_t s_initialized;

static stm32h743_rom_boot_status_t MapFirmwareToRomBoot(firmware_status_t status)
{
    switch (status)
    {
        case FIRMWARE_STATUS_OK:
            return STM32H743_ROM_BOOT_STATUS_OK;
        case FIRMWARE_STATUS_INVALID_ARGUMENT:
            return STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT;
        case FIRMWARE_STATUS_INVALID_STATE:
            return STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
        case FIRMWARE_STATUS_OUT_OF_RANGE:
            return STM32H743_ROM_BOOT_STATUS_OUT_OF_RANGE;
        case FIRMWARE_STATUS_TIMEOUT:
            return STM32H743_ROM_BOOT_STATUS_TIMEOUT;
        case FIRMWARE_STATUS_NOT_SUPPORTED:
            return STM32H743_ROM_BOOT_STATUS_NOT_SUPPORTED;
        default:
            return STM32H743_ROM_BOOT_STATUS_IO_ERROR;
    }
}

static firmware_status_t MapRomBootStatus(stm32h743_rom_boot_status_t status)
{
    switch (status)
    {
        case STM32H743_ROM_BOOT_STATUS_OK:
            return FIRMWARE_STATUS_OK;
        case STM32H743_ROM_BOOT_STATUS_INVALID_ARGUMENT:
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        case STM32H743_ROM_BOOT_STATUS_INVALID_STATE:
            return FIRMWARE_STATUS_INVALID_STATE;
        case STM32H743_ROM_BOOT_STATUS_OUT_OF_RANGE:
            return FIRMWARE_STATUS_OUT_OF_RANGE;
        case STM32H743_ROM_BOOT_STATUS_TIMEOUT:
            return FIRMWARE_STATUS_TIMEOUT;
        case STM32H743_ROM_BOOT_STATUS_NOT_SUPPORTED:
            return FIRMWARE_STATUS_NOT_SUPPORTED;
        case STM32H743_ROM_BOOT_STATUS_WRONG_DEVICE:
            return FIRMWARE_STATUS_NOT_FOUND;
        case STM32H743_ROM_BOOT_STATUS_VERIFY_FAILED:
            return FIRMWARE_STATUS_AUTHENTICATION_FAILED;
        case STM32H743_ROM_BOOT_STATUS_NACK:
        case STM32H743_ROM_BOOT_STATUS_PROTOCOL_ERROR:
        case STM32H743_ROM_BOOT_STATUS_IO_ERROR:
        default:
            return FIRMWARE_STATUS_IO_ERROR;
    }
}

static stm32h743_rom_boot_status_t ConfigureUart(void *context, stm32h743_rom_boot_uart_mode_t mode)
{
    bsp_isp_uart_mode_t bsp_mode;

    (void) context;
    bsp_mode = (mode == STM32H743_ROM_BOOT_UART_ROM_8E1) ? BSP_ISP_UART_ROM_MODE
                                                         : BSP_ISP_UART_APPLICATION_MODE;
    return MapFirmwareToRomBoot(BspIspUart_Configure(bsp_mode));
}

static stm32h743_rom_boot_status_t Transmit(void *context, const uint8_t *data, uint32_t size,
                                            uint32_t timeout_ms)
{
    (void) context;
    return MapFirmwareToRomBoot(BspIspUart_Transmit(data, size, timeout_ms));
}

static stm32h743_rom_boot_status_t Receive(void *context, uint8_t *data, uint32_t size,
                                           uint32_t timeout_ms)
{
    (void) context;
    return MapFirmwareToRomBoot(BspIspUart_Receive(data, size, timeout_ms));
}

static stm32h743_rom_boot_status_t SetBoot0(void *context, int high)
{
    (void) context;
    return (BspIspGpio_SetBoot0(high) == BSP_ISP_GPIO_OK) ? STM32H743_ROM_BOOT_STATUS_OK
                                                          : STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
}

static stm32h743_rom_boot_status_t ResetTarget(void *context)
{
    (void) context;
    return (BspIspGpio_ResetTarget() == BSP_ISP_GPIO_OK) ? STM32H743_ROM_BOOT_STATUS_OK
                                                         : STM32H743_ROM_BOOT_STATUS_INVALID_STATE;
}

static void DelayMs(void *context, uint32_t delay_ms)
{
    (void) context;
    PlatformSystem_DelayMs(delay_ms);
}

firmware_status_t PlatformTherapy_Init(void)
{
    const stm32h743_rom_boot_port_t port     = {.context        = NULL,
                                                .configure_uart = ConfigureUart,
                                                .transmit       = Transmit,
                                                .receive        = Receive,
                                                .set_boot0      = SetBoot0,
                                                .reset_target   = ResetTarget,
                                                .delay_ms       = DelayMs};
    const stm32h743_rom_boot_config_t config = {
        .command_timeout_ms       = PLATFORM_THERAPY_COMMAND_TIMEOUT_MS,
        .write_timeout_ms         = PLATFORM_THERAPY_WRITE_TIMEOUT_MS,
        .erase_timeout_ms         = PLATFORM_THERAPY_ERASE_TIMEOUT_MS,
        .reset_settle_ms          = PLATFORM_THERAPY_RESET_SETTLE_MS,
        .v91_bank2_erase_guard_ms = PLATFORM_THERAPY_V91_BANK2_GUARD_MS};
    stm32h743_rom_boot_status_t status;

    if (s_initialized != 0U)
    {
        return FIRMWARE_STATUS_OK;
    }

    BspIspGpio_Init();
    status = Stm32H743RomBoot_Init(&s_target, &port, &config);
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return MapRomBootStatus(status);
    }

    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformTherapy_BeginUpdate(platform_therapy_info_t *info)
{
    stm32h743_rom_boot_info_t driver_info;
    stm32h743_rom_boot_status_t status;

    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    status = Stm32H743RomBoot_Enter(&s_target);
    if (status == STM32H743_ROM_BOOT_STATUS_OK)
    {
        status = Stm32H743RomBoot_GetInfo(&s_target, &driver_info);
    }
    if (status != STM32H743_ROM_BOOT_STATUS_OK)
    {
        return MapRomBootStatus(status);
    }

    if (info != NULL)
    {
        info->bootloader_version = driver_info.bootloader_version;
        info->device_id          = driver_info.device_id;
    }
    return FIRMWARE_STATUS_OK;
}

firmware_status_t PlatformTherapy_Read(uint32_t address, void *data, uint32_t size)
{
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return MapRomBootStatus(Stm32H743RomBoot_Read(&s_target, address, data, size));
}

firmware_status_t PlatformTherapy_Erase(uint32_t address, uint32_t size)
{
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return MapRomBootStatus(Stm32H743RomBoot_Erase(&s_target, address, size));
}

firmware_status_t PlatformTherapy_Write(uint32_t address, const void *data, uint32_t size)
{
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return MapRomBootStatus(Stm32H743RomBoot_Write(&s_target, address, data, size));
}

firmware_status_t PlatformTherapy_Verify(uint32_t address, const void *data, uint32_t size)
{
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return MapRomBootStatus(Stm32H743RomBoot_Verify(&s_target, address, data, size));
}

firmware_status_t PlatformTherapy_EndUpdate(void)
{
    if (s_initialized == 0U)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    return MapRomBootStatus(Stm32H743RomBoot_Leave(&s_target));
}
