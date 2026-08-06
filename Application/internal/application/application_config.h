/**
 * @file application_config.h
 * @brief Composition-facing Application dependency contract.
 */
#ifndef APPLICATION_CONFIG_H
#define APPLICATION_CONFIG_H

#include "application/application.h"
#include "firmware/package_source.h"
#include "firmware/system_reset.h"
#include "services/common/boot_types.h"

struct active_validation_service;
struct boot_control_service;
struct launch_service;
struct recovery_service;
struct update_service;

typedef struct
{
    struct boot_control_service *boot_control;
    struct update_service *update;
    struct recovery_service *recovery;
    struct active_validation_service *validation;
    struct launch_service *launch;
    const package_source_t *package_source;
    const system_reset_t *system_reset;
    release_version_t bootloader_version;
    const char *request_path;
} application_dependencies_t;

firmware_status_t Application_Configure(
    const application_dependencies_t *dependencies);

#endif
