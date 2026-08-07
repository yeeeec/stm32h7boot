#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "services/use_case/recovery_service.h"

typedef struct
{
    boot_active_record_t records[2];
    int present[2];
    int valid[2];
} candidate_context_t;

static candidate_context_t candidates;
static boot_active_record_t validation_record;
static service_run_state_t validation_state;
static service_result_t validation_result;
static int validation_index;

static active_validation_service_t validation_service;
static recovery_service_t recovery_service;

static int CandidateIndex(boot_pair_t pair)
{
    return (pair == BOOT_PAIR_1) ? 0 : 1;
}

static firmware_status_t LoadCandidate(
    void *context,
    boot_pair_t pair,
    boot_active_record_t *candidate)
{
    candidate_context_t *fixture = (candidate_context_t *)context;
    int index = CandidateIndex(pair);

    if (fixture->present[index] == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    *candidate = fixture->records[index];
    return FIRMWARE_STATUS_OK;
}

firmware_status_t ActiveValidationService_Start(
    struct active_validation_service *service,
    const boot_active_record_t *active_record)
{
    (void)service;
    validation_record = *active_record;
    validation_index = (active_record->sequence == candidates.records[1].sequence) ? 1 : 0;
    validation_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void ActiveValidationService_Process(struct active_validation_service *service)
{
    int index;

    (void)service;
    index = validation_index;
    validation_state = (candidates.valid[index] != 0)
                           ? SERVICE_RUN_STATE_SUCCEEDED
                           : SERVICE_RUN_STATE_FAILED;
    validation_result.status = (candidates.valid[index] != 0)
                                   ? FIRMWARE_STATUS_OK
                                   : FIRMWARE_STATUS_INVALID_STATE;
}

service_run_state_t ActiveValidationService_GetState(
    const struct active_validation_service *service)
{
    (void)service;
    return validation_state;
}

const service_result_t *ActiveValidationService_GetResult(
    const struct active_validation_service *service)
{
    (void)service;
    return &validation_result;
}

static boot_active_record_t MakeRecord(uint32_t sequence)
{
    boot_active_record_t record;

    memset(&record, 0, sizeof(record));
    record.sequence = sequence;
    record.format_version = BOOT_ACTIVE_RECORD_FORMAT_V2;
    record.state = BOOT_ACTIVE_RECORD_STATE_VALID;
    record.app_size = 8U;
    record.gui_size = 8U;
    record.release_version.major = (uint16_t)sequence;
    return record;
}

static void ResetFixture(void)
{
    recovery_service_dependencies_t dependencies;

    memset(&candidates, 0, sizeof(candidates));
    memset(&validation_record, 0, sizeof(validation_record));
    memset(&validation_result, 0, sizeof(validation_result));
    memset(&validation_service, 0, sizeof(validation_service));
    memset(&recovery_service, 0, sizeof(recovery_service));
    validation_state = SERVICE_RUN_STATE_IDLE;
    validation_index = 0;
    dependencies.load_candidate = LoadCandidate;
    dependencies.candidate_context = &candidates;
    dependencies.validation = &validation_service;
    assert(
        RecoveryService_Init(&recovery_service, &dependencies) ==
        FIRMWARE_STATUS_OK);
}

static void RunToTerminal(void)
{
    uint32_t steps;

    for (steps = 0U; steps < 32U; ++steps)
    {
        if (RecoveryService_GetState(&recovery_service) !=
            SERVICE_RUN_STATE_RUNNING)
        {
            return;
        }
        RecoveryService_Process(&recovery_service);
    }
    assert(0 && "Recovery Service did not reach a terminal state");
}

static void TestSingleValidPair(void)
{
    ResetFixture();
    candidates.records[0] = MakeRecord(4U);
    candidates.present[0] = 1;
    candidates.valid[0] = 1;
    assert(
        RecoveryService_Start(&recovery_service, BOOT_PAIR_NONE) ==
        FIRMWARE_STATUS_OK);
    RunToTerminal();
    assert(
        RecoveryService_GetState(&recovery_service) ==
        SERVICE_RUN_STATE_SUCCEEDED);
    assert(RecoveryService_GetCandidate(&recovery_service) != NULL);
    assert(RecoveryService_GetCandidate(&recovery_service)->sequence == 4U);
}

static void TestNewestValidPairWins(void)
{
    ResetFixture();
    candidates.records[0] = MakeRecord(7U);
    candidates.records[1] = MakeRecord(8U);
    candidates.present[0] = 1;
    candidates.present[1] = 1;
    candidates.valid[0] = 1;
    candidates.valid[1] = 1;
    assert(
        RecoveryService_Start(&recovery_service, BOOT_PAIR_NONE) ==
        FIRMWARE_STATUS_OK);
    RunToTerminal();
    assert(
        RecoveryService_GetState(&recovery_service) ==
        SERVICE_RUN_STATE_SUCCEEDED);
    assert(RecoveryService_GetCandidate(&recovery_service) != NULL);
    assert(RecoveryService_GetCandidate(&recovery_service)->sequence == 8U);
}

static void TestEqualSequenceConflictIsRejected(void)
{
    const service_result_t *result;

    ResetFixture();
    candidates.records[0] = MakeRecord(9U);
    candidates.records[1] = MakeRecord(9U);
    candidates.present[0] = 1;
    candidates.present[1] = 1;
    candidates.valid[0] = 1;
    candidates.valid[1] = 1;
    assert(
        RecoveryService_Start(&recovery_service, BOOT_PAIR_NONE) ==
        FIRMWARE_STATUS_OK);
    RunToTerminal();
    result = RecoveryService_GetResult(&recovery_service);
    assert(
        RecoveryService_GetState(&recovery_service) ==
        SERVICE_RUN_STATE_FAILED);
    assert(result != NULL);
    assert(result->error == BOOT_ERROR_NO_VALID_PAIR);
    assert(RecoveryService_GetCandidate(&recovery_service) == NULL);
}

int main(void)
{
    TestSingleValidPair();
    TestNewestValidPairWins();
    TestEqualSequenceConflictIsRejected();
    return 0;
}
