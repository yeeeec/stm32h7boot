add_subdirectory(SEGGER_RTT)

add_library(third_party_sha256 STATIC
    mbedTLS/library/platform_util.c
    mbedTLS/library/sha256.c
    mbedTLS/library/base64.c
    mbedTLS/library/asn1parse.c
    mbedTLS/library/bignum.c
    mbedTLS/library/ecp.c
    mbedTLS/library/ecp_curves.c
    mbedTLS/library/ecdsa.c
    mbedTLS/library/oid.c
    mbedTLS/library/platform.c
    mbedTLS/library/error.c
)

target_include_directories(third_party_sha256 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedTLS/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedTLS/firmware_config
)

target_compile_definitions(third_party_sha256 PUBLIC
    MBEDTLS_CONFIG_FILE=\"firmware_mbedtls_config.h\"
)
