add_library(altair_sim
  sim_plant.c
)

target_include_directories(altair_sim
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(altair_sim PUBLIC altair_common altair_fsw)

add_executable(sitl_runner sitl_runner.c)
target_link_libraries(sitl_runner PRIVATE altair_sim altair_fsw altair_vehicle altair_common)
