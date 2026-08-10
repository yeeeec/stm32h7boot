add_library(firmware_adapters STATIC
    # 按硬件类别列出适配实现，将底层驱动绑定到 Firmware 接口。
    src/at24_boot_control_adapter.c
    src/cortex_m_application_jump_adapter.c
    src/fatfs_package_source_adapter.c
    src/fatfs_update_request_store_adapter.c
    src/spi_nor_block_adapter.c
    src/stm32_rom_boot_programmer_adapter.c
    src/stm32_clock_adapter.c
    src/stm32_qspi_xip_adapter.c
    src/stm32_system_reset_adapter.c
    src/stm32_watchdog_adapter.c
    src/uart_log_adapter.c
)

target_include_directories(firmware_adapters PUBLIC
    # 对外公开适配器头文件，供 Composition 注入服务依赖。
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_adapters
    # 适配器依赖稳定的 Firmware 接口；具体实现链接到 STM32、BSP 和设备驱动。
    PUBLIC firmware_interfaces
    PRIVATE
        stm32cubemx
        platform_stm32h7
        board_bsp
        driver_at24
        driver_spi_nor
        driver_stm32_rom_boot
        firmware_logger
)
