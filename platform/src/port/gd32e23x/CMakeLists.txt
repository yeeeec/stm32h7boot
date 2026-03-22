add_library(platform_port_gd32e23x OBJECT)

target_sources(
  platform_port_gd32e23x
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/adc_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/clock_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/crc_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/dma_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/gpio_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/i2c_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/iflash_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/irq_handlers.c"
    "${CMAKE_CURRENT_LIST_DIR}/iwdg_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/timer_backend.c"
    "${CMAKE_CURRENT_LIST_DIR}/uart_backend.c")

target_include_directories(
  platform_port_gd32e23x
  PRIVATE
    "${CMAKE_SOURCE_DIR}/platform/include"
    "${CMAKE_CURRENT_LIST_DIR}/internal_inc")

target_link_libraries(
  platform_port_gd32e23x
  PRIVATE
    mcu_options
    gd32e23x_vendor_headers
    middlewares)
