set(FIRMWARE_APPLICATION_SOURCES
    src/application.c
    src/boot_manager.c
)

add_library(firmware_application STATIC ${FIRMWARE_APPLICATION_SOURCES})

target_include_directories(firmware_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_application PUBLIC
    firmware_types
    update
    platform_stm32h7
    firmware_crypto
    firmware_logger
)
