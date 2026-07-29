set(BOOT_FRAMEWORK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/boot_jump.c
)

add_library(boot_framework STATIC)
target_sources(boot_framework PRIVATE ${BOOT_FRAMEWORK_SOURCES})
target_include_directories(boot_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)
target_link_libraries(boot_framework PUBLIC stm32cubemx)
