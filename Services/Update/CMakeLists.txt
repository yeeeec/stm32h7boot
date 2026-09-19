add_library(update STATIC
    src/update_manifest.c
    src/update_request.c
    src/version_policy.c
    src/image_installer.c
    src/current_manager.c
    src/update_manager.c
    src/recovery_manager.c
    src/update_journal.c
    src/boot_mailbox.c
)

target_include_directories(update PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(update
    PUBLIC firmware_types
    PRIVATE firmware_crypto platform_stm32h7
)
