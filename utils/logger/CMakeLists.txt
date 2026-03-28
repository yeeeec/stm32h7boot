option(LOG_ENABLE "Enable logging module" ON)
option(LOG_BACKEND_RTT "Enable RTT log backend" OFF)
option(LOG_BACKEND_UART "Enable UART log backend" ON)
option(LOG_USE_RTOS "Enable RTOS-aware logging mode" OFF)
set(LOG_BACKEND_UART_ID "PLAT_UART_DISPLAY" CACHE STRING
    "Platform UART ID used by the UART log backend")

if(NOT LOG_ENABLE)
  message(FATAL_ERROR "LOG_ENABLE=OFF is not supported by the current application.")
endif()

if(NOT LOG_BACKEND_RTT AND NOT LOG_BACKEND_UART)
  message(FATAL_ERROR "At least one logger backend must be enabled.")
endif()

if(LOG_USE_RTOS)
  message(WARNING "LOG_USE_RTOS is enabled, but no RTOS integration is configured in this project.")
endif()

add_library(
  app_logger STATIC
)
add_library(
  fw::logger ALIAS app_logger
)

target_sources(
  app_logger
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/logging.c"
)

target_include_directories(
  app_logger
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_compile_definitions(
  app_logger
  PUBLIC LOG_ENABLE=$<IF:$<BOOL:${LOG_ENABLE}>,1,0>
         LOG_BACKEND_RTT=$<IF:$<BOOL:${LOG_BACKEND_RTT}>,1,0>
         LOG_BACKEND_UART=$<IF:$<BOOL:${LOG_BACKEND_UART}>,1,0>
         LOG_USE_RTOS=$<IF:$<BOOL:${LOG_USE_RTOS}>,1,0>
)

if(LOG_BACKEND_RTT)
  if(NOT TARGET segger_rtt)
    message(FATAL_ERROR "LOG_BACKEND_RTT requires target 'segger_rtt'.")
  endif()
  target_sources(app_logger PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/log_backend_rtt.c")
  target_link_libraries(app_logger PUBLIC fw::segger_rtt)
endif()

if(LOG_BACKEND_UART)
  target_sources(app_logger PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/log_backend_uart.c")
  target_include_directories(app_logger PRIVATE "${CMAKE_SOURCE_DIR}/platform/include")
  target_link_libraries(app_logger PUBLIC lwrb)
  target_compile_definitions(app_logger PUBLIC PL_LOG_UART_ID=${LOG_BACKEND_UART_ID})
endif()
