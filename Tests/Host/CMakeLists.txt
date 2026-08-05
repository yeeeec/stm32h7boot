cmake_minimum_required(VERSION 3.22)

project(stm32h7boot_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(spi_nor_test
    spi_nor_test.c
    ../../Drivers/Devices/SpiNor/src/spi_nor.c
)

target_include_directories(spi_nor_test PRIVATE
    ../../Drivers/Devices/SpiNor/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spi_nor_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME spi_nor_test COMMAND spi_nor_test)

add_executable(sdram_test
    sdram_test.c
    ../../Drivers/Devices/Sdram/src/sdram.c
)

target_include_directories(sdram_test PRIVATE
    ../../Drivers/Devices/Sdram/include
    ../../Shared/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sdram_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME sdram_test COMMAND sdram_test)
