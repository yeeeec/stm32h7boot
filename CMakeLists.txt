cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Select the boot variant
set(BOOT_VARIANT "prod" CACHE STRING "Boot variant to build (prod or test)")
set_property(CACHE BOOT_VARIANT PROPERTY STRINGS prod test)
string(TOLOWER "${BOOT_VARIANT}" BOOT_VARIANT)
if(NOT (BOOT_VARIANT STREQUAL "prod" OR BOOT_VARIANT STREQUAL "test"))
    message(FATAL_ERROR "Unsupported BOOT_VARIANT='${BOOT_VARIANT}', expected 'prod' or 'test'")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32h7boot)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})
message("Boot variant: " ${BOOT_VARIANT})

if(BOOT_VARIANT STREQUAL "prod")
    add_compile_definitions(BOOT_VARIANT_PROD=1 BOOT_VARIANT_TEST=0)
else()
    add_compile_definitions(BOOT_VARIANT_PROD=0 BOOT_VARIANT_TEST=1)
endif()

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
if(BOOT_VARIANT STREQUAL "test")
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME "${CMAKE_PROJECT_NAME}_test")
endif()

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(Middlewares/Third_Party)
add_subdirectory(utils)
add_subdirectory(platform)
add_subdirectory(user)

if(TARGET app_logger AND TARGET fw::platform)
    target_link_libraries(app_logger PUBLIC fw::platform)
endif()

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    fw::user

    # Add user defined libraries
)
