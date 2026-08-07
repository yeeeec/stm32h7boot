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

# Set the project name
set(CMAKE_PROJECT_NAME stm32h7boot)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Narrow facade for project-owned low-level code. Unlike the generated
# stm32cubemx target, this deliberately excludes middleware include paths.
add_library(cubemx_generated_stm32h7boot INTERFACE)
target_include_directories(cubemx_generated_stm32h7boot INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include
)
target_compile_definitions(cubemx_generated_stm32h7boot INTERFACE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H743xx
    $<$<CONFIG:Debug>:DEBUG>
)

# Add project-owned architecture layers. The order mirrors the dependency
# direction and keeps HAL/CubeMX include paths out of Services and Application.
add_subdirectory(Shared)
add_subdirectory(Interfaces)
add_subdirectory(Drivers/Devices)
add_subdirectory(Platform/STM32H7)
add_subdirectory(BSP)
add_subdirectory(Adapters)
add_subdirectory(Utilities/Logger)
add_subdirectory(Utilities/Checksum)
add_subdirectory(Utilities/Crypto)
add_subdirectory(Services)
add_subdirectory(Application)
add_subdirectory(Composition)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/Utilities/Logger/include
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    platform_stm32h7
    board_bsp
    firmware_application
    firmware_composition

    # Add user defined libraries
)
