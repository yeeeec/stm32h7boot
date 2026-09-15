add_library(update STATIC
    src/update_manifest.c
)

target_include_directories(update PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(update
    PUBLIC firmware_types
)
