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
static boot_active_record_t committed_record;
static service_run_state_t validation_state;
static service_run_state_t boot_control_state;
static service_result_t validation_result;
static service_result_t boot_control_result;
static firmware_status_t commit_start_status;
static uint32_t commit_count;

static active_validation_service_t validation_service;
static boot_control_service_t boot_control_service;
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
    validation_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void ActiveValidationService_Process(struct active_validation_service *service)
{
    int index;

    (void)service;
    index = CandidateIndex(validation_record.active_pair);
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

firmware_status_t BootControlService_CommitActiveStart(
    struct boot_control_service *service,
    const boot_active_record_t *record)
{
    (void)service;
    if (!FirmwareStatus_IsOk(commit_start_status))
    {
        return commit_start_status;
    }
    committed_record = *record;
    ++commit_count;
    boot_control_state = SERVICE_RUN_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

void BootControlService_Process(struct boot_control_service *service)
{
    (void)service;
    boot_control_state = SERVICE_RUN_STATE_SUCCEEDED;
}

service_run_state_t BootControlService_GetState(
    const struct boot_control_service *service)
{
    (void)service;
    return boot_control_state;
}

const service_result_t *BootControlService_GetResult(
    const struct boot_control_service *service)
{
    (void)service;
    return &boot_control_result;
}

static boot_active_record_t MakeRecord(boot_pair_t pair, uint32_t sequence)
{
    boot_active_record_t record;

    memset(&record, 0, sizeof(record));
    record.sequence = sequence;
    record.active_pair = pair;
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
    memset(&committed_record, 0, sizeof(committed_record));
    memset(&validation_result, 0, sizeof(validation_result));
    memset(&boot_control_result, 0, sizeof(boot_control_result));
    memset(&validation_service, 0, sizeof(validation_service));
    memset(&boot_control_service, 0, sizeof(boot_control_service));
    memset(&recovery_service, 0, sizeof(recovery_service));
    validation_state = SERVICE_RUN_STATE_IDLE;
    boot_control_state = SERVICE_RUN_STATE_IDLE;
    commit_start_status = FIRMWARE_STATUS_OK;
    commit_count = 0U;
    dependencies.load_candidate = LoadCandidate;
    dependencies.candidate_context = &candidates;
    dependencies.validation = &validation_service;
    dependencies.boot_control = &boot_control_service;
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
    candidates.records[0] = MakeRecord(BOOT_PAIR_1, 4U);
    candidates.present[0] = 1;
    candidates.valid[0] = 1;
    assert(
        RecoveryService_Start(&recovery_service, BOOT_PAIR_NONE) ==
        FIRMWARE_STATUS_OK);
    RunToTerminal();
    assert(
        RecoveryService_GetState(&recovery_service) ==
        SERVICE_RUN_STATE_SUCCEEDED);
    assert(commit_count == 1U);
    assert(committed_record.active_pair == BOOT_PAIR_1);
}

static void TestNewestValidPairWins(void)
{
    ResetFixture();
    candidates.records[0] = MakeRecord(BOOT_PAIR_1, 7U);
    candidates.records[1] = MakeRecord(BOOT_PAIR_2, 8U);
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
    assert(commit_count == 1U);
    assert(committed_record.active_pair == BOOT_PAIR_2);
}

static void TestEqualSequenceConflictIsRejected(void)
{
    const service_result_t *result;

    ResetFixture();
    candidates.records[0] = MakeRecord(BOOT_PAIR_1, 9U);
    candidates.records[1] = MakeRecord(BOOT_PAIR_2, 9U);
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
    assert(commit_count == 0U);
}

static void TestCommitStartFailureIsReported(void)
{
    const service_result_t *result;

    ResetFixture();
    candidates.records[1] = MakeRecord(BOOT_PAIR_2, 3U);
    candidates.present[1] = 1;
    candidates.valid[1] = 1;
    commit_start_status = FIRMWARE_STATUS_IO_ERROR;
    assert(
        RecoveryService_Start(&recovery_service, BOOT_PAIR_NONE) ==
        FIRMWARE_STATUS_OK);
    RunToTerminal();
    result = RecoveryService_GetResult(&recovery_service);
    assert(
        RecoveryService_GetState(&recovery_service) ==
        SERVICE_RUN_STATE_FAILED);
    assert(result != NULL);
    assert(result->status == FIRMWARE_STATUS_IO_ERROR);
    assert(result->error == BOOT_ERROR_EEPROM_COMMIT);
    assert(commit_count == 0U);
}

int main(void)
{
    TestSingleValidPair();
    TestNewestValidPairWins();
    TestEqualSequenceConflictIsRejected();
    TestCommitStartFailureIsReported();
    return 0;
}
