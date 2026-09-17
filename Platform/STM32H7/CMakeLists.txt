set(PLATFORM_STM32H7_SOURCES
    src/platform.c
    src/platform_boot_control.c
    src/platform_flash.c
    src/platform_storage.c
    src/platform_system.c
    src/platform_therapy.c
    src/platform_application.c
    src/platform_log.c
    src/platform_time.c
)

add_library(platform_stm32h7 STATIC ${PLATFORM_STM32H7_SOURCES})

target_include_directories(platform_stm32h7 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(platform_stm32h7 PUBLIC firmware_types)

target_link_libraries(platform_stm32h7 PRIVATE
    board_bsp
    firmware_crypto
    device_drivers
    stm32cubemx
    firmware_logger
)

target_link_libraries(platform_stm32h7 PRIVATE fw::segger_rtt)
