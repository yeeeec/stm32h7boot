/**
 * @file update_service.h
 * @brief Fixed APP/GUI runtime installer state and dependencies.
 */
#ifndef SERVICES_UPDATE_SERVICE_INTERNAL_H
#define SERVICES_UPDATE_SERVICE_INTERNAL_H

#include <stdint.h>

#include "firmware/async_block_device.h"
#include "firmware/hash.h"
#include "firmware/package_source.h"
#include "services/capability/manifest_service.h"
#include "services/capability/update_request_service.h"
#include "services/common/boot_control_types.h"
#include "services/common/runtime_layout.h"
#include "services/use_case/update_service_api.h"

#define UPDATE_SERVICE_MANIFEST_MAX_SIZE 16384U
#define UPDATE_SERVICE_IO_BUFFER_MIN_SIZE 4096U

typedef struct
{
    const package_source_t *package_source;
    manifest_service_t *manifest_service;
    update_request_service_t *update_request_service;
    const hash_provider_t *hash;
    const async_block_device_t *storage;
    const boot_runtime_layout_t *runtime_layout;
    uint8_t *manifest_buffer;
    uint32_t manifest_buffer_size;
    uint8_t *io_buffer;
    uint32_t io_buffer_size;
} update_service_dependencies_t;

typedef enum
{
    UPDATE_STAGE_IDLE = 0,
    UPDATE_STAGE_PREPARE_MANIFEST_OPEN,
    UPDATE_STAGE_PREPARE_MANIFEST_SIZE,
    UPDATE_STAGE_PREPARE_MANIFEST_READ,
    UPDATE_STAGE_PREPARE_MANIFEST_CLOSE,
    UPDATE_STAGE_PREPARE_MANIFEST_PARSE,
    UPDATE_STAGE_PREPARED,
    UPDATE_STAGE_SOURCE_APP_OPEN,
    UPDATE_STAGE_SOURCE_APP_SIZE,
    UPDATE_STAGE_SOURCE_APP_HASH,
    UPDATE_STAGE_SOURCE_APP_VERIFY,
    UPDATE_STAGE_SOURCE_GUI_OPEN,
    UPDATE_STAGE_SOURCE_GUI_SIZE,
    UPDATE_STAGE_SOURCE_GUI_HASH,
    UPDATE_STAGE_SOURCE_GUI_VERIFY,
    UPDATE_STAGE_APP_ERASE,
    UPDATE_STAGE_APP_ERASE_POLL,
    UPDATE_STAGE_APP_PROGRAM_OPEN,
    UPDATE_STAGE_APP_PROGRAM_READ,
    UPDATE_STAGE_APP_PROGRAM_START,
    UPDATE_STAGE_APP_PROGRAM_POLL,
    UPDATE_STAGE_APP_PROGRAM_HASH,
    UPDATE_STAGE_APP_TARGET_READ,
    UPDATE_STAGE_APP_TARGET_HASH,
    UPDATE_STAGE_GUI_ERASE,
    UPDATE_STAGE_GUI_ERASE_POLL,
    UPDATE_STAGE_GUI_PROGRAM_OPEN,
    UPDATE_STAGE_GUI_PROGRAM_READ,
    UPDATE_STAGE_GUI_PROGRAM_START,
    UPDATE_STAGE_GUI_PROGRAM_POLL,
    UPDATE_STAGE_GUI_PROGRAM_HASH,
    UPDATE_STAGE_GUI_TARGET_READ,
    UPDATE_STAGE_GUI_TARGET_HASH,
    UPDATE_STAGE_BUILD_RECORD_CANDIDATE
} update_stage_t;

typedef struct update_service
{
    const package_source_t *package_source;
    manifest_service_t *manifest_service;
    update_request_service_t *update_request_service;
    const hash_provider_t *hash;
    const async_block_device_t *storage;
    const boot_runtime_layout_t *runtime_layout;
    async_block_device_info_t storage_info;
    uint8_t *manifest_buffer;
    uint32_t manifest_buffer_size;
    uint8_t *io_buffer;
    uint32_t io_buffer_size;

    update_request_t request;
    validated_manifest_t manifest;
    boot_active_record_t candidate_record;
    uint8_t source_digest[FIRMWARE_SHA256_DIGEST_SIZE];
    uint8_t target_digest[FIRMWARE_SHA256_DIGEST_SIZE];

    service_run_state_t state;
    service_result_t result;
    update_stage_t stage;
    firmware_status_t failure_status;
    boot_error_t failure_error;
    uint32_t manifest_size;
    uint32_t manifest_offset;
    uint32_t source_offset;
    uint32_t target_offset;
    uint32_t erase_offset;
    uint32_t program_offset;
    uint32_t pending_program_size;
    uint32_t active_source_size;
    const manifest_app_component_t *active_component;
    uint32_t active_runtime_offset;
    uint32_t active_runtime_size;
    int source_file_open;
    int candidate_ready;
    int runtime_may_be_modified;
    update_stage_t logged_stage;
    int stage_logged;
    int initialized;
} update_service_t;

firmware_status_t UpdateService_Init(
    update_service_t *service,
    const update_service_dependencies_t *dependencies);

#endif
