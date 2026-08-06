/**
 * @file recovery_service.h
 * @brief Composition-visible fallback pair recovery state.
 */
#ifndef SERVICES_RECOVERY_SERVICE_INTERNAL_H
#define SERVICES_RECOVERY_SERVICE_INTERNAL_H

#include <stdint.h>

#include "services/common/boot_control_types.h"
#include "services/use_case/active_validation_service.h"
#include "services/use_case/recovery_service_api.h"

/**
 * Load one structurally valid retained Active Record for a fixed pair. The
 * loader keeps EEPROM addressing and the concrete store outside Recovery.
 */
typedef firmware_status_t (*recovery_candidate_load_fn)(
    void *context,
    boot_pair_t pair,
    boot_active_record_t *candidate);

typedef struct
{
    recovery_candidate_load_fn load_candidate;
    void *candidate_context;
    active_validation_service_t *validation;
} recovery_service_dependencies_t;

typedef enum
{
    RECOVERY_STAGE_IDLE = 0,
    RECOVERY_STAGE_LOAD_PAIR_1,
    RECOVERY_STAGE_LOAD_PAIR_2,
    RECOVERY_STAGE_START_VALIDATE_PAIR_1,
    RECOVERY_STAGE_VALIDATE_PAIR_1,
    RECOVERY_STAGE_START_VALIDATE_PAIR_2,
    RECOVERY_STAGE_VALIDATE_PAIR_2,
    RECOVERY_STAGE_SELECT_PAIR
} recovery_stage_t;

typedef struct recovery_service
{
    recovery_candidate_load_fn load_candidate;
    void *candidate_context;
    active_validation_service_t *validation;
    boot_active_record_t candidates[2];
    boot_active_record_t selected_record;
    boot_pair_t preferred_pair;
    service_run_state_t state;
    service_result_t result;
    recovery_stage_t stage;
    uint8_t candidate_present[2];
    uint8_t candidate_valid[2];
    int initialized;
} recovery_service_t;

firmware_status_t RecoveryService_Init(
    recovery_service_t *service,
    const recovery_service_dependencies_t *dependencies);

#endif
