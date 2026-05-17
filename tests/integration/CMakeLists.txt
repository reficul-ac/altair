function(
    altair_add_csv_header_check
    TEST_NAME
    CSV_FILE
    EXPECTED_HEADER
    DEPENDS_ON
)
    add_test(NAME ${TEST_NAME}
             COMMAND ${CMAKE_COMMAND} -DCSV_FILE=${CSV_FILE} -DEXPECTED_HEADER=${EXPECTED_HEADER}
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/check_csv.cmake"
    )
    set_tests_properties(${TEST_NAME} PROPERTIES DEPENDS ${DEPENDS_ON})
endfunction()

function(
    altair_add_file_compare
    TEST_NAME
    EXPECTED_FILE
    ACTUAL_FILE
    SCRIPT
    DEPENDS_ON
)
    add_test(NAME ${TEST_NAME}
             COMMAND ${CMAKE_COMMAND} -DEXPECTED_FILE=${EXPECTED_FILE} -DACTUAL_FILE=${ACTUAL_FILE}
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}"
    )
    set_tests_properties(${TEST_NAME} PROPERTIES DEPENDS "${DEPENDS_ON}")
endfunction()

function(altair_add_sitl_cruise6dof TEST_NAME OUTPUT_CSV)
    add_test(NAME ${TEST_NAME} COMMAND $<TARGET_FILE:sitl_runner> --scenario cruise6dof ${ARGN}
                                       --output "${OUTPUT_CSV}"
    )
endfunction()

add_executable(test_replay test_replay.c)
target_link_libraries(test_replay PRIVATE altair_vehicle bayek_common)
altair_enable_warnings(test_replay)
add_test(NAME integration_replay COMMAND test_replay)

add_executable(test_sitl_smoke test_sitl_smoke.c)
target_link_libraries(test_sitl_smoke PRIVATE bayek_sim altair_vehicle bayek_common)
altair_enable_warnings(test_sitl_smoke)
add_test(NAME integration_sitl_smoke COMMAND test_sitl_smoke)

