/**
 * @file active_validation_service.h
 * @brief Composition-visible Active Validation object and dependencies.
 */
#ifndef SERVICES_ACTIVE_VALIDATION_SERVICE_INTERNAL_H
#define SERVICES_ACTIVE_VALIDATION_SERVICE_INTERNAL_H

#include "firmware/async_block_device.h"
#include "firmware/hash.h"
#include "services/common/runtime_layout.h"
#include "services/capability/vector_validation.h"
#include "services/use_case/active_validation_service_api.h"

/** Caller-owned dependencies and work buffer for fixed-runtime validation. */
typedef struct
{
    const async_block_device_t *storage;
    const hash_provider_t *hash;
    uint8_t *buffer;
    uint32_t buffer_size;
    const memory_region_t *sram_regions;
    uint32_t sram_region_count;
} active_validation_service_dependencies_t;

typedef enum
{
    ACTIVE_VALIDATION_STAGE_IDLE = 0,
    ACTIVE_VALIDATION_STAGE_RESET_APP_HASH,
    ACTIVE_VALIDATION_STAGE_READ_APP,
    ACTIVE_VALIDATION_STAGE_FINISH_APP_HASH,
    ACTIVE_VALIDATION_STAGE_RESET_GUI_HASH,
    ACTIVE_VALIDATION_STAGE_READ_GUI,
    ACTIVE_VALIDATION_STAGE_FINISH_GUI_HASH
} active_validation_stage_t;

/** State retained while APP and GUI are read incrementally. */
typedef struct active_validation_service
{
    const async_block_device_t *storage;
    const hash_provider_t *hash;
    uint8_t *buffer;
    uint32_t buffer_size;
    const memory_region_t *sram_regions;
    uint32_t sram_region_count;
    boot_active_record_t active_record;
    const boot_runtime_layout_t *layout;
    service_run_state_t state;
    service_result_t result;
    active_validation_stage_t stage;
    uint32_t offset;
    int initialized;
} active_validation_service_t;

/** Initialize an Active Validation Service. */
firmware_status_t ActiveValidationService_Init(
    active_validation_service_t *service,
    const active_validation_service_dependencies_t *dependencies);

#endif
