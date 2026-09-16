set(BOARD_BSP_SOURCES
    src/bsp.c
    src/bsp_debug.c
    src/bsp_display.c
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
    driver_at24
    driver_gt911
    driver_sdram
)
