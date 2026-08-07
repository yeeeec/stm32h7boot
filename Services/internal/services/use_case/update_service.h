/**
 * @file update_service.h
 * @brief Composition-visible package preparation and installer state.
 */
#ifndef SERVICES_UPDATE_SERVICE_INTERNAL_H
#define SERVICES_UPDATE_SERVICE_INTERNAL_H

#include <stdint.h>

#include "firmware/async_block_device.h"
#include "firmware/checksum.h"
#include "firmware/hash.h"
#include "firmware/package_source.h"
#include "services/capability/manifest_service.h"
#include "services/capability/relocation_service.h"
#include "services/common/manifest_types.h"
#include "services/use_case/update_service_api.h"

#define UPDATE_SERVICE_MANIFEST_MAX_SIZE 16384U
#define UPDATE_SERVICE_IO_BUFFER_MIN_SIZE 4096U
#define UPDATE_SERVICE_MAX_RELOCATIONS 4096U

typedef struct
{
    const package_source_t *package_source;
    const async_block_device_t *storage;
    const checksum_t *checksum;
    const hash_provider_t *hash;
    manifest_service_t *manifest_service;
    const char *manifest_path;
    const char *app_path;
    const char *relocation_path;
    const char *gui_path;
    uint8_t *manifest_buffer;
    uint32_t manifest_buffer_size;
    uint8_t *io_buffer;
    uint32_t io_buffer_size;
    uint8_t *relocation_buffer;
    uint32_t relocation_buffer_size;
    hmi_relocation_entry_t *relocation_entries;
    uint32_t relocation_entry_capacity;
} update_service_dependencies_t;

typedef enum
{
    UPDATE_STAGE_IDLE = 0,
    UPDATE_STAGE_OPEN_MANIFEST,
    UPDATE_STAGE_READ_MANIFEST,
    UPDATE_STAGE_CLOSE_MANIFEST,
    UPDATE_STAGE_VERIFY_MANIFEST,
    UPDATE_STAGE_SELECT_TARGET,
    UPDATE_STAGE_OPEN_APP,
    UPDATE_STAGE_PREPARE_APP,
    UPDATE_STAGE_HASH_APP,
    UPDATE_STAGE_OPEN_APP_RELOCATIONS,
    UPDATE_STAGE_PREPARE_APP_RELOCATIONS,
    UPDATE_STAGE_READ_APP_RELOCATIONS,
    UPDATE_STAGE_VALIDATE_APP_RELOCATION,
    UPDATE_STAGE_READ_APP_RELOCATION_WORD,
    UPDATE_STAGE_CHECK_APP_RELOCATION_WORD,
    UPDATE_STAGE_CLOSE_APP,
    UPDATE_STAGE_OPEN_APP_RELOCATION_WORDS,
    UPDATE_STAGE_CLOSE_APP_RELOCATION_WORDS,
    UPDATE_STAGE_CLOSE_APP_RELOCATIONS,
    UPDATE_STAGE_OPEN_GUI,
    UPDATE_STAGE_PREPARE_GUI,
    UPDATE_STAGE_HASH_GUI,
    UPDATE_STAGE_CLOSE_GUI_SOURCE,
    UPDATE_STAGE_ERASE_APP_START,
    UPDATE_STAGE_ERASE_APP_POLL,
    UPDATE_STAGE_PROGRAM_APP_START,
    UPDATE_STAGE_PROGRAM_APP_POLL,
    UPDATE_STAGE_READ_APP_TARGET,
    UPDATE_STAGE_FINISH_APP_TARGET,
    UPDATE_STAGE_OPEN_APP_PROGRAM,
    UPDATE_STAGE_READ_APP_PROGRAM_BLOCK,
    UPDATE_STAGE_APPLY_APP_PROGRAM_BLOCK,
    UPDATE_STAGE_CLOSE_APP_PROGRAM,
    UPDATE_STAGE_ERASE_GUI_START,
    UPDATE_STAGE_ERASE_GUI_POLL,
    UPDATE_STAGE_PROGRAM_GUI_START,
    UPDATE_STAGE_PROGRAM_GUI_POLL,
    UPDATE_STAGE_READ_GUI_TARGET,
    UPDATE_STAGE_FINISH_GUI_TARGET,
    UPDATE_STAGE_OPEN_GUI_PROGRAM,
    UPDATE_STAGE_READ_GUI_PROGRAM_BLOCK,
    UPDATE_STAGE_CLOSE_GUI_PROGRAM,
    UPDATE_STAGE_CLEANUP_CLOSE
} update_stage_t;

typedef struct update_service
{
    const package_source_t *package_source;
    const async_block_device_t *storage;
    async_block_device_info_t storage_info;
    const checksum_t *checksum;
    const hash_provider_t *hash;
    manifest_service_t *manifest_service;
    const char *manifest_path;
    const char *app_path;
    const char *relocation_path;
    const char *gui_path;
    uint8_t *manifest_buffer;
    uint32_t manifest_buffer_size;
    uint8_t *io_buffer;
    uint32_t io_buffer_size;
    uint8_t *relocation_buffer;
    uint32_t relocation_buffer_size;
    hmi_relocation_entry_t *relocation_entries;
    uint32_t relocation_entry_capacity;
    boot_active_record_t active_record;
    boot_pair_t initial_target_pair;
    validated_manifest_t manifest;
    boot_pair_layout_t target_layout;
    relocation_service_t relocation;
    boot_active_record_t candidate_record;
    int manifest_prepared;
    int initial_install;
    int install_completed;
    service_run_state_t state;
    service_result_t result;
    update_stage_t stage;
    firmware_status_t failure_status;
    boot_error_t failure_error;
    uint32_t failure_stage;
    uint32_t manifest_size;
    uint32_t manifest_offset;
    uint32_t file_size;
    uint32_t file_offset;
    uint32_t file_chunk_size;
    uint32_t relocation_bytes;
    uint32_t relocation_index;
    uint32_t relocation_apply_index;
    uint32_t app_block_offset;
    uint32_t app_block_size;
    uint32_t program_offset;
    uint32_t erase_offset;
    uint32_t target_read_offset;
    uint32_t gui_block_size;
    uint32_t gui_program_offset;
    uint32_t gui_target_read_offset;
    uint32_t pending_program_size;
    uint32_t pending_relocation_word;
    hmi_relocation_entry_t pending_relocation;
    uint32_t app_source_crc;
    uint32_t app_relocation_crc;
    uint32_t gui_source_crc;
    uint32_t target_app_crc;
    uint32_t target_gui_crc;
    int manifest_size_known;
    int file_open;
    int erase_started;
    int cancel_requested;
    int failure_pending;
    int initialized;
} update_service_t;

firmware_status_t UpdateService_Init(
    update_service_t *service,
    const update_service_dependencies_t *dependencies);

#endif