set(SITL_SMOKE_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_smoke.csv")
add_test(NAME integration_sitl_runner_cli COMMAND $<TARGET_FILE:sitl_runner> --scenario smoke
                                                  --output "${SITL_SMOKE_CSV}"
)
altair_add_csv_header_check(
    integration_sitl_runner_cli_csv "${SITL_SMOKE_CSV}"
    "step,time_s,mode,motor,aileron,elevator,rudder,airspeed_mps,altitude_m"
    integration_sitl_runner_cli
)

set(SITL_CRUISE6DOF_HEADER
    "step,time_s,mode,motor,aileron,elevator,rudder,rc_throttle,rc_roll,rc_pitch,rc_yaw,gps_fix_valid,lat_deg,lon_deg,pos_n_m,pos_e_m,pos_d_m,vel_n_mps,vel_e_mps,vel_d_mps,roll_rad,pitch_rad,yaw_rad,quat_w,quat_x,quat_y,quat_z,p_rps,q_rps,r_rps,airspeed_mps,altitude_m,accel_x_mps2,accel_y_mps2,accel_z_mps2,force_x_n,force_y_n,force_z_n,moment_x_nm,moment_y_nm,moment_z_nm,pos_ecef_x_m,pos_ecef_y_m,pos_ecef_z_m,vel_ecef_x_mps,vel_ecef_y_mps,vel_ecef_z_mps,trim_active,trim_achieved,trim_failed,trim_iteration_count,trim_residual_norm,mission_loaded,mission_active_wp,mission_wp_count,mission_distance_m"
)

set(SITL_CRUISE6DOF_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof.csv")
altair_add_sitl_cruise6dof(
    integration_sitl_cruise6dof
    "${SITL_CRUISE6DOF_CSV}"
    --initial
    "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini"
    --duration
    10
    --dt
    0.01
)
altair_add_csv_header_check(
    integration_sitl_cruise6dof_csv "${SITL_CRUISE6DOF_CSV}" "${SITL_CRUISE6DOF_HEADER}"
    integration_sitl_cruise6dof
)

add_test(NAME integration_sitl_cruise6dof_trajectory
         COMMAND ${CMAKE_COMMAND} -DCSV_FILE=${SITL_CRUISE6DOF_CSV} -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/check_sitl_trajectory.cmake"
)
set_tests_properties(
    integration_sitl_cruise6dof_trajectory PROPERTIES DEPENDS integration_sitl_cruise6dof
)

set(SITL_CRUISE6DOF_CASE_INITIAL_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_case_initial.csv")
altair_add_sitl_cruise6dof(
    integration_sitl_cruise6dof_case_initial
    "${SITL_CRUISE6DOF_CASE_INITIAL_CSV}"
    --case
    "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_case_initial.ini"
    --duration
    10
    --dt
    0.01
)
altair_add_file_compare(
    integration_sitl_cruise6dof_case_initial_matches_initial
    "${SITL_CRUISE6DOF_CSV}"
    "${SITL_CRUISE6DOF_CASE_INITIAL_CSV}"
    compare_files.cmake
    "integration_sitl_cruise6dof;integration_sitl_cruise6dof_case_initial"
)

set(SITL_CRUISE6DOF_CASE_SIM_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_case_sim.csv")
altair_add_sitl_cruise6dof(
    integration_sitl_cruise6dof_case_sim_params
    "${SITL_CRUISE6DOF_CASE_SIM_CSV}"
    --case
    "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_case_sim_params.ini"
    --duration
    10
    --dt
    0.01
)
altair_add_file_compare(
    integration_sitl_cruise6dof_case_sim_changes_trajectory
    "${SITL_CRUISE6DOF_CSV}"
    "${SITL_CRUISE6DOF_CASE_SIM_CSV}"
    compare_files_not_equal.cmake
    "integration_sitl_cruise6dof;integration_sitl_cruise6dof_case_sim_params"
)

set(SITL_CRUISE6DOF_CASE_MISSION_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_case_mission.csv")
altair_add_sitl_cruise6dof(
    integration_sitl_cruise6dof_case_mission
    "${SITL_CRUISE6DOF_CASE_MISSION_CSV}"
    --case
    "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_case_mission.ini"
    --duration
    1
    --dt
    0.01
)
add_test(NAME integration_sitl_cruise6dof_case_mission_columns
         COMMAND ${CMAKE_COMMAND} -DCSV_FILE=${SITL_CRUISE6DOF_CASE_MISSION_CSV} -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/check_sitl_case_mission.cmake"
)
set_tests_properties(
    integration_sitl_cruise6dof_case_mission_columns
    PROPERTIES DEPENDS integration_sitl_cruise6dof_case_mission
)

add_test(
    NAME integration_sitl_cruise6dof_case_bad
    COMMAND
        ${CMAKE_COMMAND} -DSITL_RUNNER=$<TARGET_FILE:sitl_runner>
        -DCASE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_case_bad.ini -P
        "${CMAKE_CURRENT_SOURCE_DIR}/expect_bad_case.cmake"
)

set(SITL_CRUISE6DOF_CONDITIONS_GPS_CSV
    "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_conditions_gps.csv"
)
add_test(
    NAME integration_sitl_cruise6dof_conditions_gps
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --initial
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --conditions
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_conditions_gps.ini" --duration 0.06 --dt 0.01
        --output "${SITL_CRUISE6DOF_CONDITIONS_GPS_CSV}"
)
add_test(NAME integration_sitl_cruise6dof_conditions_gps_drop
         COMMAND ${CMAKE_COMMAND} -DCSV_FILE=${SITL_CRUISE6DOF_CONDITIONS_GPS_CSV} -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/check_sitl_conditions_gps.cmake"
)
set_tests_properties(
    integration_sitl_cruise6dof_conditions_gps_drop
    PROPERTIES DEPENDS integration_sitl_cruise6dof_conditions_gps
)

set(SITL_CRUISE6DOF_CONDITIONS_BASELINE_CSV
    "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_conditions_baseline.csv"
)
add_test(
    NAME integration_sitl_cruise6dof_conditions_baseline
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --initial
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --duration 0.06 --dt 0.01 --output
        "${SITL_CRUISE6DOF_CONDITIONS_BASELINE_CSV}"
)

set(SITL_CRUISE6DOF_CONDITIONS_PARAM_CSV
    "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_conditions_param.csv"
)
add_test(
    NAME integration_sitl_cruise6dof_conditions_param
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --initial
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --conditions
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_conditions_param.ini" --duration 0.06 --dt 0.01
        --output "${SITL_CRUISE6DOF_CONDITIONS_PARAM_CSV}"
)
add_test(
    NAME integration_sitl_cruise6dof_conditions_param_changes_trajectory
    COMMAND
        ${CMAKE_COMMAND} -DEXPECTED_FILE=${SITL_CRUISE6DOF_CONDITIONS_BASELINE_CSV}
        -DACTUAL_FILE=${SITL_CRUISE6DOF_CONDITIONS_PARAM_CSV} -P
        "${CMAKE_CURRENT_SOURCE_DIR}/compare_files_not_equal.cmake"
)
set_tests_properties(
    integration_sitl_cruise6dof_conditions_param_changes_trajectory
    PROPERTIES
        DEPENDS
        "integration_sitl_cruise6dof_conditions_baseline;integration_sitl_cruise6dof_conditions_param"
)

set(SITL_CRUISE6DOF_CONDITIONS_TRIM_CSV
    "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_conditions_trim.csv"
)
add_test(
    NAME integration_sitl_cruise6dof_conditions_trim
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --conditions
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_conditions_trim.ini" --duration 0.06 --dt 0.01
        --output "${SITL_CRUISE6DOF_CONDITIONS_TRIM_CSV}"
)
add_test(NAME integration_sitl_cruise6dof_conditions_trim_status
         COMMAND ${CMAKE_COMMAND} -DCSV_FILE=${SITL_CRUISE6DOF_CONDITIONS_TRIM_CSV} -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/check_sitl_trim.cmake"
)
set_tests_properties(
    integration_sitl_cruise6dof_conditions_trim_status
    PROPERTIES DEPENDS integration_sitl_cruise6dof_conditions_trim
)

set(SITL_CRUISE6DOF_NED_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_ned.csv")
add_test(
    NAME integration_sitl_cruise6dof_ned
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --frame-mode ned --initial
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --duration 0.05 --dt 0.01 --output
        "${SITL_CRUISE6DOF_NED_CSV}"
)
add_test(
    NAME integration_sitl_cruise6dof_ned_csv
    COMMAND
        ${CMAKE_COMMAND} -DCSV_FILE=${SITL_CRUISE6DOF_NED_CSV}
        -DEXPECTED_HEADER=${SITL_CRUISE6DOF_HEADER} -P
        "${CMAKE_CURRENT_SOURCE_DIR}/check_csv.cmake"
)
set_tests_properties(
    integration_sitl_cruise6dof_ned_csv PROPERTIES DEPENDS integration_sitl_cruise6dof_ned
)

set(SITL_QGC_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_qgc.csv")
add_test(
    NAME integration_sitl_cruise6dof_qgc
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --initial
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --duration 0.02 --dt 0.01 --output
        "${SITL_QGC_CSV}" --qgc
)

set(SITL_MAVLINK_CSV "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_mavlink.csv")
add_test(
    NAME integration_sitl_cruise6dof_mavlink_alias
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --initial
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --duration 0.02 --dt 0.01 --output
        "${SITL_MAVLINK_CSV}" --mavlink --mavlink-port 14551
)

set(SITL_CRUISE6DOF_PROFILES
    cruise
    takeoff
    turn
    descent
    failsafe
    mission
)
foreach(PROFILE IN LISTS SITL_CRUISE6DOF_PROFILES)
    set(SITL_PROFILE_CSV_A "${CMAKE_CURRENT_BINARY_DIR}/sitl_${PROFILE}_a.csv")
    set(SITL_PROFILE_CSV_B "${CMAKE_CURRENT_BINARY_DIR}/sitl_${PROFILE}_b.csv")
    set(SITL_PROFILE_RUN_A "integration_sitl_cruise6dof_${PROFILE}_profile_a")
    set(SITL_PROFILE_RUN_B "integration_sitl_cruise6dof_${PROFILE}_profile_b")
    set(SITL_PROFILE_REPLAY "integration_sitl_cruise6dof_${PROFILE}_profile_replay")
    set(SITL_PROFILE_GUARDRAILS "integration_sitl_cruise6dof_${PROFILE}_profile_guardrails")

    add_test(
        NAME ${SITL_PROFILE_RUN_A}
        COMMAND
            $<TARGET_FILE:sitl_runner> --scenario cruise6dof --profile ${PROFILE} --initial
            "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --duration 2 --dt 0.01 --output
            "${SITL_PROFILE_CSV_A}"
    )
    add_test(
        NAME ${SITL_PROFILE_RUN_B}
        COMMAND
            $<TARGET_FILE:sitl_runner> --scenario cruise6dof --profile ${PROFILE} --initial
            "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --duration 2 --dt 0.01 --output
            "${SITL_PROFILE_CSV_B}"
    )
    add_test(
        NAME ${SITL_PROFILE_REPLAY}
        COMMAND
            ${CMAKE_COMMAND} -DEXPECTED_FILE=${SITL_PROFILE_CSV_A}
            -DACTUAL_FILE=${SITL_PROFILE_CSV_B} -P
            "${CMAKE_CURRENT_SOURCE_DIR}/compare_files.cmake"
    )
    add_test(NAME ${SITL_PROFILE_GUARDRAILS}
             COMMAND ${CMAKE_COMMAND} -DCSV_FILE=${SITL_PROFILE_CSV_A} -DPROFILE=${PROFILE} -P
                     "${CMAKE_CURRENT_SOURCE_DIR}/check_sitl_profile.cmake"
    )
    set_tests_properties(
        ${SITL_PROFILE_REPLAY} PROPERTIES DEPENDS "${SITL_PROFILE_RUN_A};${SITL_PROFILE_RUN_B}"
    )
    set_tests_properties(${SITL_PROFILE_GUARDRAILS} PROPERTIES DEPENDS ${SITL_PROFILE_RUN_A})
endforeach()

set(SITL_FAILSAFE_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sitl_cruise6dof_failsafe_v1.csv")
set(SITL_FAILSAFE_FIXTURE_ACTUAL "${CMAKE_CURRENT_BINARY_DIR}/sitl_cruise6dof_failsafe_fixture.csv")
add_test(
    NAME integration_sitl_cruise6dof_failsafe_fixture_run
    COMMAND
        $<TARGET_FILE:sitl_runner> --scenario cruise6dof --profile failsafe --initial
        "${CMAKE_CURRENT_SOURCE_DIR}/cruise6dof_initial.ini" --duration 1.0 --dt 0.02 --output
        "${SITL_FAILSAFE_FIXTURE_ACTUAL}"
)

set(MC_SUMMARY_CSV "${CMAKE_CURRENT_BINARY_DIR}/mc_summary.csv")
add_test(NAME integration_mc_runner_cli COMMAND $<TARGET_FILE:mc_runner> --seed 1 --runs 100
                                                --scenario smoke --output "${MC_SUMMARY_CSV}"
)
add_test(
    NAME integration_mc_runner_cli_csv
    COMMAND
        ${CMAKE_COMMAND} -DCSV_FILE=${MC_SUMMARY_CSV}
        -DEXPECTED_HEADER=run,seed,scenario,passed,failure_reason,throttle_bias,final_airspeed_mps,final_altitude_m,max_abs_roll_rad
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_csv.cmake"
)
set_tests_properties(integration_mc_runner_cli_csv PROPERTIES DEPENDS integration_mc_runner_cli)
add_test(NAME integration_mc_runner_cli_semantics
         COMMAND ${CMAKE_COMMAND} -DCSV_FILE=${MC_SUMMARY_CSV} -P
                 "${CMAKE_CURRENT_SOURCE_DIR}/check_mc_summary.cmake"
)
set_tests_properties(
    integration_mc_runner_cli_semantics PROPERTIES DEPENDS integration_mc_runner_cli
)

add_executable(test_telemetry test_telemetry.c)
target_link_libraries(test_telemetry PRIVATE bayek_telemetry)
altair_enable_warnings(test_telemetry)
add_test(NAME integration_telemetry COMMAND test_telemetry)

add_executable(test_vehicle_interface test_vehicle_interface.c)
target_link_libraries(test_vehicle_interface PRIVATE altair_vehicle bayek_common)
altair_enable_warnings(test_vehicle_interface)
add_test(NAME integration_vehicle_interface COMMAND test_vehicle_interface)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME integration_sitl_cruise6dof_failsafe_fixture_replay
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/python/compare_sitl_replay.py"
                --expected "${SITL_FAILSAFE_FIXTURE}" --actual "${SITL_FAILSAFE_FIXTURE_ACTUAL}"
    )
    set_tests_properties(
        integration_sitl_cruise6dof_failsafe_fixture_replay
        PROPERTIES DEPENDS integration_sitl_cruise6dof_failsafe_fixture_run
    )

    add_test(
        NAME integration_run_sitl_cruise6dof_summary
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_run_sitl.py"
                "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/run_sitl"
    )
    set_tests_properties(integration_run_sitl_cruise6dof_summary PROPERTIES SKIP_RETURN_CODE 77)

    add_test(
        NAME integration_mavlink_live_bridge
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_mavlink_live_bridge.py"
                "${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME integration_verify_agent_work_selector
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_verify_agent_work.py"
                     "${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME integration_capture_animus_qt_sitl_png_checks
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_capture_animus_qt_sitl.py"
                     "${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME integration_sitl_session
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_sitl_session.py"
                     "${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME integration_plot_sitl_velocities
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_plot_sitl.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/plot_sitl"
    )
    set_tests_properties(integration_plot_sitl_velocities PROPERTIES SKIP_RETURN_CODE 77)

endif()
