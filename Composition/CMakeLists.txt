add_library(firmware_composition STATIC
    src/composition.c
)

target_include_directories(firmware_composition PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_composition PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Services/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../Utilities/Logger/internal
)

target_link_libraries(firmware_composition
    PUBLIC firmware_types
    PRIVATE
        firmware_application
        firmware_services
        firmware_adapters
        firmware_logger
        platform_stm32h7
        board_bsp
)
