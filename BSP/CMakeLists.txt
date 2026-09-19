set(BOARD_BSP_SOURCES
    src/bsp_at24_bus.c
    src/bsp_log_uart.c
    src/bsp_therapy_uart.c
    src/bsp_qspi_flash.c
    src/bsp_therapy_gpio.c
)

add_library(board_bsp STATIC ${BOARD_BSP_SOURCES})

target_include_directories(board_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(board_bsp PRIVATE
)

target_link_libraries(board_bsp PUBLIC firmware_types)
target_link_libraries(board_bsp PRIVATE stm32cubemx)
