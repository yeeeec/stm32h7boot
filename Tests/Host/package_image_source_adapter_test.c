#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "adapters/package_image_source_adapter.h"

typedef struct
{
    uint8_t data[8];
    uint32_t size;
    uint32_t short_read_by;
} source_fixture_t;

static firmware_status_t GetSize(void *context, uint32_t *size)
{
    source_fixture_t *fixture = (source_fixture_t *)context;

    if ((fixture == NULL) || (size == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    *size = fixture->size;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ReadAt(void *context, uint32_t offset, uint8_t *data,
                                uint32_t size, uint32_t *bytes_read)
{
    source_fixture_t *fixture = (source_fixture_t *)context;

    if ((fixture == NULL) || (data == NULL) || (bytes_read == NULL) ||
        (offset > fixture->size) || (size > (fixture->size - offset)) ||
        (fixture->short_read_by > size))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    memcpy(data, &fixture->data[offset], size - fixture->short_read_by);
    *bytes_read = size - fixture->short_read_by;
    return FIRMWARE_STATUS_OK;
}

int main(void)
{
    source_fixture_t fixture = {{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}, 8U, 0U};
    package_source_t package_source = {0};
    package_image_source_adapter_t adapter = {0};
    firmware_image_info_t info = {0};
    uint8_t bytes[3] = {0};
    const firmware_image_source_t *image;

    package_source.context = &fixture;
    package_source.get_size = GetSize;
    package_source.read_at = ReadAt;

    assert(PackageImageSourceAdapter_Init(&adapter, &package_source) ==
           FIRMWARE_STATUS_OK);
    image = PackageImageSourceAdapter_Interface(&adapter);
    assert(image != NULL);
    assert(image->get_info(image->context, &info) == FIRMWARE_STATUS_OK);
    assert(info.size_bytes == fixture.size);
    assert(image->read(image->context, 2U, bytes, sizeof(bytes)) ==
           FIRMWARE_STATUS_OK);
    assert((bytes[0] == 2U) && (bytes[1] == 3U) && (bytes[2] == 4U));

    fixture.short_read_by = 1U;
    assert(image->read(image->context, 0U, bytes, sizeof(bytes)) ==
           FIRMWARE_STATUS_IO_ERROR);

    fixture.size = 0U;
    assert(image->get_info(image->context, &info) ==
           FIRMWARE_STATUS_INVALID_ARGUMENT);
    assert(PackageImageSourceAdapter_Init(NULL, &package_source) ==
           FIRMWARE_STATUS_INVALID_ARGUMENT);
    assert(PackageImageSourceAdapter_Interface(NULL) == NULL);
    return 0;
}
