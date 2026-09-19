add_library(driver_at24 STATIC At24/at24.c)
target_include_directories(driver_at24 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/At24
)
target_link_libraries(driver_at24 PUBLIC firmware_types)

add_library(driver_spi_nor STATIC SpiNor/spi_nor.c)
target_include_directories(driver_spi_nor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SpiNor
)
target_link_libraries(driver_spi_nor PUBLIC firmware_types)

add_library(driver_stm32_rom_boot STATIC Stm32RomBoot/stm32h743_rom_boot.c)
target_include_directories(driver_stm32_rom_boot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Stm32RomBoot
)
target_link_libraries(driver_stm32_rom_boot PUBLIC firmware_types)
