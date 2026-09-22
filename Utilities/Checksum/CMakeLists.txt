add_library(firmware_checksum STATIC
    src/crc8.c
    src/crc32.c
)

target_include_directories(firmware_checksum PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
