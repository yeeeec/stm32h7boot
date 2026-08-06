add_library(firmware_services STATIC
    src/capability/appx_validation.c
    src/capability/checked_arithmetic.c
    src/capability/boot_control_service.c
    src/capability/relocation_service.c
    src/capability/slot_policy.c
    src/capability/vector_validation.c
    src/capability/version_policy.c
    src/use_case/active_validation_service.c
    src/use_case/launch_service.c
)

target_include_directories(firmware_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_services PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/internal
)

target_link_libraries(firmware_services
    PUBLIC firmware_types
    PRIVATE firmware_interfaces
)
