# Device drivers are declared in this directory so firmware targets only need
# one add_subdirectory(Drivers/Devices). Each driver remains a separate target
# and receives only shared, hardware-independent types.
add_library(driver_sdram STATIC
    Sdram/sdram.c
)

target_include_directories(driver_sdram PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sdram/
)

target_link_libraries(driver_sdram PUBLIC firmware_types)

# SPI-NOR does not inherit CubeMX, HAL, BSP, or middleware include paths. The
# board-specific QSPI binding is supplied through spi_nor_port_t by the BSP.
add_library(driver_spi_nor STATIC
    SpiNor/spi_nor.c
)

target_include_directories(driver_spi_nor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SpiNor
)

target_link_libraries(driver_spi_nor PUBLIC firmware_types)
