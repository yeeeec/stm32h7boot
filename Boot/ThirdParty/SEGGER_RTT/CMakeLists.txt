add_library(boot_segger_rtt STATIC)
target_sources(boot_segger_rtt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/SEGGER_RTT.c
)
target_include_directories(boot_segger_rtt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(boot_segger_rtt PUBLIC stm32cubemx)
