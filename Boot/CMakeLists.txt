add_subdirectory(Logger)
add_subdirectory(Platform)

set(BOOT_FRAMEWORK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_crc32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_handoff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_info.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_udisk_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_simple_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_simple_jump.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_simple_manifest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_simple_upgrade.c
)

add_library(boot_framework STATIC)
target_sources(boot_framework PRIVATE ${BOOT_FRAMEWORK_SOURCES})
target_include_directories(boot_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)
target_link_libraries(boot_framework PUBLIC stm32cubemx boot_logger boot_platform)
