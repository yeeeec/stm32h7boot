#ifndef FIRMWARE_CURRENT_MANAGER_H
#define FIRMWARE_CURRENT_MANAGER_H

#include <stddef.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int (*exists)(void *context, const char *path);
        firmware_status_t (*mkdir)(void *context, const char *path);
        firmware_status_t (*copy_file)(void *context, const char *source, const char *destination);
        firmware_status_t (*verify_file)(void *context, const char *path);
        firmware_status_t (*remove_tree)(void *context, const char *path);
        firmware_status_t (*rename)(void *context, const char *old_path, const char *new_path);
        firmware_status_t (*sync)(void *context);
        void *context;
    } current_manager_port_t;

    /* Build CURRENT_NEW completely, verify it, then commit it.  CURRENT is not
     * touched when any pre-commit operation fails. */
    firmware_status_t CurrentManager_BuildAndCommit(const current_manager_port_t *port,
                                                    const char *source_root,
                                                    const char *current_root, const char *new_root,
                                                    const char *const *files, size_t file_count);

    /* Remove an interrupted/incomplete CURRENT_NEW tree. */
    firmware_status_t CurrentManager_Cleanup(const current_manager_port_t *port,
                                             const char *new_root);

#ifdef __cplusplus
}
#endif

#endif
