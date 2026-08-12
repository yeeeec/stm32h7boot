add_library(firmware_composition STATIC
    src/composition.c
)

set(SECONDARY_MCU_TARGET "F429" CACHE STRING
    "Therapy controller target: F030 or F429")
set_property(CACHE SECONDARY_MCU_TARGET PROPERTY STRINGS F030 F429)

if(SECONDARY_MCU_TARGET STREQUAL "F030")
    target_compile_definitions(firmware_composition PRIVATE
        COMPOSITION_SECONDARY_MCU_TARGET=2
    )
elseif(SECONDARY_MCU_TARGET STREQUAL "F429")
    target_compile_definitions(firmware_composition PRIVATE
        COMPOSITION_SECONDARY_MCU_TARGET=1
    )
else()
    message(FATAL_ERROR
        "SECONDARY_MCU_TARGET must be F030 or F429, got: ${SECONDARY_MCU_TARGET}")
endif()

target_include_directories(firmware_composition PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_composition PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../Utilities/Logger/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../Services/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../Application/internal
)

target_link_libraries(firmware_composition
    PUBLIC firmware_types firmware_interfaces
    PRIVATE
        firmware_application
        firmware_services
        firmware_adapters
        firmware_checksum
        firmware_crypto
        firmware_logger
        platform_stm32h7
        board_bsp
        cubemx_generated_stm32h7boot
)
