add_library(firmware_services STATIC
    src/runtime_service.c
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
