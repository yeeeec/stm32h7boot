/**
 * @file update_service_api.h
 * @brief Incremental package preparation and installation capability API.
 */
#ifndef SERVICES_UPDATE_SERVICE_API_H
#define SERVICES_UPDATE_SERVICE_API_H

#include "services/common/manifest_types.h"
#include "services/common/service_result.h"
#include "services/common/update_request_types.h"

struct update_service;

/** Read, parse, and bind the fixed Manifest from an already-mounted source. */
firmware_status_t UpdateService_PrepareStart(
    struct update_service *service,
    const update_request_t *request);

/** Install the prepared package into the fixed APP and GUI runtime regions. */
firmware_status_t UpdateService_InstallStart(struct update_service *service);

/** Advance at most one bounded update operation or state transition. */
void UpdateService_Process(struct update_service *service);

/** Cancel before target-slot erase begins. */
firmware_status_t UpdateService_Cancel(struct update_service *service);

/** Return the current update lifecycle state. */
service_run_state_t UpdateService_GetState(
    const struct update_service *service);

/** Return the result retained by the most recent update attempt. */
const service_result_t *UpdateService_GetResult(
    const struct update_service *service);

/** Return the prepared Manifest, or NULL before Prepare succeeds. */
const validated_manifest_t *UpdateService_GetManifest(
    const struct update_service *service);

/** Return the uncommitted candidate Active Record after Install succeeds. */
const boot_active_record_t *UpdateService_GetCandidate(
    const struct update_service *service);

/**
 * Return whether this install attempt may already have modified Runtime.
 * The value becomes true when the first APP erase starts and remains sticky
 * until the service is re-prepared.
 */
int UpdateService_RuntimeMayBeModified(
    const struct update_service *service);

#endif
