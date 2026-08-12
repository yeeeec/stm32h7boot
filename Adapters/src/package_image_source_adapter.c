/**
 * @file package_image_source_adapter.c
 * @brief Package-source to random-access image-source conversion.
 */
#include "adapters/package_image_source_adapter.h"

#include <stddef.h>

static firmware_status_t GetInfo(void *context, firmware_image_info_t *info)
{
    const package_image_source_adapter_t *adapter =
        (const package_image_source_adapter_t *) context;
    firmware_status_t status;

    if ((adapter == NULL) || (adapter->package_source == NULL) || (info == NULL) ||
        (adapter->package_source->get_size == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status = adapter->package_source->get_size(adapter->package_source->context, &info->size_bytes);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    return (info->size_bytes == 0U) ? FIRMWARE_STATUS_INVALID_ARGUMENT : FIRMWARE_STATUS_OK;
}

static firmware_status_t Read(void *context, uint32_t offset, uint8_t *data, uint32_t size)
{
    const package_image_source_adapter_t *adapter =
        (const package_image_source_adapter_t *) context;
    uint32_t bytes_read = 0U;
    firmware_status_t status;

    if ((adapter == NULL) || (adapter->package_source == NULL) || (data == NULL) || (size == 0U) ||
        (adapter->package_source->read_at == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    status = adapter->package_source->read_at(adapter->package_source->context, offset, data, size,
                                              &bytes_read);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    /* A short read is never a successful image transaction. */
    return (bytes_read == size) ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_IO_ERROR;
}

firmware_status_t PackageImageSourceAdapter_Init(package_image_source_adapter_t *adapter,
                                                 const package_source_t *package_source)
{
    if ((adapter == NULL) || (package_source == NULL) || (package_source->get_size == NULL) ||
        (package_source->read_at == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    adapter->package_source     = package_source;
    adapter->interface.context  = adapter;
    adapter->interface.get_info = GetInfo;
    adapter->interface.read     = Read;
    return FIRMWARE_STATUS_OK;
}

const firmware_image_source_t *
PackageImageSourceAdapter_Interface(const package_image_source_adapter_t *adapter)
{
    return (adapter == NULL) ? NULL : &adapter->interface;
}
