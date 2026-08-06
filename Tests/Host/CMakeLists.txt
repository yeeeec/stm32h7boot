cmake_minimum_required(VERSION 3.22)

project(stm32h7boot_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(at24_test
    at24_test.c
    ../../Drivers/Devices/At24/at24.c
    ../../Adapters/src/at24_boot_control_adapter.c
)

target_include_directories(at24_test PRIVATE
    ../../Drivers/Devices/At24
    ../../Adapters/include
    ../../Interfaces/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(at24_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME at24_test COMMAND at24_test)

add_executable(spi_nor_test
    spi_nor_test.c
    ../../Drivers/Devices/SpiNor/spi_nor.c
)

target_include_directories(spi_nor_test PRIVATE
    ../../Drivers/Devices/SpiNor
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spi_nor_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME spi_nor_test COMMAND spi_nor_test)

add_executable(sdram_test
    sdram_test.c
    ../../Drivers/Devices/Sdram/sdram.c
)

target_include_directories(sdram_test PRIVATE
    ../../Drivers/Devices/Sdram
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sdram_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME sdram_test COMMAND sdram_test)

add_executable(logger_test
    logger_test.c
    ../../Utilities/Logger/src/logging.c
)

target_include_directories(logger_test PRIVATE
    ../../Utilities/Logger/include
    ../../Utilities/Logger/internal
    ../../Interfaces/include
    ../../Shared/include
)

target_compile_definitions(logger_test PRIVATE
    FIRMWARE_LOG_ENABLE=1
    FIRMWARE_LOG_LEVEL=4
    FIRMWARE_LOG_USE_COLOR=0
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(logger_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME logger_test COMMAND logger_test)

add_executable(services_test
    services_test.c
    ../../Services/src/capability/checked_arithmetic.c
    ../../Services/src/capability/slot_policy.c
    ../../Services/src/capability/vector_validation.c
    ../../Services/src/capability/version_policy.c
)

target_include_directories(services_test PRIVATE
    ../../Services/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(services_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME services_test COMMAND services_test)

add_executable(checksum_boot_control_test
    checksum_boot_control_test.c
    ../../Utilities/Checksum/src/crc32_iso_hdlc.c
    ../../Services/src/capability/boot_control_service.c
    ../../Services/src/capability/checked_arithmetic.c
    ../../Services/src/capability/slot_policy.c
)

target_include_directories(checksum_boot_control_test PRIVATE
    ../../Utilities/Checksum/include
    ../../Services/include
    ../../Services/internal
    ../../Interfaces/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(checksum_boot_control_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME checksum_boot_control_test COMMAND checksum_boot_control_test)

add_executable(appx_relocation_test
    appx_relocation_test.c
    ../../Utilities/Checksum/src/crc32_iso_hdlc.c
    ../../Services/src/capability/appx_validation.c
    ../../Services/src/capability/checked_arithmetic.c
    ../../Services/src/capability/relocation_service.c
)

target_include_directories(appx_relocation_test PRIVATE
    ../../Utilities/Checksum/include
    ../../Services/include
    ../../Interfaces/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(appx_relocation_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME appx_relocation_test COMMAND appx_relocation_test)

add_executable(crypto_json_test
    crypto_json_test.c
    ../../Utilities/Crypto/src/sha256.c
    ../../Utilities/Crypto/src/micro_ecc_authenticator.c
    ../../Utilities/Crypto/MicroEcc/uECC.c
    ../../Services/src/capability/base64.c
    ../../Services/src/capability/json_document.c
)

target_include_directories(crypto_json_test PRIVATE
    ../../Utilities/Crypto/include
    ../../Utilities/Crypto/MicroEcc
    ../../Services/internal
    ../../Interfaces/include
    ../../Shared/include
)

target_compile_definitions(crypto_json_test PRIVATE
    uECC_PLATFORM=uECC_arch_other
    uECC_WORD_SIZE=4
    uECC_OPTIMIZATION_LEVEL=2
    uECC_SUPPORTS_secp160r1=0
    uECC_SUPPORTS_secp192r1=0
    uECC_SUPPORTS_secp224r1=0
    uECC_SUPPORTS_secp256k1=0
    uECC_SUPPORT_COMPRESSED_POINT=0
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(crypto_json_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME crypto_json_test COMMAND crypto_json_test)

add_executable(manifest_service_test
    manifest_service_test.c
    ../../Services/src/capability/base64.c
    ../../Services/src/capability/json_document.c
    ../../Services/src/capability/manifest_service.c
)

target_include_directories(manifest_service_test PRIVATE
    ../../Services/include
    ../../Services/internal
    ../../Interfaces/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(manifest_service_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME manifest_service_test COMMAND manifest_service_test)
