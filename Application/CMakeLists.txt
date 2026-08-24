add_library(firmware_application STATIC
    src/application.c
)

target_include_directories(firmware_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_application PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/internal
)

target_link_libraries(firmware_application
    PUBLIC firmware_types
    PRIVATE firmware_services firmware_interfaces firmware_logger
)

target_compile_definitions(firmware_application PRIVATE
    $<$<CONFIG:Debug>:BOOT_SKIP_ACTIVE_HASH_VALIDATION=1>
)