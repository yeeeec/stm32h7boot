add_library(segger_rtt STATIC
  SEGGER_RTT.c
)
add_library(
  fw::segger_rtt ALIAS segger_rtt
)

target_include_directories(segger_rtt PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

