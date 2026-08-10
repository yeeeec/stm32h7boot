/**
 * @file composition.c
 * @brief 生产 Firmware 镜像的静态依赖图。
 *
 * Composition Root 持有所有 Adapter 和 Service 实例，将 BSP 设备转换为面向
 * Architecture 的接口，并向 Application 提供最终依赖图。本模块只包含 wiring
 * 和不变的 Board Policy；Boot、Update、Validation、Launch 决策仍由所属层负责。
 *
 * 只有所有依赖成功初始化后，才通过 Application_Configure() 发布依赖图。初始化
 * 没有 rollback，因此任何失败都是当前 Boot 的终态，并且必须保持
 * Composition_IsInitialized() 为 false。
 */
#include "composition/composition.h"

#include <stddef.h>

#include "adapters/at24_boot_control_adapter.h"
#include "adapters/cortex_m_application_jump_adapter.h"
#include "adapters/fatfs_package_source_adapter.h"
#include "adapters/fatfs_update_request_store_adapter.h"
#include "adapters/spi_nor_block_adapter.h"
#include "adapters/stm32_clock_adapter.h"
#include "adapters/stm32_qspi_xip_adapter.h"
#include "adapters/stm32_system_reset_adapter.h"
#include "adapters/uart_log_adapter.h"
#include "application/application.h"
#include "application/application_config.h"
#include "bsp/bsp_eeprom.h"
#include "bsp/bsp_external_flash.h"
#include "checksum/crc32_iso_hdlc.h"
#include "crypto/sha256.h"
#include "firmware/async_block_device.h"
#include "firmware/hash.h"
#include "logging.h"
#include "logging_setup.h"
#include "quadspi.h"
#include "services/capability/boot_control_service.h"
#include "services/capability/boot_control_service_api.h"
#include "services/capability/manifest_service.h"
#include "services/capability/update_request_service.h"
#include "services/capability/vector_validation.h"
#include "services/use_case/active_validation_service.h"
#include "services/use_case/launch_service.h"
#include "services/use_case/update_service.h"

/** Update 与 Active Validation 单步复用的 I/O 缓冲区容量，单位为字节。 */
#define SERVICE_IO_BUFFER_SIZE 4096U

/** 为日志系统提供单调时钟的 STM32 Platform Adapter。 */
static stm32_clock_adapter_t clock_adapter;
/** 把格式化日志写入调试 UART 的 Adapter。 */
static uart_log_adapter_t log_adapter;
/** 将 BSP 外部 NOR Flash 转换为异步块设备接口的 Adapter。 */
static spi_nor_block_adapter_t external_flash_adapter;
/** 将 BSP AT24 EEPROM 转换为 Boot Control 存储接口的 Adapter。 */
static at24_boot_control_adapter_t eeprom_adapter;
/** 负责 Active Record A/B 选择和原子提交的 Service。 */
static boot_control_service_t boot_control_service;
/** Manifest、Trusted Request 与 Update Prepare 串行复用的 SHA-256 Context。 */
static sha256_context_t manifest_hash_context;
/** Active Runtime 校验独占的 SHA-256 Context。 */
static sha256_context_t active_validation_hash_context;
/** 严格解析并验证发布 Manifest 的 Service。 */
static manifest_service_t manifest_service;
/** 严格解析 Trusted Request 并验证 Manifest 绑定的 Service。 */
static update_request_service_t update_request_service;
/** Package Source 与 Request Store 共同借用的 FatFs 发布卷上下文。 */
static fatfs_release_volume_context_t release_volume;
/** 向 Service 暴露固定发布文件访问能力的 Package Source Adapter。 */
static fatfs_package_source_adapter_t package_source_adapter;
/** 向 Application 暴露 Trusted Request 加载和清除能力的 Adapter。 */
static fatfs_update_request_store_adapter_t request_store_adapter;
/** 管理 QSPI indirect 与 memory-mapped 模式切换的 XIP Adapter。 */
static stm32_qspi_xip_adapter_t xip_adapter;
/** 执行 Cortex-M VTOR、MSP 和 Reset Handler 交接的 Adapter。 */
static cortex_m_application_jump_adapter_t jump_adapter;
/** 执行不可返回全系统复位请求的 Adapter。 */
static stm32_system_reset_adapter_t system_reset_adapter;
/** 为 Boot Control Record 提供 CRC-32/ISO-HDLC 的 Provider。 */
static crc32_iso_hdlc_t crc32_provider;
/** 增量校验当前 Active APP/GUI Runtime 的 Service。 */
static active_validation_service_t active_validation_service;
/** 完成 XIP 建立和最终 APP 跳转的 Service。 */
static launch_service_t launch_service;
/** 准备发布包并安装固定 APP/GUI Runtime 的 Service。 */
static update_service_t update_service;

