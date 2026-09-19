set(PLATFORM_STM32H7_SOURCES
    src/platform.c
    src/platform_flash.c
    src/platform_storage.c
    src/platform_nv_storage.c
    src/platform_retained_memory.c
    src/platform_system.c
    src/platform_cpu.c
    src/platform_therapy.c
    src/platform_log.c
)

add_library(platform_stm32h7 STATIC ${PLATFORM_STM32H7_SOURCES})

target_include_directories(platform_stm32h7 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(platform_stm32h7 PUBLIC firmware_types)

target_link_libraries(platform_stm32h7 PRIVATE
    board_bsp
    firmware_crypto
    driver_at24
    driver_spi_nor
    driver_stm32_rom_boot
    stm32cubemx
    firmware_logger
)

target_link_libraries(platform_stm32h7 PRIVATE fw::segger_rtt)
