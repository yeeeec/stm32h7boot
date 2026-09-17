#include "application/application.h"

#include <stdio.h>
#include <string.h>


#include "application/boot_manager.h"
#include "crypto/sha256.h"
#include "firmware/boot_config.h"
#include "platform/platform.h"
#include "platform/platform_application.h"
#include "platform/platform_boot_control.h"
#include "platform/platform_flash.h"
#include "platform/platform_storage.h"
#include "platform/platform_system.h"
#include "platform/platform_therapy.h"
#include "update/image_installer.h"
#include "update/current_manager.h"
#include "update/update_manager.h"
#include "update/update_manifest.h"
#include "update/update_request.h"

#define APPLICATION_FILE_BUFFER_SIZE UPDATE_MANIFEST_MAX_SIZE

static uint8_t s_request_buffer[APPLICATION_FILE_BUFFER_SIZE];
static uint8_t s_manifest_buffer[APPLICATION_FILE_BUFFER_SIZE];
static uint8_t s_initialized;
static BootControl_t s_boot_control;

static firmware_status_t InstallManifest(const char *root, const update_manifest_t *manifest,
                                         int *runtime_modified);

static firmware_status_t DigestHex(const uint8_t *data, size_t size,
                                   char output[UPDATE_SHA256_HEX_LENGTH + 1U])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t digest[CRYPTO_SHA256_DIGEST_SIZE];
    size_t i;
    if (Crypto_Sha256(data, size, digest) != 0)
        return FIRMWARE_STATUS_IO_ERROR;
    for (i = 0U; i < sizeof(digest); ++i)
    {
        output[i * 2U]      = hex[digest[i] >> 4U];
        output[i * 2U + 1U] = hex[digest[i] & 0x0FU];
    }
    output[UPDATE_SHA256_HEX_LENGTH] = '\0';
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t ReadFile(const char *path, uint8_t *buffer, size_t capacity,
                                  size_t *length)
{
    platform_file_handle_t handle;
    platform_file_info_t info;
    size_t total = 0U;
    firmware_status_t status;

    if (path == NULL || buffer == NULL || length == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = PlatformStorage_Stat(path, &info);
    if (FirmwareStatus_IsError(status))
        return status;
    if (info.is_directory != 0U || info.size > capacity)
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    status = PlatformStorage_OpenRead(path, &handle);
    if (FirmwareStatus_IsError(status))
        return status;
    while (total < info.size)
    {
        size_t read_size = 0U;
        size_t request =
            (info.size - total > STORAGE_IO_CHUNK_SIZE) ? STORAGE_IO_CHUNK_SIZE : info.size - total;
        status = PlatformStorage_Read(handle, &buffer[total], request, &read_size);
        if (FirmwareStatus_IsError(status) || read_size != request)
        {
            (void) PlatformStorage_Close(handle);
            return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_IO_ERROR;
        }
        total += read_size;
    }
    status  = PlatformStorage_Close(handle);
    *length = total;
    return status;
}

static firmware_status_t ReadSource(void *context, const char *path, uint32_t *handle,
                                    uint32_t *size)
{
    platform_file_handle_t file;
    platform_file_info_t info;
    firmware_status_t status;
    (void) context;
    if (path == NULL || handle == NULL || size == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = PlatformStorage_Stat(path, &info);
    if (FirmwareStatus_IsError(status) || info.is_directory != 0U)
        return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_INVALID_STATE;
    status = PlatformStorage_OpenRead(path, &file);
    if (FirmwareStatus_IsOk(status))
    {
        *handle = file;
        *size   = info.size;
    }
    return status;
}

static firmware_status_t ReadSourceBlock(void *context, uint32_t handle, uint32_t offset,
                                         void *buffer, size_t size, size_t *read_size)
{
    firmware_status_t status;
    (void) context;
    status = PlatformStorage_Seek(handle, offset);
    if (FirmwareStatus_IsError(status))
        return status;
    return PlatformStorage_Read(handle, buffer, size, read_size);
}

static firmware_status_t CloseSource(void *context, uint32_t handle)
{
    (void) context;
    return PlatformStorage_Close(handle);
}

static int StorageExists(void *context, const char *path)
{
    platform_file_info_t info;
    (void) context;
    return PlatformStorage_Stat(path, &info) == FIRMWARE_STATUS_OK;
}

static firmware_status_t CopyFile(void *context, const char *source, const char *destination)
{
    static uint8_t buffer[STORAGE_IO_CHUNK_SIZE];
    platform_file_handle_t input  = 0U;
    platform_file_handle_t output = 0U;
    platform_file_info_t info;
    firmware_status_t status;
    uint32_t offset = 0U;
    (void) context;
    status = PlatformStorage_Stat(source, &info);
    if (FirmwareStatus_IsError(status) || info.is_directory != 0U)
        return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_INVALID_STATE;
    status = PlatformStorage_OpenRead(source, &input);
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformStorage_OpenWrite(destination, &output);
    if (FirmwareStatus_IsError(status))
    {
        (void) PlatformStorage_Close(input);
        return status;
    }
    while (offset < info.size)
    {
        size_t request =
            (info.size - offset > sizeof(buffer)) ? sizeof(buffer) : info.size - offset;
        size_t actual  = 0U;
        size_t written = 0U;
        status         = PlatformStorage_Read(input, buffer, request, &actual);
        if (FirmwareStatus_IsError(status) || actual != request)
            break;
        status = PlatformStorage_Write(output, buffer, actual, &written);
        if (FirmwareStatus_IsError(status) || written != actual)
            break;
        offset += (uint32_t) actual;
    }
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Sync(output);
    (void) PlatformStorage_Close(input);
    if (PlatformStorage_Close(output) != FIRMWARE_STATUS_OK && FirmwareStatus_IsOk(status))
        status = FIRMWARE_STATUS_IO_ERROR;
    return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_OK;
}

static firmware_status_t VerifyFile(void *context, const char *path)
{
    platform_file_info_t info;
    platform_file_handle_t handle;
    uint8_t buffer[STORAGE_IO_CHUNK_SIZE];
    uint32_t total = 0U;
    firmware_status_t status;
    (void) context;
    status = PlatformStorage_Stat(path, &info);
    if (FirmwareStatus_IsError(status) || info.is_directory != 0U)
        return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_INVALID_STATE;
    status = PlatformStorage_OpenRead(path, &handle);
    if (FirmwareStatus_IsError(status))
        return status;
    while (total < info.size)
    {
        size_t request = (info.size - total > sizeof(buffer)) ? sizeof(buffer) : info.size - total;
        size_t actual  = 0U;
        status         = PlatformStorage_Read(handle, buffer, request, &actual);
        if (FirmwareStatus_IsError(status) || actual != request)
            break;
        total += (uint32_t) actual;
    }
    (void) PlatformStorage_Close(handle);
    return FirmwareStatus_IsError(status) ? status : FIRMWARE_STATUS_OK;
}

static firmware_status_t EnsureDirectory(const char *path)
{
    platform_file_info_t info;
    firmware_status_t status = PlatformStorage_Stat(path, &info);
    if (status == FIRMWARE_STATUS_OK)
        return info.is_directory != 0U ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_INVALID_STATE;
    if (status != FIRMWARE_STATUS_NOT_FOUND)
        return status;
    return PlatformStorage_Mkdir(path);
}

static firmware_status_t ManagerMkdir(void *context, const char *path)
{
    (void) context;
    return PlatformStorage_Mkdir(path);
}

static firmware_status_t ManagerRemoveTree(void *context, const char *path)
{
    (void) context;
    return PlatformStorage_RemoveTree(path);
}

static firmware_status_t ManagerRename(void *context, const char *old_path, const char *new_path)
{
    (void) context;
    return PlatformStorage_Rename(old_path, new_path);
}

static firmware_status_t CommitPackage(void *context, const update_manifest_t *manifest)
{
    static const current_manager_port_t port = {.exists      = StorageExists,
                                                .mkdir       = ManagerMkdir,
                                                .copy_file   = CopyFile,
                                                .verify_file = VerifyFile,
                                                .remove_tree = ManagerRemoveTree,
                                                .rename      = ManagerRename,
                                                .sync        = NULL,
                                                .context     = NULL};
    const char *files[UPDATE_MANIFEST_MAX_COMPONENTS + 1U];
    char file_names[UPDATE_MANIFEST_MAX_COMPONENTS][UPDATE_COMPONENT_FILE_MAX];
    size_t i;
    firmware_status_t status;
    (void) context;
    if (manifest == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    status = EnsureDirectory(BOOT_CURRENT_ROOT);
    if (FirmwareStatus_IsError(status))
        return status;
    status = EnsureDirectory(BOOT_CURRENT_NEW_ROOT);
    if (FirmwareStatus_IsError(status))
        return status;
    status = EnsureDirectory(BOOT_CURRENT_NEW_ROOT);
    if (FirmwareStatus_IsError(status))
        return status;
    files[0] = BOOT_MANIFEST_FILE;
    for (i = 0U; i < manifest->component_count; ++i)
    {
        (void) strncpy(file_names[i], manifest->components[i].file, sizeof(file_names[i]) - 1U);
        file_names[i][sizeof(file_names[i]) - 1U] = '\0';
        files[i + 1U]                             = file_names[i];
    }
    return CurrentManager_BuildAndCommit(&port, BOOT_UPDATE_FIRMWARE, BOOT_CURRENT_FIRMWARE,
                                         BOOT_CURRENT_NEW_FIRMWARE, files,
                                         manifest->component_count + 1U);
}

static firmware_status_t InstallPackage(void *context, const update_manifest_t *manifest,
                                        int *runtime_modified)
{
    (void) context;
    return InstallManifest(BOOT_UPDATE_FIRMWARE, manifest, runtime_modified);
}

static firmware_status_t LoadCurrentVersion(update_version_t *version)
{
    update_manifest_t manifest;
    size_t length;
    firmware_status_t status;
    status = ReadFile(BOOT_CURRENT_FIRMWARE "/" BOOT_MANIFEST_FILE, s_manifest_buffer,
                      sizeof(s_manifest_buffer), &length);
    if (FirmwareStatus_IsError(status))
        return status;
    status = UpdateManifest_Parse(s_manifest_buffer, length, &manifest);
    if (FirmwareStatus_IsError(status))
        return status;
    *version = manifest.release;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t PerformUpdate(void *context, const storage_boot_update_request_t *request,
                                       int *runtime_modified)
{
    update_manifest_t manifest;
    update_version_t current_version;
    update_manager_port_t port = {
        .install = InstallPackage, .commit = CommitPackage, .context = NULL};
    char manifest_hash[UPDATE_SHA256_HEX_LENGTH + 1U];
    size_t length;
    int current_exists = 0;
    firmware_status_t status;
    size_t i;
    (void) context;
    *runtime_modified = 0;
    status            = ReadFile(BOOT_UPDATE_FIRMWARE "/" BOOT_MANIFEST_FILE, s_manifest_buffer,
                                 sizeof(s_manifest_buffer), &length);
    if (FirmwareStatus_IsError(status))
        return status;
    status = UpdateManifest_Parse(s_manifest_buffer, length, &manifest);
    if (FirmwareStatus_IsError(status))
        return status;
    status = DigestHex(s_manifest_buffer, length, manifest_hash);
    if (FirmwareStatus_IsError(status) || strcmp(manifest_hash, request->manifest_sha256) != 0)
        return FIRMWARE_STATUS_AUTHENTICATION_FAILED;
#if BOOT_FAST_UPDATE_CHECK_ENABLE
    for (i = 0U; i < sizeof(s_boot_control.manifest_sha256); ++i)
    {
        unsigned value;
        if (sscanf(&manifest_hash[i * 2U], "%2x", &value) != 1 ||
            s_boot_control.manifest_sha256[i] != (uint8_t) value)
            return FIRMWARE_STATUS_AUTHENTICATION_FAILED;
    }
#else
    (void) i;
#endif
    status = LoadCurrentVersion(&current_version);
    if (FirmwareStatus_IsOk(status))
        current_exists = 1;
    else if (status != FIRMWARE_STATUS_NOT_FOUND)
        return status;
    return UpdateManager_Execute(request, &manifest, manifest_hash, &current_version,
                                 current_exists, &port, runtime_modified);
}

static firmware_status_t EraseFlash(void *context, uint32_t address, uint32_t size)
{
    (void) context;
    return PlatformFlash_Erase(address, size);
}

static firmware_status_t WriteFlash(void *context, uint32_t address, const void *data, size_t size)
{
    (void) context;
    return PlatformFlash_Write(address, data, (uint32_t) size);
}

static firmware_status_t ReadFlash(void *context, uint32_t address, void *data, size_t size)
{
    (void) context;
    return PlatformFlash_Read(address, data, (uint32_t) size);
}

static firmware_status_t TherapyErase(void *context, uint32_t address, uint32_t size)
{
    (void) context;
    return PlatformTherapy_Erase(address, size);
}

static firmware_status_t TherapyWrite(void *context, uint32_t address, const void *data,
                                      size_t size)
{
    (void) context;
    return PlatformTherapy_Write(address, data, (uint32_t) size);
}

static firmware_status_t TherapyRead(void *context, uint32_t address, void *data, size_t size)
{
    (void) context;
    return PlatformTherapy_Read(address, data, (uint32_t) size);
}

static firmware_status_t InstallComponent(const char *root,
                                          const update_manifest_component_t *component,
                                          int *runtime_modified)
{
    image_installer_port_t port = {.source_open  = ReadSource,
                                   .source_read  = ReadSourceBlock,
                                   .source_close = CloseSource,
                                   .target_erase = EraseFlash,
                                   .target_write = WriteFlash,
                                   .target_read  = ReadFlash,
                                   .context      = NULL};
    ImageInstallPlan_t plan;
    ImageInstallResult_t result;
    static char source_path[160];
    size_t i;

    if (root == NULL || component == NULL || runtime_modified == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    (void) memset(&plan, 0, sizeof(plan));
    if (component->mask == 1U)
    {
        plan.target_address  = BOOT_APP_TARGET_OFFSET;
        plan.target_max_size = BOOT_APP_MAX_SIZE;
    }
    else if (component->mask == 2U)
    {
        plan.target_address  = BOOT_GUI_TARGET_OFFSET;
        plan.target_max_size = BOOT_GUI_MAX_SIZE;
    }
    else if (component->mask == 4U)
    {
        plan.target_address  = BOOT_THERAPY_TARGET_ADDRESS;
        plan.target_max_size = BOOT_THERAPY_MAX_SIZE;
        port.target_erase    = TherapyErase;
        port.target_write    = TherapyWrite;
        port.target_read     = TherapyRead;
    }
    else
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (component->mask != 4U)
    {
        firmware_status_t status = PlatformFlash_Init();
        if (FirmwareStatus_IsError(status))
            return status;
        status = PlatformFlash_ExitMemoryMapped();
        if (FirmwareStatus_IsError(status))
            return status;
    }
    if (snprintf(source_path, sizeof(source_path), "%s/%s", root, component->file) <= 0)
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    plan.source_path   = source_path;
    plan.expected_size = component->size;
    for (i = 0U; i < sizeof(plan.expected_sha256); ++i)
    {
        unsigned value;
        if (sscanf(&component->sha256[i * 2U], "%2x", &value) != 1)
            return FIRMWARE_STATUS_INVALID_ARGUMENT;
        plan.expected_sha256[i] = (uint8_t) value;
    }
    *runtime_modified = 1;
    result            = ImageInstaller_InstallWithPort(&plan, &port);
    if (result == IMAGE_INSTALL_SOURCE || result == IMAGE_INSTALL_SIZE ||
        result == IMAGE_INSTALL_INVALID_ARGUMENT)
        *runtime_modified = 0;
    return result == IMAGE_INSTALL_OK ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_IO_ERROR;
}

static firmware_status_t ReadBootControl(void *context, BootControl_t *control)
{
    firmware_status_t status;
    (void) context;
    status = PlatformBootControl_Read(control);
    if (FirmwareStatus_IsOk(status))
        s_boot_control = *control;
    return status;
}

static firmware_status_t ClearBootControl(void *context)
{
    (void) context;
    return PlatformBootControl_Clear();
}

static firmware_status_t StorageInitMount(void *context)
{
    firmware_status_t status;
    (void) context;
    status = PlatformStorage_Init();
    if (FirmwareStatus_IsOk(status))
        status = PlatformStorage_Mount();
    return status;
}

static firmware_status_t StorageUnmount(void *context)
{
    (void) context;
    return PlatformStorage_Unmount();
}

static firmware_status_t LoadRequest(void *context, storage_boot_update_request_t *request)
{
    size_t length;
    firmware_status_t status;
    (void) context;
    status = ReadFile(BOOT_REQUEST_PATH, s_request_buffer, sizeof(s_request_buffer), &length);
    if (FirmwareStatus_IsError(status))
        return status;
    return UpdateRequest_Parse(s_request_buffer, length, request);
}

static firmware_status_t InstallManifest(const char *root, const update_manifest_t *manifest,
                                         int *runtime_modified)
{
    size_t i;
    firmware_status_t status;
    int therapy_started = 0;
    for (i = 0U; i < manifest->component_count; ++i)
    {
        if (manifest->components[i].mask == 4U && therapy_started == 0)
        {
            status = PlatformTherapy_Init();
            if (FirmwareStatus_IsOk(status))
                status = PlatformTherapy_BeginUpdate(NULL);
            if (FirmwareStatus_IsError(status))
                return status;
            therapy_started = 1;
        }
        status = InstallComponent(root, &manifest->components[i], runtime_modified);
        if (FirmwareStatus_IsError(status))
            break;
    }
    if (therapy_started != 0)
    {
        firmware_status_t end_status = PlatformTherapy_EndUpdate();
        if (FirmwareStatus_IsOk(status))
            status = end_status;
    }
    return status;
}

static firmware_status_t Recover(void *context)
{
    update_manifest_t manifest;
    size_t length;
    firmware_status_t status;
    (void) context;
    status = ReadFile(BOOT_CURRENT_FIRMWARE "/" BOOT_MANIFEST_FILE, s_manifest_buffer,
                      sizeof(s_manifest_buffer), &length);
    if (FirmwareStatus_IsError(status))
        return status;
    status = UpdateManifest_Parse(s_manifest_buffer, length, &manifest);
    if (FirmwareStatus_IsError(status))
        return status;
    {
        int runtime_modified = 0;
        return InstallManifest(BOOT_CURRENT_FIRMWARE, &manifest, &runtime_modified);
    }
}

static firmware_status_t Launch(void *context)
{
    firmware_status_t status;
    (void) context;
    status = PlatformFlash_Init();
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformApplication_Validate(PLATFORM_FLASH_MAPPED_BASE + BOOT_APP_TARGET_OFFSET);
    if (FirmwareStatus_IsError(status))
        return status;
    PlatformApplication_Jump(PLATFORM_FLASH_MAPPED_BASE + BOOT_APP_TARGET_OFFSET);
    return FIRMWARE_STATUS_OK;
}

static void Fatal(void *context, BootError_t error)
{
    (void) context;
    (void) error;
    PlatformSystem_Reset();
}

firmware_status_t Application_Init(void)
{
    firmware_status_t status;
    if (s_initialized != 0U)
        return FIRMWARE_STATUS_OK;
    status = Platform_Init();
    if (FirmwareStatus_IsError(status))
        return status;
    status = PlatformSystem_WatchdogInit();
    if (FirmwareStatus_IsError(status))
        return status;
    s_initialized = 1U;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Run(void)
{
    static const boot_manager_io_t io = {.read_boot_control  = ReadBootControl,
                                         .clear_boot_control = ClearBootControl,
                                         .storage_init_mount = StorageInitMount,
                                         .storage_unmount    = StorageUnmount,
                                         .load_request       = LoadRequest,
                                         .perform_update     = PerformUpdate,
                                         .perform_recovery   = Recover,
                                         .launch_app         = Launch,
                                         .fatal              = Fatal,
                                         .context            = NULL};
    if (s_initialized == 0U)
        return FIRMWARE_STATUS_INVALID_STATE;
    {
        boot_manager_result_t result = BootManager_Run(&io);
        if (result == BOOT_MANAGER_UPDATED || result == BOOT_MANAGER_RECOVERED)
        {
            if (StorageUnmount(NULL) != FIRMWARE_STATUS_OK)
                return FIRMWARE_STATUS_IO_ERROR;
            PlatformSystem_Reset();
        }
        if (result == BOOT_MANAGER_FATAL)
            return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}
