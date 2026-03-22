add_subdirectory(lwrb)

target_link_libraries(
  lwrb
  PUBLIC stm32cubemx
)

target_link_libraries(
  lwrb_ex
  PUBLIC stm32cubemx
)

add_library(
  fw::lwrb ALIAS lwrb
)
add_library(
  fw::lwrb_ex ALIAS lwrb_ex
)
