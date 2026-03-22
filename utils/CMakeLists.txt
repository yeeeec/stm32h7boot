add_subdirectory(logger)

add_library(
  app_utils INTERFACE
)
add_library(
  fw::utils ALIAS app_utils
)

target_link_libraries(
  app_utils
  INTERFACE app_logger
)
