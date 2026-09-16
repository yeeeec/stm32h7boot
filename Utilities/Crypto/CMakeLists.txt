add_library(firmware_crypto STATIC
    src/crypto.c
    src/sha256.c
    src/ecdsa_p256.c
)

target_include_directories(firmware_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_crypto
    PUBLIC firmware_types
)
