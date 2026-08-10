/**
 * @file application.c
 * @brief 受信固定 Runtime 更新的顶层 Application 编排实现。
 *
 * 本模块持有 Bootloader 的唯一顶层状态机。它负责选择继续启动、执行更新、
 * 恢复复位或 fail-closed，但把具体 I/O 和领域校验委托给注入的 Service。
 */
#include "application/application.h"
#include "application/application_config.h"

#include <stddef.h>
#include <string.h>

#include "logging.h"
#include "services/capability/boot_control_service_api.h"
#include "services/capability/update_request_service_api.h"
#include "services/capability/version_policy.h"
#include "services/use_case/active_validation_service_api.h"
#include "services/use_case/launch_service_api.h"
#include "services/use_case/secondary_mcu_update_service_api.h"
#include "services/use_case/update_service_api.h"

/** 清理发布卷时允许的最大卸载尝试次数。 */
#define APPLICATION_UNMOUNT_RETRY_LIMIT 3U

/** Application 从启动检查到 APP 交接的顶层编排阶段。 */
typedef enum
{
    /** 初始化完成，等待进入每次启动都必须执行的更新介质检查。 */
    APPLICATION_STAGE_STARTUP = 0,
    /** 查询发布介质是否存在；不存在时转入当前 Runtime 校验。 */
    APPLICATION_STAGE_UPDATE_CHECK,
    /** 挂载包含 Trusted Request 和发布包的介质。 */
    APPLICATION_STAGE_MOUNT,
    /** 从已挂载卷加载 Trusted Request 原始文档。 */
    APPLICATION_STAGE_REQUEST_LOAD,
    /** 解析请求并启动发布包 Prepare 生命周期。 */
    APPLICATION_STAGE_PREPARE_START,
    /** 增量驱动发布包读取、绑定和源文件预校验。 */
    APPLICATION_STAGE_PREPARE_PROCESS,
    /** 检查 Bootloader 最低版本、重复请求和升级版本策略。 */
    APPLICATION_STAGE_POLICY,
    /** 对重复请求所指向的当前 Runtime 启动校验。 */
    APPLICATION_STAGE_STALE_VALIDATE_START,
    /** 增量驱动重复请求场景下的当前 Runtime 校验。 */
    APPLICATION_STAGE_STALE_VALIDATE_PROCESS,
    /** 启动请求选择的 APP/GUI Runtime 安装生命周期。 */
    APPLICATION_STAGE_INSTALL_START,
    /** 增量驱动 Runtime 擦除、写入和回读校验。 */
    APPLICATION_STAGE_INSTALL_PROCESS,
    /** 打开 therapy.app.bin，Application 取得文件所有权。 */
    APPLICATION_STAGE_THERAPY_OPEN,
    /** 以 Manifest 尺寸与摘要启动 Therapy MCU 更新。 */
    APPLICATION_STAGE_THERAPY_START,
    /** 增量驱动 Therapy MCU 擦写与回读状态机。 */
    APPLICATION_STAGE_THERAPY_PROCESS,
    /** 关闭 therapy.app.bin 并进入提交或失败恢复。 */
    APPLICATION_STAGE_THERAPY_CLOSE,
    /** 启动候选 Active Record 原子提交。 */
    APPLICATION_STAGE_COMMIT_START,
    /** 增量驱动候选 Active Record 提交。 */
    APPLICATION_STAGE_COMMIT_PROCESS,
    /** 清除已消费的 Trusted Request 并选择后续动作。 */
    APPLICATION_STAGE_CLEANUP,
    /** 释放 Application 持有的发布卷挂载所有权。 */
    APPLICATION_STAGE_UNMOUNT,
    /** 启动当前 Active Runtime 完整性校验。 */
    APPLICATION_STAGE_VALIDATE_START,
    /** 增量驱动当前 Active Runtime 完整性校验。 */
    APPLICATION_STAGE_VALIDATE_PROCESS,
    /** 配置 XIP 并把控制权交给已校验 APP。 */
    APPLICATION_STAGE_LAUNCH,
    /** 新 Active Record 提交完成后请求全系统复位。 */
    APPLICATION_STAGE_RESET,
    /** Runtime 可能已损坏时保留请求并复位进入恢复安装。 */
    APPLICATION_STAGE_RECOVERY_RESET,
    /** 不允许继续更新或启动的 fail-closed 终态。 */
    APPLICATION_STAGE_FAILED
} application_stage_t;

