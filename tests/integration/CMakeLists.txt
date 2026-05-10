add_executable(test_replay test_replay.c)
target_link_libraries(test_replay PRIVATE bayek_fsw altair_vehicle bayek_common)
altair_enable_warnings(test_replay)
add_test(NAME integration_replay COMMAND test_replay)

add_executable(test_sitl_smoke test_sitl_smoke.c)
target_link_libraries(test_sitl_smoke PRIVATE bayek_sim bayek_fsw altair_vehicle bayek_common)
altair_enable_warnings(test_sitl_smoke)
add_test(NAME integration_sitl_smoke COMMAND test_sitl_smoke)

set(SITL_SMOKE_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_smoke.csv")
add_test(
  NAME integration_sitl_runner_cli
  COMMAND $<TARGET_FILE:sitl_runner> --scenario smoke --output "${SITL_SMOKE_CSV}"
)
add_test(
  NAME integration_sitl_runner_cli_csv
  COMMAND
    ${CMAKE_COMMAND}
    -DCSV_FILE=${SITL_SMOKE_CSV}
    -DEXPECTED_HEADER=step,time_s,mode,motor,aileron,elevator,rudder,airspeed_mps,altitude_m
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_csv.cmake"
)
set_tests_properties(integration_sitl_runner_cli_csv PROPERTIES DEPENDS integration_sitl_runner_cli)

set(MC_SUMMARY_CSV "${CMAKE_CURRENT_BINARY_DIR}/mc_summary.csv")
add_test(
  NAME integration_mc_runner_cli
  COMMAND $<TARGET_FILE:mc_runner> --seed 1 --runs 100 --scenario smoke --output "${MC_SUMMARY_CSV}"
)
add_test(
  NAME integration_mc_runner_cli_csv
  COMMAND
    ${CMAKE_COMMAND}
    -DCSV_FILE=${MC_SUMMARY_CSV}
    -DEXPECTED_HEADER=run,seed,scenario,passed,failure_reason,throttle_bias,final_airspeed_mps,final_altitude_m,max_abs_roll_rad
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_csv.cmake"
)
set_tests_properties(integration_mc_runner_cli_csv PROPERTIES DEPENDS integration_mc_runner_cli)

add_executable(test_telemetry test_telemetry.c)
target_link_libraries(test_telemetry PRIVATE bayek_telemetry)
altair_enable_warnings(test_telemetry)
add_test(NAME integration_telemetry COMMAND test_telemetry)

add_executable(test_vehicle_interface test_vehicle_interface.c)
target_link_libraries(test_vehicle_interface PRIVATE bayek_fsw altair_vehicle bayek_common)
altair_enable_warnings(test_vehicle_interface)
add_test(NAME integration_vehicle_interface COMMAND test_vehicle_interface)
