add_library(platform_port_stm32h7 OBJECT)

target_sources(
  platform_port_stm32h7
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/boot_platform_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/system_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/fmc_sdram_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/fmc_io_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/ltdc_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/qspi_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/uart_backend.c"
)

target_include_directories(
  platform_port_stm32h7
  PRIVATE
    "${CMAKE_SOURCE_DIR}/platform/include"
    "${CMAKE_SOURCE_DIR}/user/services/inc"
)

target_link_libraries(
  platform_port_stm32h7
  PRIVATE
    lwrb
    stm32cubemx
)

target_compile_definitions(
  stm32cubemx
  INTERFACE
    HAL_SRAM_MODULE_ENABLED
)