/** Application 借用的依赖图；仅由 Application_Configure() 写入一次。 */
static application_dependencies_t application_dependencies;
/** 当前由 Boot Control 选中、允许参与校验和启动的 Active Record。 */
static boot_active_record_t application_active_record;
/** 安装成功后等待提交的候选 Active Record 副本。 */
static boot_active_record_t application_candidate_record;
/** 由 Trusted Request 原始文档解析得到的受信请求。 */
static update_request_t application_request;
/** Trusted Request 的固定容量原始文档缓冲区。 */
static uint8_t application_request_raw[UPDATE_REQUEST_STORE_MAX_RAW_SIZE];
/** application_request_raw 中本次加载的有效字节数。 */
static uint32_t application_request_raw_size;
/** 当前顶层编排阶段。 */
static application_stage_t application_stage;
/** 发布卷成功卸载后需要进入的阶段。 */
static application_stage_t application_after_unmount;
/** 依赖图已通过校验并发布的标志。 */
static int application_configured;
/** Application_Init() 已成功完成的标志。 */
static int application_initialized;
/** Application 当前是否持有发布卷的挂载所有权。 */
static int application_media_mounted;
/** 当前卸载恢复流程已执行的尝试次数。 */
static uint32_t application_unmount_attempts;
/** application_active_record 是否包含可用记录。 */
static int application_has_active_record;
/** 当前请求是否与 Active Record 绑定到同一发布包。 */
static int application_stale_request;
/** 执行 Trusted Request 清理步骤后是否必须通过复位启用新 Runtime。 */
static int application_reset_after_cleanup;
/** therapy.app.bin 当前是否由 Application 打开。 */
static int application_therapy_file_open;
/** therapy 文件关闭成功后进入的顶层阶段。 */
static application_stage_t application_after_therapy_close;
/** therapy 文件关闭的有限重试次数。 */
static uint32_t application_therapy_close_attempts;
/** 当前阶段是否已经输出过入口日志。 */
static int application_stage_logged;
/** 最近一次输出入口日志的阶段。 */
static application_stage_t application_logged_stage;

/**
 * @brief 将顶层阶段转换为稳定的诊断文本。
 *
 * @param[in] stage 待转换的 Application 阶段。
 * @return 静态只读阶段名称；未知枚举值返回 "unknown"。
 */
static const char *ApplicationStageName(application_stage_t stage)
{
    static const char *const names[] = {"startup",
                                        "update-check",
                                        "mount",
                                        "request-load",
                                        "prepare-start",
                                        "prepare-process",
                                        "policy",
                                        "stale-validate-start",
                                        "stale-validate-process",
                                        "install-start",
                                        "install-process",
                                        "therapy-open",
                                        "therapy-start",
                                        "therapy-process",
                                        "therapy-close",
                                        "commit-start",
                                        "commit-process",
                                        "cleanup",
                                        "unmount",
                                        "validate-start",
                                        "validate-process",
                                        "launch",
                                        "reset",
                                        "recovery-reset",
                                        "failed"};
    return ((unsigned int) stage < (sizeof(names) / sizeof(names[0]))) ? names[stage] : "unknown";
}

/** @brief 每个阶段首次被处理时输出一次入口日志。 */
static void LogStageEntry(void)
{
    if ((application_stage_logged == 0) || (application_logged_stage != application_stage))
    {
        LOG_INFO("app", "stage=%s", ApplicationStageName(application_stage));
        application_logged_stage = application_stage;
        application_stage_logged = 1;
    }
}

/** @brief 返回当前是否存在允许校验和启动的 Active Record。 */
static int HasActiveRecord(void)
{
    return application_has_active_record != 0;
}

static void BeginUnmount(application_stage_t next);

/** @brief 返回本次请求是否选择给定组件位。 */
static int ComponentSelected(uint32_t component)
{
    return (application_request.component_mask & component) != 0U;
}

/** @brief 返回当前记录是否已持久化 Therapy MCU 版本。 */
static int HasTherapyRecord(void)
{
    return HasActiveRecord() &&
           ((application_active_record.component_mask & UPDATE_COMPONENT_THERAPY) != 0U) &&
           (application_active_record.therapy_size != 0U);
}

/** @brief 判断 Composition 是否提供了完整的 Therapy MCU 更新能力。 */
static int TherapyDependenciesReady(void)
{
    const package_source_t *source               = application_dependencies.package_source;
    const secondary_mcu_update_request_t *target = &application_dependencies.secondary_mcu_target;

    return (application_dependencies.secondary_mcu_update != NULL) && (source != NULL) &&
           (source->open != NULL) && (source->close != NULL) &&
           (target->target_capacity_bytes == MANIFEST_THERAPY_MAX_SIZE) &&
           (target->erase_page_count != 0U);
}

