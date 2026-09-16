#include "update/current_manager.h"

#include <stdio.h>

static int valid(const current_manager_port_t *p)
{
    return p != NULL && p->exists != NULL && p->mkdir != NULL && p->copy_file != NULL &&
           p->verify_file != NULL && p->remove_tree != NULL && p->rename != NULL;
}

static firmware_status_t path_join(char *out, size_t capacity, const char *root, const char *name)
{
    int n;
    if (out == NULL || root == NULL || name == NULL || root[0] == '\0' || name[0] == '\0')
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    n = snprintf(out, capacity, "%s/%s", root, name);
    return n > 0 && (size_t) n < capacity ? FIRMWARE_STATUS_OK : FIRMWARE_STATUS_BUFFER_TOO_SMALL;
}

firmware_status_t CurrentManager_Cleanup(const current_manager_port_t *port, const char *new_root)
{
    if (!valid(port) || new_root == NULL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    if (!port->exists(port->context, new_root))
        return FIRMWARE_STATUS_OK;
    return port->remove_tree(port->context, new_root);
}

firmware_status_t CurrentManager_BuildAndCommit(const current_manager_port_t *port,
                                                const char *source_root, const char *current_root,
                                                const char *new_root, const char *const *files,
                                                size_t file_count)
{
    size_t i;
    char source[256], destination[256];
    char backup[256];
    int path_length;
    firmware_status_t status;
    if (!valid(port) || source_root == NULL || current_root == NULL || new_root == NULL ||
        files == NULL || file_count == 0U)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    /* A stale transaction can never become a valid CURRENT. */
    status = CurrentManager_Cleanup(port, new_root);
    if (FirmwareStatus_IsError(status))
        return status;
    status = port->mkdir(port->context, new_root);
    if (FirmwareStatus_IsError(status))
        return status;
    for (i = 0U; i < file_count; ++i)
    {
        status = path_join(source, sizeof(source), source_root, files[i]);
        if (FirmwareStatus_IsError(status))
            break;
        status = path_join(destination, sizeof(destination), new_root, files[i]);
        if (FirmwareStatus_IsError(status))
            break;
        status = port->copy_file(port->context, source, destination);
        if (FirmwareStatus_IsError(status))
            break;
        status = port->verify_file(port->context, destination);
        if (FirmwareStatus_IsError(status))
            break;
    }
    if (FirmwareStatus_IsOk(status) && port->sync != NULL)
        status = port->sync(port->context);
    if (FirmwareStatus_IsError(status))
    {
        (void) port->remove_tree(port->context, new_root);
        return status;
    }
    /* Commit is deliberately the last operation.  Existing CURRENT is removed
     * only after the new tree has been completely built and verified. */
    path_length = snprintf(backup, sizeof(backup), "%s.previous", current_root);
    if (path_length <= 0 || (size_t) path_length >= sizeof(backup))
    {
        (void) port->remove_tree(port->context, new_root);
        return FIRMWARE_STATUS_BUFFER_TOO_SMALL;
    }
    if (port->exists(port->context, current_root))
    {
        (void) port->remove_tree(port->context, backup);
        status = port->rename(port->context, current_root, backup);
        if (FirmwareStatus_IsError(status))
        {
            (void) port->remove_tree(port->context, new_root);
            return status;
        }
    }
    status = port->rename(port->context, new_root, current_root);
    if (FirmwareStatus_IsError(status))
    {
        if (port->exists(port->context, backup))
            (void) port->rename(port->context, backup, current_root);
        (void) port->remove_tree(port->context, new_root);
        return status;
    }
    if (port->exists(port->context, backup))
        (void) port->remove_tree(port->context, backup);
    return FIRMWARE_STATUS_OK;
}
