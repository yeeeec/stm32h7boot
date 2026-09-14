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

add_executable(stm32_rom_boot_test
    stm32_rom_boot_test.c
    ../../Drivers/Devices/Stm32RomBoot/stm32_rom_boot.c
)

target_include_directories(stm32_rom_boot_test PRIVATE
    ../../Drivers/Devices/Stm32RomBoot
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(stm32_rom_boot_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME stm32_rom_boot_test COMMAND stm32_rom_boot_test)

add_executable(secondary_mcu_update_service_test
    secondary_mcu_update_service_test.c
    ../../Services/src/use_case/secondary_mcu_update_service.c
)

target_include_directories(secondary_mcu_update_service_test PRIVATE
    ../../Services/include
    ../../Services/internal
    ../../Interfaces/include
    ../../Shared/include
    ../../Utilities/Logger/include
)

target_compile_definitions(secondary_mcu_update_service_test PRIVATE
    FIRMWARE_LOG_ENABLE=0
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(secondary_mcu_update_service_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME secondary_mcu_update_service_test
         COMMAND secondary_mcu_update_service_test)

add_executable(package_image_source_adapter_test
    package_image_source_adapter_test.c
    ../../Adapters/src/package_image_source_adapter.c
)

target_include_directories(package_image_source_adapter_test PRIVATE
    ../../Adapters/include
    ../../Interfaces/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(package_image_source_adapter_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME package_image_source_adapter_test
         COMMAND package_image_source_adapter_test)

add_executable(stm32_rom_boot_programmer_adapter_test
    stm32_rom_boot_programmer_adapter_test.c
    ../../Adapters/src/stm32_rom_boot_programmer_adapter.c
)

target_include_directories(stm32_rom_boot_programmer_adapter_test PRIVATE
    ../../Adapters/include
    ../../Interfaces/include
    ../../Drivers/Devices/Stm32RomBoot
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(stm32_rom_boot_programmer_adapter_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME stm32_rom_boot_programmer_adapter_test
         COMMAND stm32_rom_boot_programmer_adapter_test)

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

function(disable_firmware_logging target)
    target_include_directories(${target} PRIVATE
        ../../Utilities/Logger/include
    )
    target_compile_definitions(${target} PRIVATE
        FIRMWARE_LOG_ENABLE=0
    )
endfunction()

add_executable(services_test
    services_test.c
    ../../Services/src/capability/checked_arithmetic.c
    ../../Services/src/capability/vector_validation.c
    ../../Services/src/capability/version_policy.c
    ../../Services/src/runtime_layout.c
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
    ../../Services/src/runtime_layout.c
)

target_include_directories(checksum_boot_control_test PRIVATE
    ../../Utilities/Checksum/include
    ../../Services/include
    ../../Services/internal
    ../../Interfaces/include
    ../../Shared/include
)
disable_firmware_logging(checksum_boot_control_test)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(checksum_boot_control_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME checksum_boot_control_test COMMAND checksum_boot_control_test)

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
    target_compile_options(crypto_json_test PRIVATE
        -Wall -Wextra -Werror
        -Wno-unknown-pragmas
    )
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

add_executable(update_request_service_test
    update_request_service_test.c
    ../../Services/src/capability/json_document.c
    ../../Services/src/capability/manifest_service.c
    ../../Services/src/capability/update_request_service.c
)

target_include_directories(update_request_service_test PRIVATE
    ../../Services/include
    ../../Services/internal
    ../../Interfaces/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(update_request_service_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME update_request_service_test COMMAND update_request_service_test)
set_target_properties(update_request_service_test PROPERTIES
    OUTPUT_NAME firmware_request_contract_test
)

function(add_fatfs_adapter_host_test target source)
    add_executable(${target}
        ${source}
        fatfs_test_support.c
        ../../Adapters/src/fatfs_package_source_adapter.c
        ../../Adapters/src/fatfs_update_request_store_adapter.c
    )

    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fakes
        ../../Adapters/include
        ../../Interfaces/include
        ../../Shared/include
    )
    disable_firmware_logging(${target})

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()

    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_fatfs_adapter_host_test(
    fatfs_package_source_adapter_test
    fatfs_package_source_adapter_test.c
)

add_fatfs_adapter_host_test(
    fatfs_update_request_store_adapter_test
    fatfs_update_request_store_adapter_test.c
)
set_target_properties(fatfs_update_request_store_adapter_test PROPERTIES
    OUTPUT_NAME fatfs_request_store_adapter_test
)

add_executable(update_service_lifecycle_test
    update_service_lifecycle_test.c
    ../../Services/src/use_case/update_service.c
    ../../Services/src/runtime_layout.c
)

target_include_directories(update_service_lifecycle_test PRIVATE
    ../../Services/include
    ../../Services/internal
    ../../Interfaces/include
    ../../Shared/include
)
disable_firmware_logging(update_service_lifecycle_test)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(update_service_lifecycle_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME update_service_lifecycle_test COMMAND update_service_lifecycle_test)
set_target_properties(update_service_lifecycle_test PROPERTIES
    OUTPUT_NAME firmware_lifecycle_test
)

add_executable(application_orchestration_test
    application_orchestration_test.c
    ../../Application/src/application.c
    ../../Services/src/capability/version_policy.c
)

target_include_directories(application_orchestration_test PRIVATE
    ../../Application/include
    ../../Application/internal
    ../../Services/include
    ../../Interfaces/include
    ../../Shared/include
)
disable_firmware_logging(application_orchestration_test)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(application_orchestration_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME application_orchestration_test COMMAND application_orchestration_test)

add_executable(application_initial_install_test
    application_orchestration_test.c
    ../../Application/src/application.c
    ../../Services/src/capability/version_policy.c
)

target_include_directories(application_initial_install_test PRIVATE
    ../../Application/include
    ../../Application/internal
    ../../Services/include
    ../../Interfaces/include
    ../../Shared/include
)
disable_firmware_logging(application_initial_install_test)

target_compile_definitions(application_initial_install_test PRIVATE
    TEST_INITIAL_INSTALL=1
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(application_initial_install_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(
    NAME application_initial_install_test
    COMMAND application_initial_install_test
)
set_target_properties(application_initial_install_test PROPERTIES
    OUTPUT_NAME firmware_initial_boot_test
)

foreach(media_test IN ITEMS
    application_media_removal_test
    application_mount_failure_retry_test
)
    add_executable(${media_test}
        application_orchestration_test.c
        ../../Application/src/application.c
        ../../Services/src/capability/version_policy.c
    )

    target_include_directories(${media_test} PRIVATE
        ../../Application/include
        ../../Application/internal
        ../../Services/include
        ../../Interfaces/include
        ../../Shared/include
    )
    disable_firmware_logging(${media_test})
    target_compile_definitions(${media_test} PRIVATE
        TEST_INITIAL_INSTALL=1
        TEST_MEDIA_REMOVAL=1
    )
    if(media_test STREQUAL "application_mount_failure_retry_test")
        target_compile_definitions(${media_test} PRIVATE
            TEST_MEDIA_MOUNT_FAILURE=1
        )
    endif()

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${media_test} PRIVATE
            -Wall -Wextra -Werror
        )
    endif()

    add_test(NAME ${media_test} COMMAND ${media_test})
endforeach()

add_executable(application_fail_closed_test
    application_fail_closed_test.c
    ../../Application/src/application.c
    ../../Services/src/capability/version_policy.c
)

target_include_directories(application_fail_closed_test PRIVATE
    ../../Application/include
    ../../Application/internal
    ../../Services/include
    ../../Interfaces/include
    ../../Shared/include
)
disable_firmware_logging(application_fail_closed_test)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(application_fail_closed_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(NAME application_fail_closed_test COMMAND application_fail_closed_test)

add_executable(application_active_fallback_test
    application_fail_closed_test.c
    ../../Application/src/application.c
    ../../Services/src/capability/version_policy.c
)

target_include_directories(application_active_fallback_test PRIVATE
    ../../Application/include
    ../../Application/internal
    ../../Services/include
    ../../Interfaces/include
    ../../Shared/include
)
disable_firmware_logging(application_active_fallback_test)

target_compile_definitions(application_active_fallback_test PRIVATE
    TEST_ACTIVE_VALIDATION_FAILURE=1
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(application_active_fallback_test PRIVATE
        -Wall -Wextra -Werror
    )
endif()

add_test(
    NAME application_active_fallback_test
    COMMAND application_active_fallback_test
)

function(add_application_formal_test target)
    add_executable(${target}
        application_orchestration_test.c
        ../../Application/src/application.c
        ../../Services/src/capability/version_policy.c
    )
    target_include_directories(${target} PRIVATE
        ../../Application/include
        ../../Application/internal
        ../../Services/include
        ../../Interfaces/include
        ../../Shared/include
    )
    disable_firmware_logging(${target})
    target_compile_definitions(${target} PRIVATE ${ARGN})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_application_formal_test(application_invalid_request_test TEST_INVALID_REQUEST=1)
add_application_formal_test(application_version_reject_test TEST_VERSION_REJECT=1)
add_application_formal_test(application_therapy_only_test TEST_THERAPY_ONLY=1)
add_application_formal_test(application_therapy_downgrade_test TEST_THERAPY_DOWNGRADE=1)
add_application_formal_test(application_stale_valid_test TEST_STALE_VALID=1)
add_application_formal_test(application_stale_invalid_test TEST_STALE_INVALID=1)
add_application_formal_test(application_commit_failure_test TEST_COMMIT_FAILURE=1)
add_application_formal_test(application_clear_failure_test TEST_CLEAR_FAILURE=1)
add_application_formal_test(application_runtime_invalid_no_request_test TEST_RUNTIME_INVALID_NO_REQUEST=1)
add_application_formal_test(application_install_post_mutation_test TEST_INSTALL_POST_MUTATION=1)
add_application_formal_test(application_install_xip_failure_test TEST_INSTALL_XIP_FAILURE=1)
add_application_formal_test(application_unmount_retry_test TEST_UNMOUNT_RETRY=1)
add_application_formal_test(application_unmount_permanent_failure_test TEST_UNMOUNT_PERMANENT_FAILURE=1)