/** @brief 为 therapy-only 请求从当前记录构造待更新候选记录。 */
static void PrepareTherapyOnlyCandidate(const validated_manifest_t *manifest)
{
    application_candidate_record       = application_active_record;
    application_candidate_record.state = BOOT_ACTIVE_RECORD_STATE_VALID;
    if (application_candidate_record.component_mask == 0U)
    {
        application_candidate_record.component_mask = UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI;
    }
    memcpy(application_candidate_record.package_id_hash, manifest->package_id_hash128,
           sizeof(application_candidate_record.package_id_hash));
    memcpy(application_candidate_record.manifest_sha256, manifest->manifest_sha256,
           sizeof(application_candidate_record.manifest_sha256));
}

/** @brief 关闭 therapy 文件后，按目标阶段选择继续提交或先卸载恢复。 */
static void FinishTherapyClose(void)
{
    if (application_after_therapy_close == APPLICATION_STAGE_COMMIT_START)
    {
        application_stage = APPLICATION_STAGE_COMMIT_START;
    }
    else
    {
        BeginUnmount(application_after_therapy_close);
    }
}

/**
 * @brief 在必要时先卸载发布卷，再进入指定的后续阶段。
 *
 * @param[in] next 卸载成功或无需卸载时进入的阶段。
 */
static void BeginUnmount(application_stage_t next)
{
    application_after_unmount = next;
    if (application_media_mounted != 0)
    {
        /* 每次进入卸载恢复都从零开始计数，但不伪造当前介质状态。 */
        application_unmount_attempts = 0U;
        application_stage            = APPLICATION_STAGE_UNMOUNT;
    }
    else
    {
        application_stage = next;
    }
}

/** @brief 将顶层状态机置入禁止继续启动的 fail-closed 终态。 */
static void FailClosed(void)
{
    application_stage = APPLICATION_STAGE_FAILED;
}

/** @brief 放弃当前更新路径，卸载介质后校验现有 Runtime。 */
static void ContinueCurrentRuntime(void)
{
    BeginUnmount(HasActiveRecord() ? APPLICATION_STAGE_VALIDATE_START : APPLICATION_STAGE_FAILED);
}

/**
 * @brief 判断已准备的 Manifest 是否就是当前 Active Record 对应的发布包。
 *
 * @param[in] manifest Update Service 完成绑定校验后的 Manifest，允许为 NULL。
 * @return 非零表示 Package ID 和 Manifest 摘要均与 Active Record 相同。
 */
static int IsSameManifestIdentity(const validated_manifest_t *manifest)
{
    return HasActiveRecord() && (manifest != NULL) &&
           (memcmp(application_active_record.package_id_hash, manifest->package_id_hash128,
                   sizeof(application_active_record.package_id_hash)) == 0) &&
           (memcmp(application_active_record.manifest_sha256, manifest->manifest_sha256,
                   sizeof(application_active_record.manifest_sha256)) == 0);
}

static int IsSamePackage(const validated_manifest_t *manifest)
{
    if (!IsSameManifestIdentity(manifest))
    {
        return 0;
    }
    if (ComponentSelected(UPDATE_COMPONENT_APP) &&
        ((application_active_record.app_size != manifest->app.size_bytes) ||
         (memcmp(application_active_record.app_sha256, manifest->app.sha256,
                 sizeof(application_active_record.app_sha256)) != 0)))
    {
        return 0;
    }
    if (ComponentSelected(UPDATE_COMPONENT_GUI) &&
        ((application_active_record.gui_size != manifest->gui.size_bytes) ||
         (memcmp(application_active_record.gui_sha256, manifest->gui.sha256,
                 sizeof(application_active_record.gui_sha256)) != 0)))
    {
        return 0;
    }
    if (ComponentSelected(UPDATE_COMPONENT_THERAPY) &&
        (!HasTherapyRecord() ||
         (application_active_record.therapy_size != manifest->therapy.size_bytes) ||
         (memcmp(application_active_record.therapy_sha256, manifest->therapy.sha256,
                 sizeof(application_active_record.therapy_sha256)) != 0)))
    {
        return 0;
    }
    return 1;
}

/** @brief 有 Active Record 时进入校验，否则直接进入 fail-closed 终态。 */
static void StartValidationOrFault(void)
{
    application_stage =
        HasActiveRecord() ? APPLICATION_STAGE_VALIDATE_START : APPLICATION_STAGE_FAILED;
}

