add_library(update_service STATIC
    src/package_reader.c
    src/update_manifest.c
    src/version_policy.c
    src/image_installer.c
    src/current_store.c
    src/update_journal.c
    src/update_service.c
)

target_include_directories(update_service
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(update_service
    PUBLIC firmware_types
    PRIVATE platform_stm32h7 firmware_crypto firmware_checksum
)
