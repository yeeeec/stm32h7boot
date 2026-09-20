add_library(boot_service STATIC
    src/boot_mailbox.c
    src/runtime_image.c
)

target_include_directories(boot_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(boot_service
    PUBLIC firmware_types
    PRIVATE platform_stm32h7
)
