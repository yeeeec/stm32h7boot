add_library(board_bsp STATIC
    src/bsp.c
    src/bsp_board.c
    src/bsp_debug.c
    src/bsp_eeprom.c
    src/bsp_external_flash.c
    src/bsp_sdram.c
)

target_include_directories(board_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(board_bsp
    PUBLIC firmware_types
    PRIVATE
        cubemx_generated_stm32h7boot
        driver_at24
        driver_sdram
        driver_spi_nor
)
