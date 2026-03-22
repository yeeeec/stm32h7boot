set(USER_SERVICES_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/boot_crc32.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_error.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_extflash.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_handoff.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_info.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_log.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_simple_flash.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_simple_jump.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_simple_manifest.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_simple_upgrade.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_udisk_check.c"
  "${CMAKE_CURRENT_LIST_DIR}/boot_usb.c"
)

if(USER_BUILD_EXAMPLES)
  list(APPEND USER_SERVICES_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/svc_temp.c"
    "${CMAKE_CURRENT_LIST_DIR}/svc_temp_state.c"
    "${CMAKE_CURRENT_LIST_DIR}/svc_io.c"
  )
endif()

add_library(
  user_services STATIC
  ${USER_SERVICES_SOURCES}
)
add_library(
  fw::user_services ALIAS user_services
)

target_include_directories(
  user_services
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(
  user_services
  PUBLIC fw::logger
         fw::platform
         fw::utils
)
