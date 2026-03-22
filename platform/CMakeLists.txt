set(PLATFORM_PORT "stm32h7" CACHE STRING "Platform backend under platform/src/port/<name>")

set(_platform_port_dir "${CMAKE_CURRENT_LIST_DIR}/src/port/${PLATFORM_PORT}")
set(_platform_port_target "platform_port_${PLATFORM_PORT}")

if(NOT EXISTS "${_platform_port_dir}/CMakeLists.txt")
  message(FATAL_ERROR "Unsupported PLATFORM_PORT='${PLATFORM_PORT}': ${_platform_port_dir}/CMakeLists.txt not found")
endif()

add_subdirectory("${_platform_port_dir}" "${CMAKE_CURRENT_BINARY_DIR}/port/${PLATFORM_PORT}")

add_library(
  app_platform STATIC
)
add_library(
  fw::platform ALIAS app_platform
)

target_sources(
  app_platform
  PRIVATE
    $<TARGET_OBJECTS:${_platform_port_target}>
)

target_include_directories(
  app_platform
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
)

target_link_libraries(
  app_platform
  PUBLIC stm32cubemx
)
