add_library(altair_fsw
  fsw.c
)

target_include_directories(altair_fsw
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(altair_fsw
  PUBLIC
    altair_common
    altair_vehicle
)