/*
 * Service 工作区具有静态生命周期。放置到 D2 可将大 Buffer 移出主 SRAM，
 * Cache-line 对齐则满足需要 DMA/Cache maintenance 的 Consumer Contract。
 */
/** Manifest 原始文档及解析过程使用的固定容量工作区。 */
static uint8_t manifest_buffer[UPDATE_SERVICE_MANIFEST_MAX_SIZE]
    __attribute__((section(".ram_d2"), aligned(32)));
/** Update 与 Active Validation 分时复用的块 I/O 工作区。 */
static uint8_t service_io_buffer[SERVICE_IO_BUFFER_SIZE]
    __attribute__((section(".ram_d2"), aligned(32)));

/*
 * 该表是可执行镜像的策略，不是对所有可写 RAM 的探测。Vector Validation
 * 必须拒绝落在这些区域之外的初始 Stack Pointer。
 */
static const memory_region_t application_sram_regions[] = {
    /* Cortex-M DTCM，起始地址 0x20000000，容量 128 KiB。 */
    {0x20000000UL, 128UL * 1024UL},
    /* AXI SRAM，起始地址 0x24000000，容量 512 KiB。 */
    {0x24000000UL, 512UL * 1024UL},
    /* D2 SRAM1/2/3 连续区域，起始地址 0x30000000，总容量 288 KiB。 */
    {0x30000000UL, 288UL * 1024UL},
    /* D3 SRAM4，起始地址 0x38000000，容量 64 KiB。 */
    {0x38000000UL, 64UL * 1024UL},
};
/** 完整依赖图已经发布给 Application 的就绪标志。 */
static int composition_initialized;

/**
 * @brief 重置接口绑定的 SHA-256 Context，开始一次新摘要计算。
 *
 * @param[in,out] context 指向 Hash 接口绑定的 sha256_context_t，不允许为 NULL。
 * @return Sha256_Reset() 返回的状态。
 */
static firmware_status_t ManifestHashReset(void *context)
{
    return Sha256_Reset((sha256_context_t *) context);
}

/**
 * @brief 把一段连续字节加入接口绑定的 SHA-256 摘要。
 *
 * @param[in,out] context 指向 Hash 接口绑定的 sha256_context_t，不允许为 NULL。
 * @param[in] data 本次加入摘要的数据起始地址；当 @p size 为零时允许为 NULL。
 * @param[in] size @p data 中同步消费的字节数，允许为零且函数不会保留该 Buffer。
 * @return Sha256_Update() 返回的状态。
 */
static firmware_status_t ManifestHashUpdate(void *context, const void *data, size_t size)
{
    return Sha256_Update((sha256_context_t *) context, data, size);
}

/**
 * @brief 完成接口绑定的 SHA-256 计算并输出固定长度摘要。
 *
 * @param[in] context 指向 Hash 接口绑定的 sha256_context_t，不允许为 NULL。
 * @param[out] digest 接收 FIRMWARE_SHA256_DIGEST_SIZE 字节摘要的非 NULL 缓冲区。
 * @return Sha256_Finish() 返回的状态。
 */
static firmware_status_t ManifestHashFinish(void *context,
                                            uint8_t digest[FIRMWARE_SHA256_DIGEST_SIZE])
{
    return Sha256_Finish((sha256_context_t *) context, digest);
}

/** Manifest、Request 与 Update Service 按 Application 编排顺序共享的 Hash 接口。 */
static hash_provider_t manifest_hash_interface = {
    &manifest_hash_context,
    ManifestHashReset,
    ManifestHashUpdate,
    ManifestHashFinish,
};

/**
 * Active Runtime Validation 使用独立的可变 Hash Context，避免其 Digest 生命周期
 * 破坏 Manifest/Update Request 的 Hash 状态。
 */
