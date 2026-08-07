/**
 * @file recovery_service.c
 * @brief Fallback pair validation and Active Record reconstruction.
 */
#include "services/use_case/recovery_service.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
#include "services/capability/slot_policy.h"

static const char *BootPairName(boot_pair_t pair)
{
    switch (pair)
    {
        case BOOT_PAIR_NONE:
            return "none";
        case BOOT_PAIR_1:
            return "pair-1";
        case BOOT_PAIR_2:
            return "pair-2";
        default:
            return "unknown";
    }
}

static const char *RecoveryStageName(recovery_stage_t stage)
{
    switch (stage)
    {
        case RECOVERY_STAGE_IDLE:
            return "idle";
        case RECOVERY_STAGE_LOAD_PAIR_1:
            return "load-pair-1";
        case RECOVERY_STAGE_LOAD_PAIR_2:
            return "load-pair-2";
        case RECOVERY_STAGE_START_VALIDATE_PAIR_1:
            return "start-validate-pair-1";
        case RECOVERY_STAGE_VALIDATE_PAIR_1:
            return "validate-pair-1";
        case RECOVERY_STAGE_START_VALIDATE_PAIR_2:
            return "start-validate-pair-2";
        case RECOVERY_STAGE_VALIDATE_PAIR_2:
            return "validate-pair-2";
        case RECOVERY_STAGE_SELECT_PAIR:
            return "select-pair";
        default:
            return "unknown";
    }
}

static int SequenceIsNewer(uint32_t candidate, uint32_t reference)
{
    uint32_t difference = candidate - reference;

    return (difference != 0U) && (difference < 0x80000000UL);
}

static void Fail(recovery_service_t *service, firmware_status_t status, boot_error_t error)
{
    LOG_ERROR("recovery", "failed: status=%d error=%d stage=%s", (int) status, (int) error,
              RecoveryStageName(service->stage));
    service->state               = SERVICE_RUN_STATE_FAILED;
    service->result.status       = status;
    service->result.error        = error;
    service->result.stage        = (uint32_t) service->stage;
    service->result.native_error = (int32_t) status;
    service->stage               = RECOVERY_STAGE_IDLE;
}

static int CandidateIndex(boot_pair_t pair)
{
    return (pair == BOOT_PAIR_1) ? 0 : 1;
}

static void LoadCandidate(recovery_service_t *service, boot_pair_t pair)
{
    int index = CandidateIndex(pair);
    firmware_status_t status =
        service->load_candidate(service->candidate_context, pair, &service->candidates[index]);

    if (FirmwareStatus_IsOk(status))
    {
        if (!FirmwareStatus_IsOk(SlotPolicy_GetPairLayout(pair, &(boot_pair_layout_t) {0})))
        {
            service->candidate_present[index] = 0U;
            LOG_WARN("recovery", "candidate rejected: pair=%s", BootPairName(pair));
        }
        else
        {
            service->candidate_present[index] = 1U;
            LOG_INFO("recovery", "candidate loaded: pair=%s sequence=%lu version=%u.%u.%u",
                     BootPairName(pair), (unsigned long) service->candidates[index].sequence,
                     (unsigned int) service->candidates[index].release_version.major,
                     (unsigned int) service->candidates[index].release_version.minor,
                     (unsigned int) service->candidates[index].release_version.patch);
        }
    }
    else if ((status == FIRMWARE_STATUS_INVALID_STATE) || (status == FIRMWARE_STATUS_OUT_OF_RANGE))
    {
        service->candidate_present[index] = 0U;
        LOG_WARN("recovery", "candidate missing: pair=%s status=%d", BootPairName(pair),
                 (int) status);
    }
    else
    {
        Fail(service, status, BOOT_ERROR_CONTROL_RECORD);
        return;
    }
    service->stage =
        (pair == BOOT_PAIR_1) ? RECOVERY_STAGE_LOAD_PAIR_2 : RECOVERY_STAGE_START_VALIDATE_PAIR_1;
}

