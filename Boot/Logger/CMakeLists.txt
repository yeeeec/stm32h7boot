option(BOOT_LOGGER_ENABLE "Enable boot logger framework" ON)
option(BOOT_LOGGER_BACKEND_RTT "Enable RTT logger backend" ON)
option(BOOT_LOGGER_BACKEND_UART "Enable UART logger backend" OFF)

if(NOT BOOT_LOGGER_ENABLE)
  add_library(boot_logger INTERFACE)
  return()
endif()

if(NOT BOOT_LOGGER_BACKEND_RTT AND NOT BOOT_LOGGER_BACKEND_UART)
  message(FATAL_ERROR "At least one boot logger backend must be enabled.")
endif()

if(BOOT_LOGGER_BACKEND_RTT)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/SEGGER_RTT ${CMAKE_CURRENT_BINARY_DIR}/segger_rtt)
endif()

add_library(boot_logger STATIC)
target_sources(boot_logger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/logging.c
)
target_include_directories(boot_logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)
target_compile_definitions(boot_logger PUBLIC
    LOG_ENABLE=1
    LOG_BACKEND_RTT=$<IF:$<BOOL:${BOOT_LOGGER_BACKEND_RTT}>,1,0>
    LOG_BACKEND_UART=$<IF:$<BOOL:${BOOT_LOGGER_BACKEND_UART}>,1,0>
)
target_link_libraries(boot_logger PUBLIC stm32cubemx)

if(BOOT_LOGGER_BACKEND_RTT)
  target_sources(boot_logger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src/log_backend_rtt.c)
  target_link_libraries(boot_logger PUBLIC boot_segger_rtt)
endif()

if(BOOT_LOGGER_BACKEND_UART)
  target_sources(boot_logger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src/log_backend_uart.c)
endif()
