/**
 * @file recovery_service_api.h
 * @brief Incremental fallback-slot validation and Active Record rebuild API.
 */
#ifndef SERVICES_RECOVERY_SERVICE_API_H
#define SERVICES_RECOVERY_SERVICE_API_H

#include "services/common/boot_types.h"
#include "services/common/service_result.h"

struct recovery_service;

/**
 * Start scanning both fixed APP/GUI pairs and rebuilding Active Record.
 * A valid preferred pair wins over serial ordering; BOOT_PAIR_NONE selects
 * the newest valid candidate by RFC 1982 sequence arithmetic.
 */
firmware_status_t RecoveryService_Start(
    struct recovery_service *service,
    boot_pair_t preferred_pair);

/** Advance one candidate load, validation step, or EEPROM commit step. */
void RecoveryService_Process(struct recovery_service *service);

/** Return the current recovery lifecycle state. */
service_run_state_t RecoveryService_GetState(
    const struct recovery_service *service);

/** Return the result retained by the most recent recovery attempt. */
const service_result_t *RecoveryService_GetResult(
    const struct recovery_service *service);

#endif