static void StartValidation(recovery_service_t *service, boot_pair_t pair,
                            recovery_stage_t next_stage)
{
    int index = CandidateIndex(pair);
    firmware_status_t status;

    if (service->candidate_present[index] == 0U)
    {
        LOG_DEBUG("recovery", "skip validation: pair=%s has no candidate", BootPairName(pair));
        service->stage = next_stage;
        return;
    }
    status = ActiveValidationService_Start(service->validation, &service->candidates[index]);
    if (!FirmwareStatus_IsOk(status))
    {
        service->candidate_valid[index] = 0U;
        service->stage                  = next_stage;
        LOG_WARN("recovery", "validation start failed: pair=%s status=%d", BootPairName(pair),
                 (int) status);
        return;
    }
    LOG_INFO("recovery", "validation started: pair=%s", BootPairName(pair));
    service->stage =
        (pair == BOOT_PAIR_1) ? RECOVERY_STAGE_VALIDATE_PAIR_1 : RECOVERY_STAGE_VALIDATE_PAIR_2;
}

static void ProcessValidation(recovery_service_t *service, boot_pair_t pair,
                              recovery_stage_t next_stage)
{
    int index = CandidateIndex(pair);
    service_run_state_t state;

    ActiveValidationService_Process(service->validation);
    state = ActiveValidationService_GetState(service->validation);
    if (state == SERVICE_RUN_STATE_SUCCEEDED)
    {
        service->candidate_valid[index] = 1U;
        service->stage                  = next_stage;
        LOG_INFO("recovery", "validation succeeded: pair=%s", BootPairName(pair));
    }
    else if (state == SERVICE_RUN_STATE_FAILED)
    {
        const service_result_t *result = ActiveValidationService_GetResult(service->validation);

        service->candidate_valid[index] = 0U;
        service->stage                  = next_stage;
        LOG_WARN("recovery", "validation failed: pair=%s status=%d error=%d stage=%lu",
                 BootPairName(pair),
                 (result == NULL) ? (int) FIRMWARE_STATUS_INVALID_STATE : (int) result->status,
                 (result == NULL) ? (int) BOOT_ERROR_INTERNAL : (int) result->error,
                 (result == NULL) ? 0UL : (unsigned long) result->stage);
    }
}

static void SelectCandidate(recovery_service_t *service)
{
    int valid_one = service->candidate_valid[0] != 0U;
    int valid_two = service->candidate_valid[1] != 0U;
    int selected  = -1;

    if ((service->preferred_pair == BOOT_PAIR_1) && valid_one)
    {
        selected = 0;
    }
    else if ((service->preferred_pair == BOOT_PAIR_2) && valid_two)
    {
        selected = 1;
    }
    else if (valid_one && !valid_two)
    {
        selected = 0;
    }
    else if (valid_two && !valid_one)
    {
        selected = 1;
    }
    else if (valid_one && valid_two)
    {
        uint32_t difference = service->candidates[0].sequence - service->candidates[1].sequence;

        /* V2 has no pair identity in the record. An equal serial therefore
         * remains ambiguous in this legacy recovery path. */
        if ((difference != 0U) && (difference != 0x80000000UL))
        {
            selected =
                SequenceIsNewer(service->candidates[0].sequence, service->candidates[1].sequence)
                    ? 0
                    : 1;
        }
    }

    if (selected < 0)
    {
        Fail(service, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_NO_VALID_PAIR);
        return;
    }
    service->selected_record = service->candidates[selected];
    LOG_INFO("recovery", "selected candidate: pair=%s sequence=%lu",
             BootPairName((selected == 0) ? BOOT_PAIR_1 : BOOT_PAIR_2),
             (unsigned long) service->selected_record.sequence);
    service->state               = SERVICE_RUN_STATE_SUCCEEDED;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = (uint32_t) service->stage;
    service->result.native_error = 0;
    service->stage               = RECOVERY_STAGE_IDLE;
}

