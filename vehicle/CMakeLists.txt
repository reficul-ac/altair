add_library(
    altair_vehicle
    ../params/altair_params.c
    ../mixer/altair_mixer.c
    altair_vehicle.c
    altair_fsw.c
    external_guidance.c
    performance_management.c
    relative_launch.c
)

target_include_directories(
    altair_vehicle
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../params
           ${CMAKE_CURRENT_SOURCE_DIR}/../mixer ${CMAKE_CURRENT_SOURCE_DIR}/../config
)

target_link_libraries(altair_vehicle PUBLIC bayek_common bayek_fsw)
altair_enable_warnings(altair_vehicle)

add_library(altair_sim_model ../params/sim/altair_sim_params.c altair_sim_model.c)
target_include_directories(
    altair_sim_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../params/sim
)
target_link_libraries(altair_sim_model PUBLIC altair_vehicle bayek_sim)
altair_enable_warnings(altair_sim_model)

add_library(altair_sitl sitl_case.c)
target_include_directories(altair_sitl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    altair_sitl
    PUBLIC bayek_common
           bayek_sim
           bayek_host_sitl
           altair_vehicle
           altair_sim_model
)
altair_enable_warnings(altair_sitl)

add_executable(sitl_runner sitl_runner.c)
target_link_libraries(sitl_runner PRIVATE altair_sitl altair_sim_model bayek_common)
altair_enable_warnings(sitl_runner)

add_executable(mc_runner mc_runner.c)
target_link_libraries(mc_runner PRIVATE altair_vehicle bayek_sim bayek_common)
altair_enable_warnings(mc_runner)
