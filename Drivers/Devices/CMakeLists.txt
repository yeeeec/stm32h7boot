add_library(driver_spi_nor STATIC SpiNor/spi_nor.c)
target_include_directories(driver_spi_nor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SpiNor
)
target_link_libraries(driver_spi_nor PUBLIC firmware_types)

add_library(driver_stm32_rom_boot STATIC Stm32RomBoot/stm32_rom_boot.c
Stm32RomBoot/stm32_rom_boot_devices.c)
target_include_directories(driver_stm32_rom_boot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Stm32RomBoot
)
target_link_libraries(driver_stm32_rom_boot PUBLIC firmware_types)
