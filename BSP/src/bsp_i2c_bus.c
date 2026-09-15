/**
 * @file bsp_i2c_bus.c
 * @brief Shared I2C1 HAL adapter with task-level serialization.
 */
#include "bsp/bsp_i2c_bus.h"

#include <stddef.h>

#include "cmsis_os2.h"
#include "i2c.h"

#define BSP_I2C_PROBE_LOCK_TIMEOUT_MS 100U

static osMutexId_t s_i2c1_mutex;

static firmware_status_t HalStatus(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
        return FIRMWARE_STATUS_OK;
    if (status == HAL_TIMEOUT)
        return FIRMWARE_STATUS_TIMEOUT;
    if (status == HAL_BUSY)
        return FIRMWARE_STATUS_BUSY;
    return FIRMWARE_STATUS_IO_ERROR;
}

static uint32_t MillisecondsToTicks(uint32_t timeoutMs)
{
    uint64_t ticks;
    uint32_t frequency;
    if (timeoutMs == osWaitForever)
        return osWaitForever;
    if (timeoutMs == 0U)
        return 0U;
    frequency = osKernelGetTickFreq();
    if (frequency == 0U)
        return timeoutMs;
    ticks = ((uint64_t) timeoutMs * frequency + 999U) / 1000U;
    return ticks > UINT32_MAX ? UINT32_MAX : (uint32_t) ticks;
}

static firmware_status_t AcquireBus(uint32_t timeoutMs, int *locked)
{
    osStatus_t status;
    if (locked == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    *locked = 0;

    /* BSP device probing occurs before osKernelInitialize(), when no task can
     * contend for I2C1. Once the kernel is initialized, the mutex is mandatory. */
    if (s_i2c1_mutex == NULL)
        return osKernelGetState() == osKernelInactive ? FIRMWARE_STATUS_OK
                                                      : FIRMWARE_STATUS_INVALID_STATE;

    status = osMutexAcquire(s_i2c1_mutex, MillisecondsToTicks(timeoutMs));
    if (status == osOK)
    {
        *locked = 1;
        return FIRMWARE_STATUS_OK;
    }
    return status == osErrorTimeout || status == osErrorResource ? FIRMWARE_STATUS_TIMEOUT
                                                                 : FIRMWARE_STATUS_IO_ERROR;
}

static void ReleaseBus(int locked)
{
    if (locked != 0)
        (void) osMutexRelease(s_i2c1_mutex);
}

static int IsValidTransfer(uint8_t deviceAddress7bit,
                           bsp_i2c_memory_address_size_t memoryAddressSize, const void *data,
                           uint16_t size)
{
    return deviceAddress7bit <= 0x7FU && data != NULL && size != 0U &&
           (memoryAddressSize == BSP_I2C_MEMORY_ADDRESS_8BIT ||
            memoryAddressSize == BSP_I2C_MEMORY_ADDRESS_16BIT);
}

static uint16_t HalMemoryAddressSize(bsp_i2c_memory_address_size_t size)
{
    return size == BSP_I2C_MEMORY_ADDRESS_8BIT ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT;
}

firmware_status_t BSP_I2cBusRtosInit(void)
{
    static const osMutexAttr_t attributes = {.name = "I2C1Bus", .attr_bits = osMutexPrioInherit};
    if (!BSP_I2c1IsReady())
        return FIRMWARE_STATUS_INVALID_STATE;
    if (s_i2c1_mutex != NULL)
        return FIRMWARE_STATUS_OK;
    if (osKernelGetState() == osKernelInactive)
        return FIRMWARE_STATUS_INVALID_STATE;
    s_i2c1_mutex = osMutexNew(&attributes);
    return s_i2c1_mutex != NULL ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_IO_ERROR;
}

int BSP_I2c1IsReady(void)
{
    return hi2c1.Instance == I2C1 && hi2c1.State != HAL_I2C_STATE_RESET;
}

firmware_status_t BSP_I2c1MemRead(uint8_t deviceAddress7bit, uint16_t memoryAddress,
                                  bsp_i2c_memory_address_size_t memoryAddressSize, uint8_t *data,
                                  uint16_t size, uint32_t timeoutMs)
{
    firmware_status_t status;
    int locked;
    if (!IsValidTransfer(deviceAddress7bit, memoryAddressSize, data, size))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (!BSP_I2c1IsReady())
        return FIRMWARE_STATUS_INVALID_STATE;
    status = AcquireBus(timeoutMs, &locked);
    if (FirmwareStatus_IsOk(status))
        status = HalStatus(HAL_I2C_Mem_Read(&hi2c1, (uint16_t) deviceAddress7bit << 1U,
                                            memoryAddress, HalMemoryAddressSize(memoryAddressSize),
                                            data, size, timeoutMs));
    ReleaseBus(locked);
    return status;
}

firmware_status_t BSP_I2c1MemWrite(uint8_t deviceAddress7bit, uint16_t memoryAddress,
                                   bsp_i2c_memory_address_size_t memoryAddressSize,
                                   const uint8_t *data, uint16_t size, uint32_t timeoutMs)
{
    firmware_status_t status;
    int locked;
    if (!IsValidTransfer(deviceAddress7bit, memoryAddressSize, data, size))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (!BSP_I2c1IsReady())
        return FIRMWARE_STATUS_INVALID_STATE;
    status = AcquireBus(timeoutMs, &locked);
    if (FirmwareStatus_IsOk(status))
        status = HalStatus(HAL_I2C_Mem_Write(&hi2c1, (uint16_t) deviceAddress7bit << 1U,
                                             memoryAddress, HalMemoryAddressSize(memoryAddressSize),
                                             (uint8_t *) data, size, timeoutMs));
    ReleaseBus(locked);
    return status;
}

firmware_status_t BSP_I2c1Probe(uint8_t deviceAddress7bit, uint32_t timeoutMs, int *ready)
{
    firmware_status_t status;
    HAL_StatusTypeDef halStatus;
    int locked;
    if (ready == NULL || deviceAddress7bit > 0x7FU)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (!BSP_I2c1IsReady())
        return FIRMWARE_STATUS_INVALID_STATE;
    *ready = 0;
    status = AcquireBus(BSP_I2C_PROBE_LOCK_TIMEOUT_MS, &locked);
    if (FirmwareStatus_IsError(status))
        return status;
    halStatus = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t) deviceAddress7bit << 1U, 1U, timeoutMs);
    *ready    = halStatus == HAL_OK ? 1 : 0;
    ReleaseBus(locked);
    /* NACK and a short HAL timeout both mean "not ready" to AT24 acknowledge
     * polling. Its driver owns the bounded overall write-cycle timeout. */
    return FIRMWARE_STATUS_OK;
}
