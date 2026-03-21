add_library(boot_platform STATIC)
target_sources(boot_platform PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_platform.c
)
target_include_directories(boot_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
)
target_link_libraries(boot_platform PUBLIC stm32cubemx)
