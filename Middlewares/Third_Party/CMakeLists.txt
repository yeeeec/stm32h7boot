add_subdirectory(SEGGER_RTT)

add_library(third_party_lwrb STATIC
    lwrb/lwrb/src/lwrb/lwrb.c
)

target_include_directories(third_party_lwrb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lwrb/lwrb/src/include
)
