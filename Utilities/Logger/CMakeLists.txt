option(FIRMWARE_LOG_ENABLE "Enable formatted firmware logging" ON)
option(FIRMWARE_LOG_USE_COLOR "Emit ANSI colors in firmware logs" ON)

set(FIRMWARE_LOG_LEVEL "DEBUG" CACHE STRING
    "Maximum firmware log level: NONE, ERROR, WARN, INFO, or DEBUG")
set_property(CACHE FIRMWARE_LOG_LEVEL PROPERTY STRINGS
    NONE ERROR WARN INFO DEBUG)

string(TOUPPER "${FIRMWARE_LOG_LEVEL}" FIRMWARE_LOG_LEVEL_NORMALIZED)
if(FIRMWARE_LOG_LEVEL_NORMALIZED STREQUAL "NONE")
    set(FIRMWARE_LOG_LEVEL_VALUE 0)
elseif(FIRMWARE_LOG_LEVEL_NORMALIZED STREQUAL "ERROR")
    set(FIRMWARE_LOG_LEVEL_VALUE 1)
elseif(FIRMWARE_LOG_LEVEL_NORMALIZED STREQUAL "WARN")
    set(FIRMWARE_LOG_LEVEL_VALUE 2)
elseif(FIRMWARE_LOG_LEVEL_NORMALIZED STREQUAL "INFO")
    set(FIRMWARE_LOG_LEVEL_VALUE 3)
elseif(FIRMWARE_LOG_LEVEL_NORMALIZED STREQUAL "DEBUG")
    set(FIRMWARE_LOG_LEVEL_VALUE 4)
else()
    message(FATAL_ERROR
        "Invalid FIRMWARE_LOG_LEVEL='${FIRMWARE_LOG_LEVEL}'. "
        "Expected NONE, ERROR, WARN, INFO, or DEBUG.")
endif()

# The frontend owns formatting and filtering only. Hardware output and time are
# supplied through firmware interfaces by the Composition layer.
add_library(firmware_logger STATIC
    src/logging.c
)

target_include_directories(firmware_logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_logger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/internal
)

target_compile_definitions(firmware_logger PUBLIC
    FIRMWARE_LOG_ENABLE=$<IF:$<BOOL:${FIRMWARE_LOG_ENABLE}>,1,0>
    FIRMWARE_LOG_LEVEL=${FIRMWARE_LOG_LEVEL_VALUE}
    FIRMWARE_LOG_USE_COLOR=$<IF:$<BOOL:${FIRMWARE_LOG_USE_COLOR}>,1,0>
)

target_link_libraries(firmware_logger PRIVATE firmware_interfaces)
