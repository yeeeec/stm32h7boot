add_library(firmware_adapters STATIC
    src/at24_boot_control_adapter.c
    src/cortex_m_application_jump_adapter.c
    src/fatfs_package_source_adapter.c
    src/spi_nor_block_adapter.c
    src/stm32_clock_adapter.c
    src/stm32_qspi_xip_adapter.c
    src/stm32_system_reset_adapter.c
    src/stm32_watchdog_adapter.c
    src/uart_log_adapter.c
)

target_include_directories(firmware_adapters PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_adapters
    PUBLIC firmware_interfaces
    PRIVATE
        stm32cubemx
        platform_stm32h7
        board_bsp
        driver_at24
        driver_spi_nor
        firmware_logger
)
