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
altair_enable_warnings(altair_vehicle)

add_library(altair_sitl
  sitl_initial_conditions.c
)
target_include_directories(altair_sitl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(altair_sitl PUBLIC bayek_common)
altair_enable_warnings(altair_sitl)

add_executable(sitl_runner sitl_runner.c)
target_link_libraries(sitl_runner PRIVATE altair_sitl altair_vehicle bayek_sim bayek_fsw bayek_common)
altair_enable_warnings(sitl_runner)

add_executable(mc_runner mc_runner.c)
target_link_libraries(mc_runner PRIVATE altair_vehicle bayek_sim bayek_fsw bayek_common)
altair_enable_warnings(mc_runner)
