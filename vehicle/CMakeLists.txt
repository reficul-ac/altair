add_library(altair_vehicle
  ../params/altair_params.c
  ../mixer/altair_mixer.c
  altair_vehicle.c
)

target_include_directories(altair_vehicle
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../params
    ${CMAKE_CURRENT_SOURCE_DIR}/../mixer
    ${CMAKE_CURRENT_SOURCE_DIR}/../config
)

target_link_libraries(altair_vehicle PUBLIC bayek_common bayek_fsw)

add_executable(sitl_runner sitl_runner.c)
target_link_libraries(sitl_runner PRIVATE altair_vehicle bayek_sim bayek_fsw bayek_common)

add_executable(mc_runner mc_runner.c)
target_link_libraries(mc_runner PRIVATE altair_vehicle bayek_sim bayek_fsw bayek_common)
