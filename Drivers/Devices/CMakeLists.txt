# Device drivers are declared in this directory so firmware targets only need
# one add_subdirectory(Drivers/Devices). Each driver remains a separate target
# and receives only shared, hardware-independent types.
# SDRAM is an application/display resource and is intentionally not linked into
# the bare-metal boot image. Keep the standalone target for host tests and
# future application targets without making it a boot dependency.
add_library(driver_sdram STATIC
    Sdram/sdram.c
)

target_include_directories(driver_sdram PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sdram
)

target_link_libraries(driver_sdram PUBLIC firmware_types)

# AT24 owns EEPROM protocol and write-cycle state without HAL dependencies.
add_library(driver_at24 STATIC
    At24/at24.c
)

target_include_directories(driver_at24 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/At24
)

target_link_libraries(driver_at24 PUBLIC firmware_types)

# SPI-NOR does not inherit CubeMX, HAL, BSP, or middleware include paths. The
# board-specific QSPI binding is supplied through spi_nor_port_t by the BSP.
add_library(driver_spi_nor STATIC
    SpiNor/spi_nor.c
)

target_include_directories(driver_spi_nor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SpiNor
)

target_link_libraries(driver_spi_nor PUBLIC firmware_types)

# STM32 ROM Bootloader 只实现 AN3155 UART 协议；UART、BOOT0 和 NRST 的
# 具体板级绑定由 BSP/Adapter 注入，因此该目标不依赖 HAL 或 CubeMX。
add_library(driver_stm32_rom_boot STATIC
    Stm32RomBoot/stm32_rom_boot.c
)

target_include_directories(driver_stm32_rom_boot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Stm32RomBoot
)

target_link_libraries(driver_stm32_rom_boot PUBLIC firmware_types)