firmware_status_t Application_Configure(const application_dependencies_t *dependencies)
{
    const package_source_t *source;
    const update_request_store_t *store;

    if (dependencies == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    source = dependencies->package_source;
    store  = dependencies->update_request_store;
    /* 顶层状态机只接收可完整执行更新、清理、复位和启动路径的依赖组合。 */
    if ((dependencies->boot_control == NULL) || (dependencies->update == NULL) ||
        (dependencies->validation == NULL) || (dependencies->launch == NULL) ||
        (dependencies->update_request_service == NULL) || (source == NULL) ||
        (source->is_media_present == NULL) || (source->mount == NULL) ||
        (source->unmount == NULL) || (store == NULL) || (store->load_raw == NULL) ||
        (store->clear == NULL) || (dependencies->system_reset == NULL) ||
        (dependencies->system_reset->request == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if ((application_configured != 0) || (application_initialized != 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /* 这里只复制借用指针；Composition 必须让所有依赖对象保持静态生命周期。 */
    application_dependencies = *dependencies;
    application_configured   = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Init(void)
{
    firmware_status_t status;

    if ((application_initialized != 0) || (application_configured == 0))
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    /* Active Record 是现有 Runtime 获得校验和启动资格的唯一持久化依据。 */
    status = BootControlService_LoadActive(application_dependencies.boot_control,
                                           &application_active_record);
    /* 无有效记录是允许进入恢复更新的启动状态，底层 I/O 错误则必须立即失败。 */
    if (!FirmwareStatus_IsOk(status) && (status != FIRMWARE_STATUS_INVALID_STATE) &&
        (status != FIRMWARE_STATUS_OUT_OF_RANGE) && (status != FIRMWARE_STATUS_NOT_FOUND))
    {
        application_stage = APPLICATION_STAGE_FAILED;
        return status;
    }
    /* 每次 Boot 都重建易失编排状态，不沿用上一轮未完成流程的内存标志。 */
    application_has_active_record      = FirmwareStatus_IsOk(status) ? 1 : 0;
    application_media_mounted          = 0;
    application_unmount_attempts       = 0U;
    application_stale_request          = 0;
    application_reset_after_cleanup    = 0;
    application_therapy_file_open      = 0;
    application_therapy_close_attempts = 0U;
    application_request_raw_size       = 0U;
    application_stage                  = APPLICATION_STAGE_STARTUP;
    application_stage_logged           = 0;
    application_initialized            = 1;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Application_Process(void)
{
    firmware_status_t status;

    if (application_initialized == 0)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    LogStageEntry();

    switch (application_stage)
    {
        case APPLICATION_STAGE_STARTUP:
            /* Active Record 加载完成后，每次 Boot 都先检查是否存在受信更新请求。 */
            application_stage = APPLICATION_STAGE_UPDATE_CHECK;
            break;

        case APPLICATION_STAGE_UPDATE_CHECK:
        {
            int present = 0;

            /* 介质查询失败等同于本轮无可用更新，但不能绕过已有 Runtime 校验。 */
            status = application_dependencies.package_source->is_media_present(
                application_dependencies.package_source->context, &present);
            if (!FirmwareStatus_IsOk(status))
            {
                StartValidationOrFault();
            }
            else if (present == 0)
            {
                StartValidationOrFault();
            }
            else
            {
                application_stage = APPLICATION_STAGE_MOUNT;
            }
            break;
        }

        case APPLICATION_STAGE_MOUNT:
            /* mount 成功后由 Application 持有 Volume，所有退出路径都必须先卸载。 */
            status = application_dependencies.package_source->mount(
                application_dependencies.package_source->context);
            if (FirmwareStatus_IsOk(status))
            {
                application_media_mounted = 1;
                application_stage         = APPLICATION_STAGE_REQUEST_LOAD;
            }
            else
            {
                ContinueCurrentRuntime();
            }
            break;

        case APPLICATION_STAGE_REQUEST_LOAD:
            /* 先加载有界原始字节，信任和 schema 决策统一留给请求解析 Service。 */
            application_request_raw_size = 0U;
            status                       = application_dependencies.update_request_store->load_raw(
                application_dependencies.update_request_store->context, application_request_raw,
                sizeof(application_request_raw), &application_request_raw_size);
            if (status == FIRMWARE_STATUS_NOT_FOUND)
            {
                LOG_WARN("app", "trusted request not found: status=%d", (int) status);
                ContinueCurrentRuntime();
            }
            else if (!FirmwareStatus_IsOk(status))
            {
                LOG_WARN("app", "trusted request load failed: status=%d", (int) status);
                ContinueCurrentRuntime();
            }
            else
            {
                application_stage = APPLICATION_STAGE_PREPARE_START;
            }
            break;

        case APPLICATION_STAGE_PREPARE_START:
            /* 严格解析请求后再启动 Prepare；此阶段尚不会修改 Runtime。 */
            status = UpdateRequestService_ParseAndValidate(
                application_dependencies.update_request_service, application_request_raw,
                application_request_raw_size, &application_request);
            if (!FirmwareStatus_IsOk(status))
            {
                ContinueCurrentRuntime();
            }
            else
            {
                if (application_request.component_mask == 0U)
                {
                    /* Legacy in-process V1 callers select the historical APP+GUI pair. */
                    application_request.component_mask =
                        UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI;
                }
                status = UpdateService_PrepareStart(application_dependencies.update,
                                                    &application_request);
                if (FirmwareStatus_IsOk(status))
                {
                    application_stage = APPLICATION_STAGE_PREPARE_PROCESS;
                }
                else
                {
                    ContinueCurrentRuntime();
                }
            }
            break;

        case APPLICATION_STAGE_PREPARE_PROCESS:
            /* 单次只推进一个有界 Service 步骤，保持主循环可响应。 */
            UpdateService_Process(application_dependencies.update);
            if (UpdateService_GetState(application_dependencies.update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_stage = APPLICATION_STAGE_POLICY;
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                ContinueCurrentRuntime();
            }
            break;

        case APPLICATION_STAGE_POLICY:
        {
            const validated_manifest_t *manifest =
                UpdateService_GetManifest(application_dependencies.update);
            uint32_t host_mask =
                application_request.component_mask & (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI);

            /* 只有完成请求、Manifest 和源文件绑定校验的对象才能参与版本决策。 */
            if (manifest == NULL)
            {
                ContinueCurrentRuntime();
                break;
            }
            /* 包要求的最低 Bootloader 版本高于当前版本时禁止安装。 */
            if (VersionPolicy_Compare(&application_dependencies.bootloader_version,
                                      &manifest->minimum_bootloader_version) < 0)
            {
                ContinueCurrentRuntime();
                break;
            }
            if (ComponentSelected(UPDATE_COMPONENT_THERAPY) && !TherapyDependenciesReady())
            {
                ContinueCurrentRuntime();
                break;
            }
            /* 重复请求先验证现有 Runtime；仅在现有内容损坏时重新安装同一包。 */
            application_stale_request = IsSamePackage(manifest);
            if (application_stale_request != 0)
            {
                application_stage = APPLICATION_STAGE_STALE_VALIDATE_START;
            }
            else if ((host_mask != 0U) && HasActiveRecord() &&
                     !VersionPolicy_IsUpgrade(&application_active_record.release_version,
                                              &manifest->release_version) &&
                     !((VersionPolicy_Compare(&application_active_record.release_version,
                                              &manifest->release_version) == 0) &&
                       IsSameManifestIdentity(manifest)))
            {
                /* 不同包必须严格升级，禁止降级或相同 Release Version 的替换。 */
                ContinueCurrentRuntime();
            }
            else if ((host_mask != 0U) && !HasActiveRecord() &&
                     (host_mask != (UPDATE_COMPONENT_APP | UPDATE_COMPONENT_GUI)))
            {
                /* 无 Active Record 时，单独 APP 或 GUI 无法构造可启动的完整 Runtime。 */
                ContinueCurrentRuntime();
            }
            else if (ComponentSelected(UPDATE_COMPONENT_THERAPY) && HasTherapyRecord() &&
                     (VersionPolicy_Compare(&manifest->release_version,
                                            &application_active_record.therapy_version) < 0))
            {
                /* Therapy 版本独立持久化，只拒绝低版本升级。 */
                ContinueCurrentRuntime();
            }
            else if ((host_mask == 0U) && !HasActiveRecord())
            {
                /* therapy-only 不能替代承载启动所需的 APP/GUI Active Record。 */
                ContinueCurrentRuntime();
            }
            else if (host_mask == 0U)
            {
                PrepareTherapyOnlyCandidate(manifest);
                application_stage = APPLICATION_STAGE_THERAPY_OPEN;
            }
            else
            {
                application_stage = APPLICATION_STAGE_INSTALL_START;
            }
            break;
        }

        case APPLICATION_STAGE_STALE_VALIDATE_START:
            /* 重复请求不直接擦写 Flash；先证明当前 Active Runtime 是否仍然完整。 */
            status = ActiveValidationService_Start(application_dependencies.validation,
                                                   &application_active_record);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_STALE_VALIDATE_PROCESS;
            }
            else
            {
                application_stage = APPLICATION_STAGE_INSTALL_START;
            }
            break;

        case APPLICATION_STAGE_STALE_VALIDATE_PROCESS:
            /* 校验成功即可消费重复请求；失败则把同一受信包作为恢复源重新安装。 */
            ActiveValidationService_Process(application_dependencies.validation);
            if (ActiveValidationService_GetState(application_dependencies.validation) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_reset_after_cleanup = 0;
                application_stage               = APPLICATION_STAGE_CLEANUP;
            }
            else if (ActiveValidationService_GetState(application_dependencies.validation) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                application_stage = APPLICATION_STAGE_INSTALL_START;
            }
            break;

        case APPLICATION_STAGE_INSTALL_START:
            /* 版本策略接受后才允许 Update Service 进入可能擦除 Runtime 的阶段。 */
            status = UpdateService_InstallStartWithRecord(
                application_dependencies.update,
                HasActiveRecord() ? &application_active_record : NULL);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_INSTALL_PROCESS;
            }
            else
            {
                ContinueCurrentRuntime();
            }
            break;

        case APPLICATION_STAGE_INSTALL_PROCESS:
            /* 安装成功仅生成候选记录，在 Boot Control 原子提交前不得视为 Active。 */
            UpdateService_Process(application_dependencies.update);
            if (UpdateService_GetState(application_dependencies.update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                const boot_active_record_t *candidate =
                    UpdateService_GetCandidate(application_dependencies.update);
                if (candidate == NULL)
                {
                    FailClosed();
                }
                else
                {
                    application_candidate_record    = *candidate;
                    application_reset_after_cleanup = 1;
                    application_stage               = ComponentSelected(UPDATE_COMPONENT_THERAPY)
                                                          ? APPLICATION_STAGE_THERAPY_OPEN
                                                          : APPLICATION_STAGE_COMMIT_START;
                }
            }
            else if (UpdateService_GetState(application_dependencies.update) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                const service_result_t *update_result =
                    UpdateService_GetResult(application_dependencies.update);
                /* 任何可能留下半写 Runtime 或异常 XIP 状态的失败都只能复位恢复。 */
                if ((UpdateService_RuntimeMayBeModified(application_dependencies.update) != 0) ||
                    ((update_result != NULL) && (update_result->error == BOOT_ERROR_XIP_SETUP)))
                {
                    /* Runtime 可能已损坏，或 QSPI 仍处于无法安全复用的状态；清理后
                     * 复位，让下一轮从 trusted request 重新走恢复安装。 */
                    BeginUnmount(APPLICATION_STAGE_RECOVERY_RESET);
                }
                else
                {
                    ContinueCurrentRuntime();
                }
            }
            break;

        case APPLICATION_STAGE_THERAPY_OPEN:
            status = application_dependencies.package_source->open(
                application_dependencies.package_source->context, PACKAGE_FILE_THERAPY_APP);
            if (!FirmwareStatus_IsOk(status))
            {
                if (UpdateService_RuntimeMayBeModified(application_dependencies.update) != 0)
                {
                    BeginUnmount(APPLICATION_STAGE_RECOVERY_RESET);
                }
                else
                {
                    ContinueCurrentRuntime();
                }
            }
            else
            {
                application_therapy_file_open = 1;
                application_stage             = APPLICATION_STAGE_THERAPY_START;
            }
            break;

        case APPLICATION_STAGE_THERAPY_START:
        {
            const validated_manifest_t *manifest =
                UpdateService_GetManifest(application_dependencies.update);
            secondary_mcu_update_request_t request = application_dependencies.secondary_mcu_target;

            if (manifest == NULL)
            {
                application_after_therapy_close = APPLICATION_STAGE_RECOVERY_RESET;
                application_stage               = APPLICATION_STAGE_THERAPY_CLOSE;
                break;
            }
            request.image_size_bytes = manifest->therapy.size_bytes;
            memcpy(request.sha256, manifest->therapy.sha256, sizeof(request.sha256));
            status = SecondaryMcuUpdateService_Start(application_dependencies.secondary_mcu_update,
                                                     &request);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_THERAPY_PROCESS;
            }
            else
            {
                application_after_therapy_close =
                    (UpdateService_RuntimeMayBeModified(application_dependencies.update) != 0)
                        ? APPLICATION_STAGE_RECOVERY_RESET
                        : APPLICATION_STAGE_VALIDATE_START;
                application_stage = APPLICATION_STAGE_THERAPY_CLOSE;
            }
            break;
        }

        case APPLICATION_STAGE_THERAPY_PROCESS:
            SecondaryMcuUpdateService_Process(application_dependencies.secondary_mcu_update);
            if (SecondaryMcuUpdateService_GetState(application_dependencies.secondary_mcu_update) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                const validated_manifest_t *manifest =
                    UpdateService_GetManifest(application_dependencies.update);

                if (manifest == NULL)
                {
                    application_after_therapy_close = APPLICATION_STAGE_RECOVERY_RESET;
                }
                else
                {
                    application_candidate_record.format_version  = BOOT_ACTIVE_RECORD_FORMAT_V3;
                    application_candidate_record.component_mask  = UPDATE_COMPONENT_ALL;
                    application_candidate_record.therapy_size    = manifest->therapy.size_bytes;
                    application_candidate_record.therapy_version = manifest->release_version;
                    memcpy(application_candidate_record.therapy_sha256, manifest->therapy.sha256,
                           sizeof(application_candidate_record.therapy_sha256));
                    application_reset_after_cleanup = 1;
                    application_after_therapy_close = APPLICATION_STAGE_COMMIT_START;
                }
                application_stage = APPLICATION_STAGE_THERAPY_CLOSE;
            }
            else if (SecondaryMcuUpdateService_GetState(
                         application_dependencies.secondary_mcu_update) == SERVICE_RUN_STATE_FAILED)
            {
                application_after_therapy_close =
                    (SecondaryMcuUpdateService_TargetMayBeModified(
                         application_dependencies.secondary_mcu_update) != 0) ||
                            (UpdateService_RuntimeMayBeModified(application_dependencies.update) !=
                             0)
                        ? APPLICATION_STAGE_RECOVERY_RESET
                        : APPLICATION_STAGE_VALIDATE_START;
                application_stage = APPLICATION_STAGE_THERAPY_CLOSE;
            }
            break;

        case APPLICATION_STAGE_THERAPY_CLOSE:
            status = application_dependencies.package_source->close(
                application_dependencies.package_source->context);
            if (FirmwareStatus_IsOk(status))
            {
                application_therapy_file_open      = 0;
                application_therapy_close_attempts = 0U;
                FinishTherapyClose();
            }
            else
            {
                ++application_therapy_close_attempts;
                if (application_therapy_close_attempts >= APPLICATION_UNMOUNT_RETRY_LIMIT)
                {
                    FailClosed();
                }
            }
            break;

        case APPLICATION_STAGE_COMMIT_START:
            /* 所有选中组件完成验证后，才开始发布候选 Active Record。 */
            status = BootControlService_CommitActiveStart(application_dependencies.boot_control,
                                                          &application_candidate_record);
            if (FirmwareStatus_IsOk(status))
            {
                application_stage = APPLICATION_STAGE_COMMIT_PROCESS;
            }
            else
            {
                BeginUnmount((ComponentSelected(UPDATE_COMPONENT_THERAPY) &&
                              SecondaryMcuUpdateService_TargetMayBeModified(
                                  application_dependencies.secondary_mcu_update)) ||
                             (UpdateService_RuntimeMayBeModified(application_dependencies.update) !=
                              0)
                                 ? APPLICATION_STAGE_RECOVERY_RESET
                                 : APPLICATION_STAGE_FAILED);
            }
            break;

        case APPLICATION_STAGE_COMMIT_PROCESS:
            /* EEPROM 原子提交成功是新 Runtime 获得 Active 身份的唯一时刻。 */
            BootControlService_Process(application_dependencies.boot_control);
            if (BootControlService_GetState(application_dependencies.boot_control) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_active_record     = application_candidate_record;
                application_has_active_record = 1;
                application_stage             = APPLICATION_STAGE_CLEANUP;
            }
            else if (BootControlService_GetState(application_dependencies.boot_control) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                BeginUnmount((ComponentSelected(UPDATE_COMPONENT_THERAPY) &&
                              SecondaryMcuUpdateService_TargetMayBeModified(
                                  application_dependencies.secondary_mcu_update)) ||
                             (UpdateService_RuntimeMayBeModified(application_dependencies.update) !=
                              0)
                                 ? APPLICATION_STAGE_RECOVERY_RESET
                                 : APPLICATION_STAGE_FAILED);
            }
            break;

        case APPLICATION_STAGE_CLEANUP:
            /* 到达 Cleanup 表示请求已被成功处理；清除失败只保留告警供下轮幂等处理。 */
            status = application_dependencies.update_request_store->clear(
                application_dependencies.update_request_store->context);
            if (!FirmwareStatus_IsOk(status))
            {
                LOG_WARN("app", "request clear failed: status=%d", (int) status);
            }
            BeginUnmount(application_reset_after_cleanup != 0 ? APPLICATION_STAGE_RESET
                                                              : APPLICATION_STAGE_VALIDATE_START);
            break;

        case APPLICATION_STAGE_UNMOUNT:
            /* 只有成功卸载发布卷，后续 Runtime 校验、跳转或复位才允许继续。 */
            status = application_dependencies.package_source->unmount(
                application_dependencies.package_source->context);
            if (FirmwareStatus_IsOk(status))
            {
                /* 只有 Adapter 确认卸载成功，Application 才能释放 mounted 所有权。 */
                application_media_mounted    = 0;
                application_unmount_attempts = 0U;
                application_stage            = application_after_unmount;
            }
            else
            {
                ++application_unmount_attempts;
                LOG_WARN("app", "media unmount retry %lu/%u failed: status=%d",
                         (unsigned long) application_unmount_attempts,
                         (unsigned) APPLICATION_UNMOUNT_RETRY_LIMIT, (int) status);
                if (application_unmount_attempts >= APPLICATION_UNMOUNT_RETRY_LIMIT)
                {
                    /* 实际卷状态仍为 mounted，继续启动或跳转不安全，必须停在 FAULT。 */
                    LOG_ERROR("app", "media remains mounted after cleanup retries");
                    FailClosed();
                }
            }
            break;

        case APPLICATION_STAGE_VALIDATE_START:
            /* 无 Active Record 时没有可授权启动的镜像，禁止退化为直接跳转。 */
            if (!HasActiveRecord())
            {
                FailClosed();
                break;
            }
            status            = ActiveValidationService_Start(application_dependencies.validation,
                                                              &application_active_record);
            application_stage = FirmwareStatus_IsOk(status) ? APPLICATION_STAGE_VALIDATE_PROCESS
                                                            : APPLICATION_STAGE_FAILED;
            break;

        case APPLICATION_STAGE_VALIDATE_PROCESS:
            /* APP 向量、APP 摘要和 GUI 摘要全部通过后才允许进入 Launch。 */
            ActiveValidationService_Process(application_dependencies.validation);
            if (ActiveValidationService_GetState(application_dependencies.validation) ==
                SERVICE_RUN_STATE_SUCCEEDED)
            {
                application_stage = APPLICATION_STAGE_LAUNCH;
            }
            else if (ActiveValidationService_GetState(application_dependencies.validation) ==
                     SERVICE_RUN_STATE_FAILED)
            {
                FailClosed();
            }
            break;

        case APPLICATION_STAGE_LAUNCH:
            /* 成功交接不会返回；任何返回错误都意味着本轮必须 fail-closed。 */
            status =
                LaunchService_Execute(application_dependencies.launch, &application_active_record);
            if (!FirmwareStatus_IsOk(status))
            {
                FailClosed();
            }
            break;

        case APPLICATION_STAGE_RESET:
            /* 新 Active Record 已提交且请求清理已尝试，复位后按正常路径验证并交接。 */
            application_dependencies.system_reset->request(
                application_dependencies.system_reset->context);
            break;

        case APPLICATION_STAGE_RECOVERY_RESET:
            /* 安装已修改 Runtime 或 XIP 退出失败时，保留 request 并通过下一轮启动
             * 重新执行 Prepare/Install；本轮绝不尝试启动可能不完整的镜像。 */
            LOG_WARN("app", "resetting into update recovery");
            application_dependencies.system_reset->request(
                application_dependencies.system_reset->context);
            break;

        case APPLICATION_STAGE_FAILED:
            /* 失败终态不再接触存储或尝试启动，等待外部看门狗或人工恢复。 */
            return FIRMWARE_STATUS_INVALID_STATE;

        default:
            /* 未知枚举值按内部状态损坏处理，绝不猜测可恢复路径。 */
            return FIRMWARE_STATUS_INVALID_STATE;
    }
    return FIRMWARE_STATUS_OK;
}
