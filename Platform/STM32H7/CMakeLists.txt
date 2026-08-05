add_library(platform_stm32h7 STATIC
    src/platform.c
    src/platform_cache.c
    src/platform_critical.c
    src/platform_memory.c
    src/platform_reset.c
    src/platform_reset_reason.c
    src/platform_time.c
    src/platform_watchdog.c
)

target_include_directories(platform_stm32h7 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(platform_stm32h7
    PUBLIC firmware_types
    PRIVATE cubemx_generated_stm32h7boot
)