static hash_provider_t active_validation_hash_interface = {
    &active_validation_hash_context,
    ManifestHashReset,
    ManifestHashUpdate,
    ManifestHashFinish,
};

firmware_status_t Composition_Init(void)
{
    /* 所有局部 Dependencies 只用于初始化；Service 会复制其中的借用指针。 */
    firmware_status_t status;
    const async_block_device_t *external_flash;
    async_block_device_info_t external_flash_info;
    active_validation_service_dependencies_t validation_dependencies;
    launch_service_dependencies_t launch_dependencies;
    update_service_dependencies_t update_dependencies;

    /* 装配过程不可回滚，禁止在同一 Boot 中对已发布对象重复初始化。 */
    if (composition_initialized != 0)
    {
        LOG_WARN("composition", "initialization requested more than once");
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* 先配置 Logger，保证后续依赖失败仍可诊断。 */
    STM32ClockAdapter_Init(&clock_adapter);
    UartLogAdapter_Init(&log_adapter);

    status = Logging_Configure(UartLogAdapter_Interface(&log_adapter),
                               STM32ClockAdapter_Interface(&clock_adapter));
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    LOG_DEBUG("composition", "logger dependencies bound");

    /* 外部 Flash Adapter 是安装、校验和启动三个流程共享的唯一块设备入口。 */
    status = SpiNorBlockAdapter_Init(&external_flash_adapter, BSP_ExternalFlashDevice());
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "external flash adapter init failed: %d", (int) status);
        return status;
    }

    /* 几何参数无效时必须在任何 Service 修改 Runtime 前失败。 */
    external_flash = SpiNorBlockAdapter_AsyncInterface(&external_flash_adapter);
    status         = external_flash->get_info(external_flash->context, &external_flash_info);
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "external flash info failed: %d", (int) status);
        return status;
    }
    if ((external_flash_info.capacity_bytes == 0U) || (external_flash_info.program_size == 0U) ||
        (external_flash_info.erase_size == 0U))
    {
        LOG_ERROR("composition", "external flash geometry is invalid");
        return FIRMWARE_STATUS_INVALID_STATE;
    }

    /* Boot Control 使用独立 CRC Provider 校验 EEPROM 中的持久化记录。 */
    status = Crc32IsoHdlc_Init(&crc32_provider);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    {
        /* Manifest Service 借用共享 Hash 接口，解析期间独占其 Context。 */
        manifest_service_dependencies_t manifest_dependencies = {&manifest_hash_interface};

        status = ManifestService_Init(&manifest_service, &manifest_dependencies);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("composition", "Manifest service init failed: %d", (int) status);
            return status;
        }
    }

    {
        /* Request Service 与 Manifest Service 分时复用同一 Hash Context。 */
        update_request_service_dependencies_t request_dependencies = {&manifest_hash_interface};

        status = UpdateRequestService_Init(&update_request_service, &request_dependencies);
        if (!FirmwareStatus_IsOk(status))
        {
            LOG_ERROR("composition", "Update request service init failed: %d", (int)status);
            return status;
        }
    }

    /* EEPROM Adapter 和 CRC Provider 共同构成 Boot Control 的持久化边界。 */
    status = At24BootControlAdapter_Init(&eeprom_adapter, BSP_EepromDevice());
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "EEPROM adapter init failed: %d", (int) status);
        return status;
    }
    {
        boot_control_service_dependencies_t boot_control_dependencies = {
            At24BootControlAdapter_Interface(&eeprom_adapter),
            Crc32IsoHdlc_Interface(&crc32_provider),
        };

        status = BootControlService_Init(&boot_control_service, &boot_control_dependencies);
    }
    if (!FirmwareStatus_IsOk(status))
    {
        LOG_ERROR("composition", "boot control init failed: %d", (int) status);
        return status;
    }

    /* 两个 FatFs Adapter 共享同一发布卷，Application 统一管理挂载 Ownership。 */
    status = FatFsReleaseVolumeContext_Init(&release_volume);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = FatFsPackageSourceAdapter_Init(&package_source_adapter, &release_volume);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = FatFsUpdateRequestStoreAdapter_Init(&request_store_adapter, &release_volume);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    /* hqspi 已由 BSP 初始化；此处只建立模式切换与 Cache 控制接口。 */
    status = Stm32QspiXipAdapter_Init(&xip_adapter, &hqspi);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    /* Jump 与 Reset Adapter 封装最终不可逆的 Platform 控制权交接操作。 */
    status = CortexMApplicationJumpAdapter_Init(&jump_adapter);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }
    status = Stm32SystemResetAdapter_Init(&system_reset_adapter);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    /* Active Validation 独占 Hash Context，但与 Update 分时复用 D2 I/O Buffer。 */
    validation_dependencies.storage      = external_flash;
    validation_dependencies.hash         = &active_validation_hash_interface;
    validation_dependencies.buffer       = service_io_buffer;
    validation_dependencies.buffer_size  = sizeof(service_io_buffer);
    validation_dependencies.sram_regions = application_sram_regions;
    validation_dependencies.sram_region_count =
        sizeof(application_sram_regions) / sizeof(application_sram_regions[0]);
    status = ActiveValidationService_Init(&active_validation_service, &validation_dependencies);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    /* Launch 复用同一 Flash/XIP 对象，并以固定 SRAM 表约束向量表初始 MSP。 */
    launch_dependencies.storage          = external_flash;
    launch_dependencies.xip_controller   = Stm32QspiXipAdapter_Interface(&xip_adapter);
    launch_dependencies.application_jump = CortexMApplicationJumpAdapter_Interface(&jump_adapter);
    launch_dependencies.sram_regions     = application_sram_regions;
    launch_dependencies.sram_region_count =
        sizeof(application_sram_regions) / sizeof(application_sram_regions[0]);
    status = LaunchService_Init(&launch_service, &launch_dependencies);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    /* Update 汇合发布卷、解析、Flash、XIP、布局和工作区，是依赖最多的 Service。 */
    update_dependencies.package_source       =
        FatFsPackageSourceAdapter_Interface(&package_source_adapter);
    update_dependencies.manifest_service     = &manifest_service;
    update_dependencies.update_request_service = &update_request_service;
    update_dependencies.hash                 = &manifest_hash_interface;
    update_dependencies.clock                = STM32ClockAdapter_Interface(&clock_adapter);
    update_dependencies.storage              = external_flash;
    /*
     * 安装期间由 Update Service 独占 XIP 状态切换，并必须在首次 Runtime
     * 擦除前肯定确认已处于 indirect mode。
     */
    update_dependencies.xip_controller       =
        Stm32QspiXipAdapter_Interface(&xip_adapter);
    update_dependencies.runtime_layout       = BootRuntimeLayout_Get();
    update_dependencies.manifest_buffer      = manifest_buffer;
    update_dependencies.manifest_buffer_size = sizeof(manifest_buffer);
    update_dependencies.io_buffer            = service_io_buffer;
    update_dependencies.io_buffer_size       = sizeof(service_io_buffer);
    status = UpdateService_Init(&update_service, &update_dependencies);
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    {
        /* 最后才发布顶层依赖，确保 Application 永远看不到半初始化对象图。 */
        const application_dependencies_t application_dependencies = {
            .boot_control       = &boot_control_service,
            .update             = &update_service,
            .validation         = &active_validation_service,
            .launch             = &launch_service,
            .package_source     = FatFsPackageSourceAdapter_Interface(&package_source_adapter),
            .update_request_store =
                FatFsUpdateRequestStoreAdapter_Interface(&request_store_adapter),
            .update_request_service = &update_request_service,
            .system_reset       = Stm32SystemResetAdapter_Interface(&system_reset_adapter),
            .bootloader_version = {1U, 0U, 0U},
        };

        status = Application_Configure(&application_dependencies);
    }
    if (!FirmwareStatus_IsOk(status))
    {
        return status;
    }

    LOG_INFO("storage", "external flash ready: capacity=%lu, write=%lu, erase=%lu bytes",
             (unsigned long) external_flash_info.capacity_bytes,
             (unsigned long) external_flash_info.program_size,
             (unsigned long) external_flash_info.erase_size);

    /* 就绪标志必须是整个对象图成功初始化并被 Application 接受后的最后一次写入。 */
    composition_initialized = 1;
    LOG_INFO("composition", "service dependencies initialized");
    return FIRMWARE_STATUS_OK;
}

int Composition_IsInitialized(void)
{
    /* 失败路径从不置位，调用者不能把部分初始化的私有对象误判为可用依赖图。 */
    return composition_initialized;
}
