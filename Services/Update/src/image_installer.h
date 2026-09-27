#ifndef IMAGE_INSTALLER_H
#define IMAGE_INSTALLER_H

#include "firmware/status.h"
#include "update_model.h"

firmware_status_t ImageInstaller_Install(const char *root,
                                         const update_manifest_component_t *component);

#endif /* IMAGE_INSTALLER_H */
