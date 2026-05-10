add_library(altair_common
  math_utils.c
  control_utils.c
)

target_include_directories(altair_common
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(altair_common PUBLIC m)
