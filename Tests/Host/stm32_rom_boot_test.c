/**
 * @file stm32_rom_boot_test.c
 * @brief STM32 System Memory UART Bootloader 驱动的 Host 协议帧测试。
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32_rom_boot.h"

#define TEST_BUFFER_SIZE 2048U

#define TEST_ASSERT(condition)                                                \
    do                                                                        \
    {                                                                         \
        if (!(condition))                                                     \
        {                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            return 1;                                                         \
        }                                                                     \
    } while (0)

typedef struct
{
    uint8_t transmitted[TEST_BUFFER_SIZE];
    uint8_t received[TEST_BUFFER_SIZE];
    uint32_t transmitted_size;
    uint32_t received_size;
    uint32_t received_offset;
    uint32_t reset_count;
    uint32_t delay_ms;
    uint32_t configure_count;
    stm32_rom_boot_uart_mode_t last_uart_mode;
    int boot0_high;
} fake_port_t;

static firmware_status_t PortConfigureUart(
    void *context,
    stm32_rom_boot_uart_mode_t mode)
{
    fake_port_t *port = (fake_port_t *)context;

    ++port->configure_count;
    port->last_uart_mode = mode;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t PortTransmit(
    void *context,
    const uint8_t *data,
    uint32_t size,
    uint32_t timeout_ms)
{
    fake_port_t *port = (fake_port_t *)context;

    (void)timeout_ms;
    if ((size > TEST_BUFFER_SIZE) ||
        (port->transmitted_size > (TEST_BUFFER_SIZE - size)))
    {
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(&port->transmitted[port->transmitted_size], data, size);
    port->transmitted_size += size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t PortReceive(
    void *context,
    uint8_t *data,
    uint32_t size,
    uint32_t timeout_ms)
{
    fake_port_t *port = (fake_port_t *)context;

    (void)timeout_ms;
    if ((size > port->received_size) ||
        (port->received_offset > (port->received_size - size)))
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    memcpy(data, &port->received[port->received_offset], size);
    port->received_offset += size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t PortSetBoot0(void *context, int high)
{
    ((fake_port_t *)context)->boot0_high = high;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t PortResetTarget(void *context)
{
    ++((fake_port_t *)context)->reset_count;
    return FIRMWARE_STATUS_OK;
}

static void PortDelayMs(void *context, uint32_t delay_ms)
{
    ((fake_port_t *)context)->delay_ms += delay_ms;
}

static void QueueBytes(fake_port_t *port, const uint8_t *data, uint32_t size)
{
    memcpy(&port->received[port->received_size], data, size);
    port->received_size += size;
}

static int DeviceInit(
    stm32_rom_boot_t *device,
    fake_port_t *fake,
    uint16_t expected_device_id,
    stm32_rom_boot_erase_mode_t erase_mode)
{
    stm32_rom_boot_port_t port;
    stm32_rom_boot_config_t config;

    memset(device, 0, sizeof(*device));
    memset(fake, 0, sizeof(*fake));
    memset(&port, 0, sizeof(port));
    memset(&config, 0, sizeof(config));

    port.context = fake;
    port.configure_uart = PortConfigureUart;
    port.transmit = PortTransmit;
    port.receive = PortReceive;
    port.set_boot0 = PortSetBoot0;
    port.reset_target = PortResetTarget;
    port.delay_ms = PortDelayMs;

    config.command_timeout_ms = 100U;
    config.write_timeout_ms = 500U;
    config.erase_timeout_ms = 1000U;
    config.reset_settle_ms = 10U;
    config.expected_device_id = expected_device_id;
    config.erase_mode = erase_mode;

    TEST_ASSERT(
        Stm32RomBoot_Init(device, &port, &config) == FIRMWARE_STATUS_OK);
    return 0;
}

static void QueueEnterAndInfo(fake_port_t *fake, uint16_t device_id)
{
    static const uint8_t enter_and_get[] = {
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
        0x05U,
        0x31U,
        STM32_ROM_BOOT_COMMAND_GET,
        STM32_ROM_BOOT_COMMAND_GET_ID,
        STM32_ROM_BOOT_COMMAND_READ_MEMORY,
        STM32_ROM_BOOT_COMMAND_WRITE_MEMORY,
        STM32_ROM_BOOT_COMMAND_EXTENDED_ERASE,
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
        0x01U,
    };
    uint8_t id_and_ack[3];

    QueueBytes(fake, enter_and_get, sizeof(enter_and_get));
    id_and_ack[0] = (uint8_t)(device_id >> 8U);
    id_and_ack[1] = (uint8_t)device_id;
    id_and_ack[2] = STM32_ROM_BOOT_ACK_BYTE;
    QueueBytes(fake, id_and_ack, sizeof(id_and_ack));
}

static int TestFullProtocolFrames(void)
{
    stm32_rom_boot_t device;
    stm32_rom_boot_info_t info;
    fake_port_t fake;
    uint8_t readback[4];
    static const uint8_t read_data[] = {0xA1U, 0xB2U, 0xC3U, 0xD4U};
    static const uint8_t write_data[] = {0x11U, 0x22U, 0x33U, 0x44U};
    static const uint8_t operation_responses[] = {
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
        0xA1U,
        0xB2U,
        0xC3U,
        0xD4U,
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
        STM32_ROM_BOOT_ACK_BYTE,
    };
    static const uint8_t expected_transmit[] = {
        0x7FU,
        0x00U, 0xFFU,
        0x02U, 0xFDU,
        0x11U, 0xEEU,
        0x08U, 0x00U, 0x00U, 0x00U, 0x08U,
        0x03U, 0xFCU,
        0x31U, 0xCEU,
        0x08U, 0x00U, 0x00U, 0x04U, 0x0CU,
        0x03U, 0x11U, 0x22U, 0x33U, 0x44U, 0x47U,
        0x44U, 0xBBU,
        0x00U, 0x02U,
        0x00U, 0x02U,
        0x00U, 0x03U,
        0x00U, 0x04U,
        0x07U,
    };

    TEST_ASSERT(
        DeviceInit(&device, &fake, 0x0450U,
                   STM32_ROM_BOOT_ERASE_EXTENDED) == 0);
    QueueEnterAndInfo(&fake, 0x0450U);
    QueueBytes(&fake, operation_responses, sizeof(operation_responses));

    TEST_ASSERT(Stm32RomBoot_Enter(&device) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(Stm32RomBoot_GetInfo(&device, &info) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((info.bootloader_version == 0x31U) &&
                (info.device_id == 0x0450U) &&
                (info.command_count == 5U));
    TEST_ASSERT(
        Stm32RomBoot_IsCommandSupported(
            &device, STM32_ROM_BOOT_COMMAND_WRITE_MEMORY) != 0);

    TEST_ASSERT(
        Stm32RomBoot_ReadMemory(
            &device, 0x08000000UL, readback, sizeof(readback)) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(memcmp(readback, read_data, sizeof(readback)) == 0);
    TEST_ASSERT(
        Stm32RomBoot_WriteMemory(
            &device, 0x08000004UL, write_data, sizeof(write_data)) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        Stm32RomBoot_ErasePages(&device, 2U, 3U) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(Stm32RomBoot_Leave(&device) == FIRMWARE_STATUS_OK);

    TEST_ASSERT(fake.received_offset == fake.received_size);
    TEST_ASSERT(fake.transmitted_size == sizeof(expected_transmit));
    TEST_ASSERT(
        memcmp(fake.transmitted, expected_transmit,
               sizeof(expected_transmit)) == 0);
    TEST_ASSERT((fake.boot0_high == 0) && (fake.reset_count == 2U) &&
                (fake.delay_ms == 10U));
    TEST_ASSERT((fake.configure_count == 2U) &&
                (fake.last_uart_mode ==
                 STM32_ROM_BOOT_UART_APPLICATION_MODE));
    return 0;
}

static int TestRejectsUnexpectedDeviceId(void)
{
    stm32_rom_boot_t device;
    stm32_rom_boot_info_t info;
    fake_port_t fake;

    TEST_ASSERT(
        DeviceInit(&device, &fake, 0x0450U,
                   STM32_ROM_BOOT_ERASE_EXTENDED) == 0);
    QueueEnterAndInfo(&fake, 0x0440U);

    TEST_ASSERT(Stm32RomBoot_Enter(&device) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(
        Stm32RomBoot_GetInfo(&device, &info) == FIRMWARE_STATUS_NOT_FOUND);
    TEST_ASSERT(
        Stm32RomBoot_IsCommandSupported(
            &device, STM32_ROM_BOOT_COMMAND_WRITE_MEMORY) == 0);
    TEST_ASSERT(Stm32RomBoot_Leave(&device) == FIRMWARE_STATUS_OK);
    return 0;
}

static int TestEnterNackRestoresApplication(void)
{
    stm32_rom_boot_t device;
    fake_port_t fake;
    uint8_t nack = STM32_ROM_BOOT_NACK_BYTE;

    TEST_ASSERT(
        DeviceInit(&device, &fake, 0xFFFFU,
                   STM32_ROM_BOOT_ERASE_EXTENDED) == 0);
    QueueBytes(&fake, &nack, 1U);

    TEST_ASSERT(Stm32RomBoot_Enter(&device) == FIRMWARE_STATUS_IO_ERROR);
    TEST_ASSERT((fake.boot0_high == 0) && (fake.reset_count == 2U));
    TEST_ASSERT(fake.last_uart_mode == STM32_ROM_BOOT_UART_APPLICATION_MODE);
    return 0;
}

int main(void)
{
    if (TestFullProtocolFrames() != 0)
    {
        return 1;
    }
    if (TestRejectsUnexpectedDeviceId() != 0)
    {
        return 1;
    }
    if (TestEnterNackRestoresApplication() != 0)
    {
        return 1;
    }

    puts("stm32_rom_boot_test: PASS");
    return 0;
}
