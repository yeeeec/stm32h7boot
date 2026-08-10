#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "adapters/fatfs_package_source_adapter.h"
#include "fatfs_test_support.h"

static const uint8_t file_data[] = "abcdefgh";

static void ResetAdapters(fatfs_release_volume_context_t *volume,
                          fatfs_package_source_adapter_t *adapter)
{
    FatFsTest_Reset();
    assert(FatFsReleaseVolumeContext_Init(volume) == FIRMWARE_STATUS_OK);
    assert(FatFsPackageSourceAdapter_Init(adapter, volume) == FIRMWARE_STATUS_OK);
}

static void TestFixedFileMappingAndRead(void)
{
    fatfs_release_volume_context_t volume;
    fatfs_package_source_adapter_t adapter;
    const package_source_t *source;
    uint8_t read_buffer[3];
    uint32_t size;
    uint32_t bytes_read;

    ResetAdapters(&volume, &adapter);
    source = FatFsPackageSourceAdapter_Interface(&adapter);
    assert(source != NULL);
    assert(source->open(source->context, PACKAGE_FILE_APP) == FIRMWARE_STATUS_INVALID_STATE);
    assert(source->mount(source->context) == FIRMWARE_STATUS_OK);

    fatfs_test_state.file_size = sizeof(file_data) - 1U;
    fatfs_test_state.file_data = file_data;
    assert(source->open(source->context, PACKAGE_FILE_MANIFEST) == FIRMWARE_STATUS_OK);
    assert(strcmp(fatfs_test_state.last_path, "0:firmware/manifest.json") == 0);
    assert(source->get_size(source->context, &size) == FIRMWARE_STATUS_OK);
    assert(size == sizeof(file_data) - 1U);
    assert(source->read_at(source->context, 2U, read_buffer, sizeof(read_buffer),
                           &bytes_read) == FIRMWARE_STATUS_OK);
    assert(bytes_read == sizeof(read_buffer));
    assert(memcmp(read_buffer, "cde", sizeof(read_buffer)) == 0);
    assert(source->close(source->context) == FIRMWARE_STATUS_OK);

    assert(source->open(source->context, PACKAGE_FILE_APP) == FIRMWARE_STATUS_OK);
    assert(strcmp(fatfs_test_state.last_path, "0:firmware/hmi.app.bin") == 0);
    assert(source->close(source->context) == FIRMWARE_STATUS_OK);
    assert(source->open(source->context, PACKAGE_FILE_GUI) == FIRMWARE_STATUS_OK);
    assert(strcmp(fatfs_test_state.last_path, "0:firmware/hmi.gui.bin") == 0);
    assert(source->close(source->context) == FIRMWARE_STATUS_OK);
    assert(source->open(source->context, PACKAGE_FILE_THERAPY_APP) ==
           FIRMWARE_STATUS_OK);
    assert(strcmp(fatfs_test_state.last_path, "0:firmware/therapy.app.bin") == 0);
    assert(source->close(source->context) == FIRMWARE_STATUS_OK);
    assert(source->unmount(source->context) == FIRMWARE_STATUS_OK);
}

static void TestErrorsAndLifecycle(void)
{
    fatfs_release_volume_context_t volume;
    fatfs_package_source_adapter_t adapter;
    const package_source_t *source;
    uint8_t read_buffer[4];
    uint32_t bytes_read;

    ResetAdapters(&volume, &adapter);
    source = FatFsPackageSourceAdapter_Interface(&adapter);
    fatfs_test_state.media_present = 0U;
    assert(source->mount(source->context) == FIRMWARE_STATUS_INVALID_STATE);
    fatfs_test_state.media_present = 1U;
    assert(source->mount(source->context) == FIRMWARE_STATUS_OK);
    assert(source->mount(source->context) == FIRMWARE_STATUS_INVALID_STATE);
    assert(source->open(source->context, (package_file_id_t)99) ==
           FIRMWARE_STATUS_INVALID_ARGUMENT);
    assert(fatfs_test_state.open_calls == 0U);

    fatfs_test_state.open_result = FR_NO_FILE;
    assert(source->open(source->context, PACKAGE_FILE_APP) == FIRMWARE_STATUS_NOT_FOUND);
    fatfs_test_state.open_result = FR_OK;
    fatfs_test_state.file_size = sizeof(file_data) - 1U;
    fatfs_test_state.file_data = file_data;
    assert(source->open(source->context, PACKAGE_FILE_APP) == FIRMWARE_STATUS_OK);
    assert(source->unmount(source->context) == FIRMWARE_STATUS_INVALID_STATE);
    assert(source->read_at(source->context, 7U, read_buffer, sizeof(read_buffer),
                           &bytes_read) == FIRMWARE_STATUS_OUT_OF_RANGE);
    fatfs_test_state.use_read_limit = 1;
    fatfs_test_state.read_limit = 2U;
    assert(source->read_at(source->context, 0U, read_buffer, sizeof(read_buffer),
                           &bytes_read) == FIRMWARE_STATUS_OK);
    assert(bytes_read == 2U);
    fatfs_test_state.use_read_limit = 0;
    fatfs_test_state.read_result = FR_DISK_ERR;
    assert(source->read_at(source->context, 0U, read_buffer, sizeof(read_buffer),
                           &bytes_read) == FIRMWARE_STATUS_IO_ERROR);
    assert(source->close(source->context) == FIRMWARE_STATUS_OK);
    assert(source->close(source->context) == FIRMWARE_STATUS_INVALID_STATE);
    assert(source->unmount(source->context) == FIRMWARE_STATUS_OK);
    assert(source->unmount(source->context) == FIRMWARE_STATUS_INVALID_STATE);
}

int main(void)
{
    TestFixedFileMappingAndRead();
    TestErrorsAndLifecycle();
    return 0;
}
