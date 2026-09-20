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

set(BOOTLOADER_UPDATE_USE_JOURNAL "1" CACHE STRING
    "Bootloader update mode: 1 for production journal, 0 for debug file flow")
if(NOT BOOTLOADER_UPDATE_USE_JOURNAL STREQUAL "0" AND
   NOT BOOTLOADER_UPDATE_USE_JOURNAL STREQUAL "1")
    message(FATAL_ERROR "BOOTLOADER_UPDATE_USE_JOURNAL must be 0 or 1")
endif()
set(BOOTLOADER_UPDATE_APP_GUI_ONLY "1" CACHE STRING
    "Temporary update scope: 1 for HMI APP/GUI only, 0 for all registered targets")
if(NOT BOOTLOADER_UPDATE_APP_GUI_ONLY STREQUAL "0" AND
   NOT BOOTLOADER_UPDATE_APP_GUI_ONLY STREQUAL "1")
    message(FATAL_ERROR "BOOTLOADER_UPDATE_APP_GUI_ONLY must be 0 or 1")
endif()

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

add_subdirectory(BSP)
add_subdirectory(Shared)
add_subdirectory(Drivers/Devices)
add_subdirectory(Platform/STM32H7)
add_subdirectory(Middlewares/Third_Party)
add_subdirectory(Utilities/Logger)
add_subdirectory(Utilities/Crypto)
add_subdirectory(Utilities/Checksum)
add_subdirectory(Services)
add_subdirectory(Application)

target_compile_definitions(update_service PRIVATE
    BOOTLOADER_UPDATE_USE_JOURNAL=${BOOTLOADER_UPDATE_USE_JOURNAL}
    BOOTLOADER_UPDATE_APP_GUI_ONLY=${BOOTLOADER_UPDATE_APP_GUI_ONLY})
target_compile_definitions(firmware_application PRIVATE
    BOOTLOADER_UPDATE_USE_JOURNAL=${BOOTLOADER_UPDATE_USE_JOURNAL}
    BOOTLOADER_UPDATE_APP_GUI_ONLY=${BOOTLOADER_UPDATE_APP_GUI_ONLY})

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

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
    firmware_application
)
