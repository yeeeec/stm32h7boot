add_subdirectory(Logger)

set(BOOT_FRAMEWORK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_ctrl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_crc32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_extflash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_jump.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_manifest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_recovery.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_upgrade.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_udisk_check.c
)

add_library(boot_framework STATIC)
target_sources(boot_framework PRIVATE ${BOOT_FRAMEWORK_SOURCES})
target_include_directories(boot_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)
target_link_libraries(boot_framework PUBLIC stm32cubemx boot_logger)
