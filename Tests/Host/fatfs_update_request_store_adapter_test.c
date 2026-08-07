#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "adapters/fatfs_package_source_adapter.h"
#include "adapters/fatfs_update_request_store_adapter.h"
#include "fatfs_test_support.h"

static const uint8_t malformed_request[] = "{ broken json";

static void ResetAdapters(fatfs_release_volume_context_t *volume,
                          fatfs_package_source_adapter_t *package_adapter,
                          fatfs_update_request_store_adapter_t *request_adapter)
{
    FatFsTest_Reset();
    assert(FatFsReleaseVolumeContext_Init(volume) == FIRMWARE_STATUS_OK);
    assert(FatFsPackageSourceAdapter_Init(package_adapter, volume) == FIRMWARE_STATUS_OK);
    assert(FatFsUpdateRequestStoreAdapter_Init(request_adapter, volume) == FIRMWARE_STATUS_OK);
}

static void TestRawLoad(void)
{
    fatfs_release_volume_context_t volume;
    fatfs_package_source_adapter_t package_adapter;
    fatfs_update_request_store_adapter_t request_adapter;
    const package_source_t *package_source;
    const update_request_store_t *request_store;
    uint8_t buffer[UPDATE_REQUEST_STORE_MAX_RAW_SIZE];
    uint32_t size;

    ResetAdapters(&volume, &package_adapter, &request_adapter);
    package_source = FatFsPackageSourceAdapter_Interface(&package_adapter);
    request_store = FatFsUpdateRequestStoreAdapter_Interface(&request_adapter);
    assert(request_store->load_raw(request_store->context, buffer, sizeof(buffer), &size) ==
           FIRMWARE_STATUS_INVALID_STATE);
    assert(package_source->mount(package_source->context) == FIRMWARE_STATUS_OK);

    fatfs_test_state.file_data = malformed_request;
    fatfs_test_state.file_size = sizeof(malformed_request) - 1U;
    assert(request_store->load_raw(request_store->context, buffer, sizeof(buffer), &size) ==
           FIRMWARE_STATUS_OK);
    assert(size == sizeof(malformed_request) - 1U);
    assert(memcmp(buffer, malformed_request, size) == 0);
    assert(strcmp(fatfs_test_state.last_path, "0:boot_update_request.json") == 0);

    fatfs_test_state.file_size = 0U;
    assert(request_store->load_raw(request_store->context, buffer, sizeof(buffer), &size) ==
           FIRMWARE_STATUS_OK);
    assert(size == 0U);

    fatfs_test_state.open_result = FR_NO_FILE;
    assert(request_store->load_raw(request_store->context, buffer, sizeof(buffer), &size) ==
           FIRMWARE_STATUS_NOT_FOUND);
    fatfs_test_state.open_result = FR_OK;
    fatfs_test_state.file_size = UPDATE_REQUEST_STORE_MAX_RAW_SIZE + 1U;
    assert(request_store->load_raw(request_store->context, buffer, sizeof(buffer), &size) ==
           FIRMWARE_STATUS_BUFFER_TOO_SMALL);
    fatfs_test_state.file_size = 10U;
    assert(request_store->load_raw(request_store->context, buffer, 9U, &size) ==
           FIRMWARE_STATUS_BUFFER_TOO_SMALL);
    fatfs_test_state.read_result = FR_DISK_ERR;
    assert(request_store->load_raw(request_store->context, buffer, sizeof(buffer), &size) ==
           FIRMWARE_STATUS_IO_ERROR);
    fatfs_test_state.read_result = FR_OK;
    fatfs_test_state.use_read_limit = 1;
    fatfs_test_state.read_limit = 1U;
    assert(request_store->load_raw(request_store->context, buffer, sizeof(buffer), &size) ==
           FIRMWARE_STATUS_IO_ERROR);
}

static void TestClearAndPackageExclusion(void)
{
    fatfs_release_volume_context_t volume;
    fatfs_package_source_adapter_t package_adapter;
    fatfs_update_request_store_adapter_t request_adapter;
    const package_source_t *package_source;
    const update_request_store_t *request_store;

    ResetAdapters(&volume, &package_adapter, &request_adapter);
    package_source = FatFsPackageSourceAdapter_Interface(&package_adapter);
    request_store = FatFsUpdateRequestStoreAdapter_Interface(&request_adapter);
    assert(package_source->mount(package_source->context) == FIRMWARE_STATUS_OK);
    assert(request_store->clear(request_store->context) == FIRMWARE_STATUS_OK);
    assert(strcmp(fatfs_test_state.last_path, "0:boot_update_request.json") == 0);
    fatfs_test_state.unlink_result = FR_NO_FILE;
    assert(request_store->clear(request_store->context) == FIRMWARE_STATUS_NOT_FOUND);
    fatfs_test_state.unlink_result = FR_DISK_ERR;
    assert(request_store->clear(request_store->context) == FIRMWARE_STATUS_IO_ERROR);
    fatfs_test_state.unlink_result = FR_OK;
    assert(package_source->open(package_source->context, PACKAGE_FILE_APP) == FIRMWARE_STATUS_OK);
    assert(request_store->clear(request_store->context) == FIRMWARE_STATUS_INVALID_STATE);
    assert(package_source->close(package_source->context) == FIRMWARE_STATUS_OK);
    assert(package_source->unmount(package_source->context) == FIRMWARE_STATUS_OK);
}

int main(void)
{
    TestRawLoad();
    TestClearAndPackageExclusion();
    return 0;
}
