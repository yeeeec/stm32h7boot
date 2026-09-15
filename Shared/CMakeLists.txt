add_library(firmware_types INTERFACE)

target_include_directories(firmware_types INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(firmware_storage_common STATIC
    src/storage/storage_common.c
)

target_include_directories(firmware_storage_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_storage_common PUBLIC firmware_types)
