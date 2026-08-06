add_library(firmware_crypto STATIC
    src/micro_ecc_authenticator.c
    src/sha256.c
    MicroEcc/uECC.c
)

target_include_directories(firmware_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_crypto PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/MicroEcc
)

target_compile_definitions(firmware_crypto PRIVATE
    uECC_PLATFORM=uECC_arch_other
    uECC_WORD_SIZE=4
    uECC_OPTIMIZATION_LEVEL=2
    uECC_SUPPORTS_secp160r1=0
    uECC_SUPPORTS_secp192r1=0
    uECC_SUPPORTS_secp224r1=0
    uECC_SUPPORTS_secp256k1=0
    uECC_SUPPORT_COMPRESSED_POINT=0
)

target_link_libraries(firmware_crypto
    PUBLIC firmware_interfaces
)
