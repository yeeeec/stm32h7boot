/**
 * @file at24_test.c
 * @brief Host tests for the AT24C128 driver and Boot Control adapter.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adapters/at24_boot_control_adapter.h"
#include "at24.h"

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
    uint8_t bytes[AT24C128_CAPACITY_BYTES];
    uint32_t now_ms;
    uint32_t probe_calls;
    uint32_t ready_after_probes;
    firmware_status_t read_status;
    firmware_status_t write_status;
    firmware_status_t probe_status;
    firmware_status_t wp_status;
    uint8_t last_device_address;
    int write_enabled;
    uint32_t wp_calls;
} fake_port_t;

static firmware_status_t PortRead(
    void *context,
    uint8_t device_address_7bit,
    uint16_t memory_address,
    uint8_t *data,
    uint32_t size)
{
    fake_port_t *port = (fake_port_t *)context;

    port->last_device_address = device_address_7bit;
    if (!FirmwareStatus_IsOk(port->read_status))
    {
        return port->read_status;
    }
    memcpy(data, &port->bytes[memory_address], size);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t PortWrite(
    void *context,
    uint8_t device_address_7bit,
    uint16_t memory_address,
    const uint8_t *data,
    uint32_t size)
{
    fake_port_t *port = (fake_port_t *)context;

    port->last_device_address = device_address_7bit;
    if (!FirmwareStatus_IsOk(port->write_status))
    {
        return port->write_status;
    }
    memcpy(&port->bytes[memory_address], data, size);
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t PortProbeReady(
    void *context,
    uint8_t device_address_7bit,
    int *ready)
{
    fake_port_t *port = (fake_port_t *)context;

    port->last_device_address = device_address_7bit;
    ++port->probe_calls;
    if (!FirmwareStatus_IsOk(port->probe_status))
    {
        return port->probe_status;
    }
    *ready = (port->probe_calls >= port->ready_after_probes) ? 1 : 0;
    return FIRMWARE_STATUS_OK;
}

static uint32_t PortNowMs(void *context)
{
    return ((fake_port_t *)context)->now_ms;
}

static firmware_status_t PortSetWriteEnabled(void *context, int enabled)
{
    fake_port_t *port = (fake_port_t *)context;

    ++port->wp_calls;
    if (!FirmwareStatus_IsOk(port->wp_status))
    {
        return port->wp_status;
    }
    port->write_enabled = enabled;
    return FIRMWARE_STATUS_OK;
}

static int DeviceInit(
    at24_t *device,
    fake_port_t *fake,
    uint32_t timeout_ms)
{
    at24_port_t port;
    at24_config_t config;

    memset(device, 0, sizeof(*device));
    memset(fake, 0, sizeof(*fake));
    memset(fake->bytes, 0xFF, sizeof(fake->bytes));
    fake->read_status = FIRMWARE_STATUS_OK;
    fake->write_status = FIRMWARE_STATUS_OK;
    fake->probe_status = FIRMWARE_STATUS_OK;
    fake->wp_status = FIRMWARE_STATUS_OK;
    fake->ready_after_probes = 1U;
    port.context = fake;
    port.read = PortRead;
    port.write = PortWrite;
    port.probe_ready = PortProbeReady;
    port.now_ms = PortNowMs;
    port.set_write_enabled = PortSetWriteEnabled;
    config.device_address_7bit = 0x50U;
    config.write_timeout_ms = timeout_ms;
    TEST_ASSERT(At24_Init(device, &port, &config) == FIRMWARE_STATUS_OK);
    return 0;
}

static int TestGeometryAndAddress(void)
{
    at24_t device;
    fake_port_t fake;
    at24_info_t info;
    at24_port_t port;
    at24_config_t config;

    TEST_ASSERT(DeviceInit(&device, &fake, 10U) == 0);
    TEST_ASSERT(At24_Probe(&device) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(At24_GetInfo(&device, &info) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((info.capacity_bytes == 16384U) && (info.page_size == 64U));

    port = device.port;
    config.device_address_7bit = 0x49U;
    config.write_timeout_ms = 10U;
    memset(&device, 0, sizeof(device));
    TEST_ASSERT(
        At24_Init(&device, &port, &config) ==
        FIRMWARE_STATUS_INVALID_ARGUMENT);
    TEST_ASSERT(DeviceInit(&device, &fake, 10U) == 0);
    fake.ready_after_probes = 2U;
    TEST_ASSERT(At24_Probe(&device) == FIRMWARE_STATUS_IO_ERROR);
    return 0;
}

static int TestPageWriteAndAdapter(void)
{
    at24_t device;
    fake_port_t fake;
    at24_boot_control_adapter_t adapter;
    const boot_control_store_t *store;
    boot_control_store_info_t info;
    uint8_t source[4] = {1U, 2U, 3U, 4U};
    uint8_t readback[4] = {0U};
    int ready;

    TEST_ASSERT(DeviceInit(&device, &fake, 10U) == 0);
    fake.ready_after_probes = 2U;
    TEST_ASSERT(
        At24BootControlAdapter_Init(&adapter, &device) == FIRMWARE_STATUS_OK);
    store = At24BootControlAdapter_Interface(&adapter);
    TEST_ASSERT(store->get_info(store->context, &info) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((info.capacity_bytes == 16384U) && (info.page_size == 64U));
    TEST_ASSERT(
        store->write_page(store->context, 60U, source, sizeof(source)) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT((fake.write_enabled != 0) && (fake.wp_calls == 1U));
    TEST_ASSERT(
        store->read(store->context, 60U, readback, sizeof(readback)) ==
        FIRMWARE_STATUS_INVALID_STATE);
    TEST_ASSERT(store->is_ready(store->context, &ready) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(ready == 0);
    TEST_ASSERT(store->is_ready(store->context, &ready) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((ready != 0) && (fake.write_enabled == 0) &&
                (fake.wp_calls == 2U));
    TEST_ASSERT(
        store->read(store->context, 60U, readback, sizeof(readback)) ==
        FIRMWARE_STATUS_OK);
    TEST_ASSERT(memcmp(source, readback, sizeof(source)) == 0);
    TEST_ASSERT(fake.last_device_address == 0x50U);
    TEST_ASSERT(
        store->write_page(store->context, 63U, source, 2U) ==
        FIRMWARE_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int TestTimeoutAcrossTickWrap(void)
{
    at24_t device;
    fake_port_t fake;
    at24_operation_result_t result;
    uint8_t value = 0x5AU;

    TEST_ASSERT(DeviceInit(&device, &fake, 5U) == 0);
    fake.ready_after_probes = UINT32_MAX;
    fake.now_ms = UINT32_MAX - 2U;
    TEST_ASSERT(
        At24_WritePageStart(&device, 0U, &value, 1U) == FIRMWARE_STATUS_OK);
    fake.now_ms = 3U;
    TEST_ASSERT(At24_OperationPoll(&device) == FIRMWARE_STATUS_TIMEOUT);
    TEST_ASSERT(
        At24_GetOperationResult(&device, &result) == FIRMWARE_STATUS_OK);
    TEST_ASSERT((result.state == AT24_OPERATION_FAILED) &&
                (result.status == FIRMWARE_STATUS_TIMEOUT));
    TEST_ASSERT(fake.write_enabled == 0);
    return 0;
}

static int TestTransportFailureRestoresProtection(void)
{
    at24_t device;
    fake_port_t fake;
    uint8_t value = 0xA5U;

    TEST_ASSERT(DeviceInit(&device, &fake, 10U) == 0);
    fake.write_status = FIRMWARE_STATUS_IO_ERROR;
    TEST_ASSERT(
        At24_WritePageStart(&device, 0U, &value, 1U) ==
        FIRMWARE_STATUS_IO_ERROR);
    TEST_ASSERT((fake.write_enabled == 0) && (fake.wp_calls == 2U));

    TEST_ASSERT(DeviceInit(&device, &fake, 10U) == 0);
    fake.probe_status = FIRMWARE_STATUS_IO_ERROR;
    TEST_ASSERT(
        At24_WritePageStart(&device, 0U, &value, 1U) == FIRMWARE_STATUS_OK);
    TEST_ASSERT(At24_OperationPoll(&device) == FIRMWARE_STATUS_IO_ERROR);
    TEST_ASSERT((fake.write_enabled == 0) && (fake.wp_calls == 2U));
    return 0;
}

int main(void)
{
    TEST_ASSERT(TestGeometryAndAddress() == 0);
    TEST_ASSERT(TestPageWriteAndAdapter() == 0);
    TEST_ASSERT(TestTimeoutAcrossTickWrap() == 0);
    TEST_ASSERT(TestTransportFailureRestoresProtection() == 0);
    printf("at24_test: PASS\n");
    return 0;
}
