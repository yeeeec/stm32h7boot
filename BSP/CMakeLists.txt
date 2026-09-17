set(BOARD_BSP_SOURCES
    src/bsp.c

    src/bsp_i2c.c

    src/bsp_log_uart.c
    src/bsp_isp_uart.c

    src/bsp_qspi_flash.c

    src/bsp_isp_gpio.c
)

add_library(board_bsp STATIC ${BOARD_BSP_SOURCES})

target_include_directories(board_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(board_bsp PRIVATE
)

target_link_libraries(board_bsp PUBLIC firmware_types)
target_link_libraries(board_bsp PRIVATE stm32cubemx)

target_link_libraries(board_bsp PRIVATE
    device_drivers
)
