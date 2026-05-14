if(NOT DEFINED CSV_FILE)
    message(FATAL_ERROR "CSV_FILE is required")
endif()

if(NOT EXISTS "${CSV_FILE}")
    message(FATAL_ERROR "CSV file does not exist: ${CSV_FILE}")
endif()

file(STRINGS "${CSV_FILE}" CSV_ROWS)
list(LENGTH CSV_ROWS CSV_ROW_COUNT)
if(NOT CSV_ROW_COUNT EQUAL 101)
    math(EXPR DATA_ROW_COUNT "${CSV_ROW_COUNT} - 1")
    message(
        FATAL_ERROR "expected 100 Monte Carlo data rows in ${CSV_FILE}, found ${DATA_ROW_COUNT}"
    )
endif()

list(GET CSV_ROWS 0 CSV_HEADER)
set(EXPECTED_HEADER
    "run,seed,scenario,passed,failure_reason,throttle_bias,final_airspeed_mps,final_altitude_m,max_abs_roll_rad"
)
if(NOT CSV_HEADER STREQUAL EXPECTED_HEADER)
    message(FATAL_ERROR "unexpected Monte Carlo CSV header in ${CSV_FILE}: ${CSV_HEADER}")
endif()

set(ROW_INDEX 0)
foreach(CSV_ROW IN LISTS CSV_ROWS)
    if(ROW_INDEX GREATER 0)
        if(NOT
           CSV_ROW
           MATCHES
           "^([0-9]+),([0-9]+),smoke,1,,([-+]?[0-9]+\\.?[0-9]*),([-+]?[0-9]+\\.?[0-9]*),([-+]?[0-9]+\\.?[0-9]*),([-+]?[0-9]+\\.?[0-9]*)$"
        )
            message(
                FATAL_ERROR
                    "unexpected Monte Carlo row format or failed run in ${CSV_FILE}: ${CSV_ROW}"
            )
        endif()

        set(FINAL_AIRSPEED_MPS "${CMAKE_MATCH_4}")
        set(FINAL_ALTITUDE_M "${CMAKE_MATCH_5}")
        set(MAX_ABS_ROLL_RAD "${CMAKE_MATCH_6}")

        if(FINAL_AIRSPEED_MPS LESS 1.0 OR FINAL_AIRSPEED_MPS GREATER 80.0)
            message(FATAL_ERROR "Monte Carlo airspeed gate failed in ${CSV_FILE}: ${CSV_ROW}")
        endif()
        if(FINAL_ALTITUDE_M LESS -100.0 OR FINAL_ALTITUDE_M GREATER 10000.0)
            message(FATAL_ERROR "Monte Carlo altitude gate failed in ${CSV_FILE}: ${CSV_ROW}")
        endif()
        if(MAX_ABS_ROLL_RAD GREATER 1.5708)
            message(FATAL_ERROR "Monte Carlo roll gate failed in ${CSV_FILE}: ${CSV_ROW}")
        endif()
    endif()
    math(EXPR ROW_INDEX "${ROW_INDEX} + 1")
endforeach()
