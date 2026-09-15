add_library(firmware_checksum STATIC
    src/crc8.c
    src/crc32.c
    src/crc32_iso_hdlc.c
)

target_include_directories(firmware_checksum PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_checksum
    PUBLIC firmware_interfaces
)
