option(USER_BUILD_EXAMPLES "Build sample user application/services targets" OFF)

add_subdirectory(services)
add_subdirectory(application)

add_library(
  user INTERFACE
)
add_library(
  fw::user ALIAS user
)

target_link_libraries(
  user
  INTERFACE fw::user_application
            fw::user_services
)
