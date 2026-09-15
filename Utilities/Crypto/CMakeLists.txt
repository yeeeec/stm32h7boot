add_library(firmware_crypto STATIC
    # 生产固件当前只使用 SHA-256；ECDSA/Micro-ECC 保留在 Host 测试路径，
    # 在签名 Manifest 进入发布合同前不得被链接进生产目标。
    src/crypto.c
    src/sha256.c
)

target_include_directories(firmware_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmware_crypto
    PUBLIC firmware_interfaces
)