firmware_status_t RecoveryService_Init(recovery_service_t *service,
                                       const recovery_service_dependencies_t *dependencies)
{
    if ((service == NULL) || (dependencies == NULL) || (dependencies->load_candidate == NULL) ||
        (dependencies->validation == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (service->initialized != 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    service->load_candidate      = dependencies->load_candidate;
    service->candidate_context   = dependencies->candidate_context;
    service->validation          = dependencies->validation;
    service->state               = SERVICE_RUN_STATE_IDLE;
    service->stage               = RECOVERY_STAGE_IDLE;
    service->result.status       = FIRMWARE_STATUS_OK;
    service->result.error        = BOOT_ERROR_NONE;
    service->result.stage        = RECOVERY_STAGE_IDLE;
    service->result.native_error = 0;
    service->initialized         = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t RecoveryService_Start(struct recovery_service *service,
                                        boot_pair_t preferred_pair)
{
    recovery_service_t *implementation = (recovery_service_t *) service;

    if (implementation == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((implementation->initialized == 0) || (implementation->state == SERVICE_RUN_STATE_RUNNING))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if ((preferred_pair != BOOT_PAIR_NONE) && (preferred_pair != BOOT_PAIR_1) &&
        (preferred_pair != BOOT_PAIR_2))
    {
        return FIRMWARE_STATUS_OUT_OF_RANGE;
    }
    memset(implementation->candidate_present, 0, sizeof(implementation->candidate_present));
    memset(implementation->candidate_valid, 0, sizeof(implementation->candidate_valid));
    implementation->preferred_pair      = preferred_pair;
    implementation->state               = SERVICE_RUN_STATE_RUNNING;
    implementation->stage               = RECOVERY_STAGE_LOAD_PAIR_1;
    implementation->result.status       = FIRMWARE_STATUS_OK;
    implementation->result.error        = BOOT_ERROR_NONE;
    implementation->result.stage        = RECOVERY_STAGE_LOAD_PAIR_1;
    implementation->result.native_error = 0;
    LOG_INFO("recovery", "started: preferred=%s", BootPairName(preferred_pair));
    return FIRMWARE_STATUS_OK;
}

void RecoveryService_Process(struct recovery_service *service)
{
    recovery_service_t *implementation = (recovery_service_t *) service;
    if ((implementation == NULL) || (implementation->state != SERVICE_RUN_STATE_RUNNING))
    {
        return;
    }
    switch (implementation->stage)
    {
        case RECOVERY_STAGE_LOAD_PAIR_1:
            LoadCandidate(implementation, BOOT_PAIR_1);
            break;
        case RECOVERY_STAGE_LOAD_PAIR_2:
            LoadCandidate(implementation, BOOT_PAIR_2);
            break;
        case RECOVERY_STAGE_START_VALIDATE_PAIR_1:
            StartValidation(implementation, BOOT_PAIR_1, RECOVERY_STAGE_START_VALIDATE_PAIR_2);
            break;
        case RECOVERY_STAGE_VALIDATE_PAIR_1:
            ProcessValidation(implementation, BOOT_PAIR_1, RECOVERY_STAGE_START_VALIDATE_PAIR_2);
            break;
        case RECOVERY_STAGE_START_VALIDATE_PAIR_2:
            StartValidation(implementation, BOOT_PAIR_2, RECOVERY_STAGE_SELECT_PAIR);
            break;
        case RECOVERY_STAGE_VALIDATE_PAIR_2:
            ProcessValidation(implementation, BOOT_PAIR_2, RECOVERY_STAGE_SELECT_PAIR);
            break;
        case RECOVERY_STAGE_SELECT_PAIR:
            SelectCandidate(implementation);
            break;
        default:
            Fail(implementation, FIRMWARE_STATUS_INVALID_STATE, BOOT_ERROR_INTERNAL);
            break;
    }
}

service_run_state_t RecoveryService_GetState(const struct recovery_service *service)
{
    return (service == NULL) ? SERVICE_RUN_STATE_FAILED
                             : ((const recovery_service_t *) service)->state;
}

const service_result_t *RecoveryService_GetResult(const struct recovery_service *service)
{
    return (service == NULL) ? NULL : &((const recovery_service_t *) service)->result;
}

const boot_active_record_t *RecoveryService_GetCandidate(const struct recovery_service *service)
{
    const recovery_service_t *implementation = (const recovery_service_t *) service;

    return ((implementation == NULL) || (implementation->state != SERVICE_RUN_STATE_SUCCEEDED))
               ? NULL
               : &implementation->selected_record;
}
