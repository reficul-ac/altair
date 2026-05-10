add_library(altair_vehicle
  altair_params.c
  altair_mixer.c
)

target_include_directories(altair_vehicle
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(altair_vehicle PUBLIC altair_common)
