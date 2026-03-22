add_library(
  segger_rtt STATIC
  "${CMAKE_CURRENT_LIST_DIR}/SEGGER_RTT.c"
)
add_library(
  fw::segger_rtt ALIAS segger_rtt
)

target_include_directories(
  segger_rtt
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)

target_link_libraries(
  segger_rtt
  PUBLIC stm32cubemx
)
