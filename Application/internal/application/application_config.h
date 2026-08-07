/**
 * @file application_config.h
 * @brief Composition-facing Application dependency contract.
 */
#ifndef APPLICATION_CONFIG_H
#define APPLICATION_CONFIG_H

#include "application/application.h"
#include "firmware/package_source.h"
#include "firmware/system_reset.h"
#include "firmware/update_request_store.h"
#include "services/common/boot_types.h"

struct active_validation_service;
struct boot_control_service;
struct launch_service;
struct update_service;
struct update_request_service;

typedef struct
{
    struct boot_control_service *boot_control;
    struct update_service *update;
    struct active_validation_service *validation;
    struct launch_service *launch;
    /* Formal fixed-file source, injected for the Phase 6 flow. */
    const package_source_t *package_source;
    /* Formal raw request storage, injected for the Phase 6 flow. */
    const update_request_store_t *update_request_store;
    /* Formal strict parser for the trusted request document. */
    struct update_request_service *update_request_service;
    const system_reset_t *system_reset;
    release_version_t bootloader_version;
} application_dependencies_t;

firmware_status_t Application_Configure(
    const application_dependencies_t *dependencies);

#endif
