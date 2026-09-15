set(BOARD_BSP_SOURCES
    src/bsp.c
    src/bsp_debug.c
    src/bsp_eeprom.c
    src/bsp_external_flash.c
    src/bsp_stm32_rom_boot.c
)

add_library(board_bsp STATIC ${BOARD_BSP_SOURCES})

target_include_directories(board_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(board_bsp PRIVATE
    ${CMAKE_SOURCE_DIR}/FATFS/App
    ${CMAKE_SOURCE_DIR}/FATFS/Target
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FatFs/src
)

target_link_libraries(board_bsp PUBLIC firmware_types)
target_link_libraries(board_bsp PRIVATE cubemx_generated_stm32h7boot third_party_lwrb)

target_link_libraries(board_bsp PRIVATE
        driver_at24
        driver_spi_nor
        driver_stm32_rom_boot
)
