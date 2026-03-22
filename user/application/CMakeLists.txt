add_library(
  user_application STATIC
  "${CMAKE_CURRENT_LIST_DIR}/boot_app.c"
)
add_library(
  fw::user_application ALIAS user_application
)

target_include_directories(
  user_application
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}/inc"
)

target_link_libraries(
  user_application
  PUBLIC fw::user_services
)

if(USER_BUILD_EXAMPLES)
  add_library(
    user_application_temp STATIC
    "${CMAKE_CURRENT_LIST_DIR}/app_temp.c"
    "${CMAKE_CURRENT_LIST_DIR}/app_temp_state.c"
  )
  add_library(
    fw::user_application_temp ALIAS user_application_temp
  )

  target_include_directories(
    user_application_temp
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/inc"
  )

  target_link_libraries(
    user_application_temp
    PUBLIC fw::logger
           fw::platform
           fw::user_services
  )
endif()
