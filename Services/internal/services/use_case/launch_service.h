/**
 * @file launch_service.h
 * @brief Composition-visible Launch Service object and dependencies.
 */
#ifndef SERVICES_LAUNCH_SERVICE_INTERNAL_H
#define SERVICES_LAUNCH_SERVICE_INTERNAL_H

#include "firmware/application_jump.h"
#include "firmware/async_block_device.h"
#include "firmware/xip_controller.h"
#include "services/capability/vector_validation.h"
#include "services/use_case/launch_service_api.h"

/** Dependencies and SRAM policy required for final application launch. */
typedef struct
{
    const async_block_device_t *storage;
    const xip_controller_t *xip_controller;
    const application_jump_t *application_jump;
    const memory_region_t *sram_regions;
    uint32_t sram_region_count;
} launch_service_dependencies_t;

/** Synchronous Launch Service state. */
typedef struct launch_service
{
    const async_block_device_t *storage;
    const xip_controller_t *xip_controller;
    const application_jump_t *application_jump;
    const memory_region_t *sram_regions;
    uint32_t sram_region_count;
    service_result_t result;
    int initialized;
} launch_service_t;

/** Initialize a Launch Service from caller-owned dependencies. */
firmware_status_t LaunchService_Init(
    launch_service_t *service,
    const launch_service_dependencies_t *dependencies);

#endif
