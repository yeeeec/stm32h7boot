cmake_minimum_required(VERSION 3.22)

project(stm32h7boot_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(spi_nor_test
    spi_nor_test.c
    ../../Drivers/Devices/SpiNor/spi_nor.c
)

target_include_directories(spi_nor_test PRIVATE
    ../../Drivers/Devices/SpiNor
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spi_nor_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME spi_nor_test COMMAND spi_nor_test)

add_executable(sdram_test
    sdram_test.c
    ../../Drivers/Devices/Sdram/sdram.c
)

target_include_directories(sdram_test PRIVATE
    ../../Drivers/Devices/Sdram
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sdram_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME sdram_test COMMAND sdram_test)

add_executable(logger_test
    logger_test.c
    ../../Utilities/Logger/src/logging.c
)

target_include_directories(logger_test PRIVATE
    ../../Utilities/Logger/include
    ../../Utilities/Logger/internal
    ../../Interfaces/include
    ../../Shared/include
)

target_compile_definitions(logger_test PRIVATE
    FIRMWARE_LOG_ENABLE=1
    FIRMWARE_LOG_LEVEL=4
    FIRMWARE_LOG_USE_COLOR=0
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(logger_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME logger_test COMMAND logger_test)
