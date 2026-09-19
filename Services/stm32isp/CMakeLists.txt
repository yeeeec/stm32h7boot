add_library(stm32isp STATIC
    src/stm32_isp.c
)

target_include_directories(stm32isp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(stm32isp
    PUBLIC firmware_types
)
