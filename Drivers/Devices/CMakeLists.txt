set(DEVICE_DRIVERS_SOURCES
    At24/at24.c
    SpiNor/spi_nor.c
    Stm32RomBoot/stm32h743_rom_boot.c
)

add_library(device_drivers STATIC ${DEVICE_DRIVERS_SOURCES})

target_include_directories(device_drivers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/At24
    ${CMAKE_CURRENT_SOURCE_DIR}/SpiNor
    ${CMAKE_CURRENT_SOURCE_DIR}/Stm32RomBoot
)

target_include_directories(device_drivers PRIVATE
)