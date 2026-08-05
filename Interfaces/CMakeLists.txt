add_library(firmware_interfaces INTERFACE)

target_include_directories(firmware_interfaces INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_interfaces INTERFACE firmware_types)
