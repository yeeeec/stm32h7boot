# Device drivers are declared in this directory so firmware targets only need
# one add_subdirectory(Drivers/Devices). Each driver remains a separate target
# and receives only shared, hardware-independent types.
add_library(driver_sdram STATIC
    Sdram/sdram.c
)

target_include_directories(driver_sdram PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Sdram/
)

target_link_libraries(driver_sdram PUBLIC firmware_types)

# AT24 owns EEPROM protocol and write-cycle state without HAL dependencies.
add_library(driver_at24 STATIC
    At24/at24.c
)

target_include_directories(driver_at24 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/At24
)

target_link_libraries(driver_at24 PUBLIC firmware_types)

# GT911 TouchDrivers.
add_library(driver_gt911 STATIC
    Gt911/gt911_reg.c
    Gt911/gt911.c
)

target_include_directories(driver_gt911 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Gt911
)

target_link_libraries(driver_gt911 PUBLIC firmware_types)
