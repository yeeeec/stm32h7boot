set(PLATFORM_STM32H7_SOURCES
    src/platform.c
    src/platform_cache.c
    src/platform_memory.c
    src/platform_reset.c
    src/platform_reset_reason.c
    src/platform_time.c
    src/platform_log.c
    src/platform_parameters.c
)

add_library(platform_stm32h7 STATIC ${PLATFORM_STM32H7_SOURCES})

target_include_directories(platform_stm32h7 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(platform_stm32h7 PUBLIC firmware_types)

target_link_libraries(platform_stm32h7 PRIVATE
    board_bsp
    cubemx_generated_stm32h7boot
    firmware_storage_common
)

target_link_libraries(platform_stm32h7 PRIVATE fw::segger_rtt)
