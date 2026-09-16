set(PLATFORM_STM32H7_SOURCES
    src/platform.c
    src/platform_display.c
    src/platform_log.c
    src/platform_storage.c
    src/storage_backend_fatfs.c
    src/platform_storage_facade.c
    src/platform_flash.c
    src/platform_hash.c
    src/platform_boot_control.c
    src/platform_reset.c
    src/platform_watchdog.c
)

add_library(platform_stm32h7 STATIC ${PLATFORM_STM32H7_SOURCES})

target_include_directories(platform_stm32h7 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(platform_stm32h7 PRIVATE
    ${CMAKE_SOURCE_DIR}/FATFS/App
    ${CMAKE_SOURCE_DIR}/FATFS/Target
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FatFs/src
)

target_link_libraries(platform_stm32h7 PUBLIC firmware_types)

target_link_libraries(platform_stm32h7 PRIVATE
    board_bsp
    stm32cubemx
    firmware_storage_common
    firmware_crypto
)

target_link_libraries(platform_stm32h7 PRIVATE fw::segger_rtt)
