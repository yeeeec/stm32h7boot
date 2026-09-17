#include "application/boot_manager.h"

#include "firmware/boot_config.h"
#include "update/update_request.h"

static boot_manager_result_t fatal(const boot_manager_io_t *io, BootError_t error)
{
    if (io->fatal != NULL)
        io->fatal(io->context, error);
    return BOOT_MANAGER_FATAL;
}

static boot_manager_result_t launch_existing(const boot_manager_io_t *io, BootError_t error)
{
    firmware_status_t status = io->storage_unmount(io->context);
    if (FirmwareStatus_IsError(status))
        return fatal(io, BOOT_ERR_STORAGE);
    status = io->launch_app(io->context);
    return FirmwareStatus_IsOk(status) ? BOOT_MANAGER_FAST_BOOT : fatal(io, error);
}

boot_manager_result_t BootManager_Run(const boot_manager_io_t *io)
{
    BootControl_t control;
    storage_boot_update_request_t request;
    firmware_status_t status;
    int runtime_modified = 0;

    if (io == NULL || io->read_boot_control == NULL || io->clear_boot_control == NULL ||
        io->launch_app == NULL || io->storage_init_mount == NULL || io->storage_unmount == NULL ||
        io->load_request == NULL || io->perform_update == NULL)
        return BOOT_MANAGER_FATAL;

#if BOOT_FAST_UPDATE_CHECK_ENABLE
    status = io->read_boot_control(io->context, &control);
    if (FirmwareStatus_IsError(status) || control.magic != BOOT_MAGIC ||
        control.format_version != 1U ||
        (control.request != BOOT_REQUEST_NONE && control.request != BOOT_REQUEST_UPDATE))
    {
        /* Invalid metadata is never allowed to trigger a slow SD path. */
        (void) io->clear_boot_control(io->context);
        status = io->launch_app(io->context);
        return FirmwareStatus_IsOk(status) ? BOOT_MANAGER_FAST_BOOT : fatal(io, BOOT_ERR_LAUNCH);
    }
    if (control.request == BOOT_REQUEST_NONE)
    {
        status = io->launch_app(io->context);
        return FirmwareStatus_IsOk(status) ? BOOT_MANAGER_FAST_BOOT : fatal(io, BOOT_ERR_LAUNCH);
    }
#endif

    status = io->storage_init_mount(io->context);
    if (FirmwareStatus_IsError(status))
        return fatal(io, BOOT_ERR_STORAGE);
    status = io->load_request(io->context, &request);
    if (status == FIRMWARE_STATUS_NOT_FOUND)
    {
        (void) io->clear_boot_control(io->context);
        return launch_existing(io, BOOT_ERR_LAUNCH);
    }
    if (FirmwareStatus_IsError(status) || FirmwareStatus_IsError(UpdateRequest_Validate(&request)))
    {
        (void) io->clear_boot_control(io->context);
        return launch_existing(io, BOOT_ERR_REQUEST);
    }

    status = io->perform_update(io->context, &request, &runtime_modified);
    if (FirmwareStatus_IsOk(status))
    {
        (void) io->clear_boot_control(io->context);
        return BOOT_MANAGER_UPDATED;
    }
    if (runtime_modified != 0 && io->perform_recovery != NULL &&
        FirmwareStatus_IsOk(io->perform_recovery(io->context)))
    {
        (void) io->clear_boot_control(io->context);
        return BOOT_MANAGER_RECOVERED;
    }
    if (runtime_modified == 0)
    {
        /* Pre-install failure: the existing Runtime is untouched and may be
         * started after cancelling this request. */
        (void) io->clear_boot_control(io->context);
        return launch_existing(io, BOOT_ERR_LAUNCH);
    }
    /* Runtime was modified (or no recovery source exists): do not jump into a
     * potentially corrupt image.  Request and EEPROM remain pending. */
    return fatal(io, runtime_modified != 0 ? BOOT_ERR_RECOVERY : BOOT_ERR_WRITE);
}
