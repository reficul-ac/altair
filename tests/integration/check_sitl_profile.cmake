if(NOT DEFINED CSV_FILE)
    message(FATAL_ERROR "CSV_FILE is required")
endif()
if(NOT DEFINED PROFILE)
    message(FATAL_ERROR "PROFILE is required")
endif()

file(STRINGS "${CSV_FILE}" CSV_ROWS)
list(LENGTH CSV_ROWS CSV_ROW_COUNT)
if(CSV_ROW_COUNT LESS 4)
    message(FATAL_ERROR "CSV file has too few data rows: ${CSV_FILE}")
endif()

math(EXPR MID_INDEX "${CSV_ROW_COUNT} / 2")
math(EXPR LAST_INDEX "${CSV_ROW_COUNT} - 1")
set(NUMBER_RE "^-?[0-9]+(\\.[0-9]+)?([eE][-+]?[0-9]+)?$")

foreach(ROW_INDEX RANGE 1 ${LAST_INDEX})
    list(GET CSV_ROWS ${ROW_INDEX} ROW)
    string(REPLACE "," ";" FIELDS "${ROW}")
    list(LENGTH FIELDS FIELD_COUNT)
    if(NOT FIELD_COUNT EQUAL 51)
        message(FATAL_ERROR "expected 51 CSV fields in ${CSV_FILE}, got ${FIELD_COUNT}: ${ROW}")
    endif()
    foreach(FIELD IN LISTS FIELDS)
        if(NOT FIELD MATCHES "${NUMBER_RE}")
            message(FATAL_ERROR "non-numeric CSV field in ${CSV_FILE}: ${FIELD}")
        endif()
    endforeach()

    list(GET FIELDS 2 MODE)
    list(GET FIELDS 3 MOTOR)
    list(GET FIELDS 4 AILERON)
    list(GET FIELDS 5 ELEVATOR)
    list(GET FIELDS 6 RUDDER)
    list(GET FIELDS 20 ROLL)
    list(GET FIELDS 21 PITCH)
    list(GET FIELDS 27 P_RATE)
    list(GET FIELDS 28 Q_RATE)
    list(GET FIELDS 29 R_RATE)
    list(GET FIELDS 30 AIRSPEED)
    list(GET FIELDS 31 ALTITUDE)

    if(MODE LESS 0 OR MODE GREATER 4)
        message(FATAL_ERROR "mode out of range in ${CSV_FILE}: ${MODE}")
    endif()
    if(MOTOR LESS 0.0 OR MOTOR GREATER 1.0)
        message(FATAL_ERROR "motor out of range in ${CSV_FILE}: ${MOTOR}")
    endif()
    if(AILERON LESS -1.0
       OR AILERON GREATER 1.0
       OR ELEVATOR LESS -1.0
       OR ELEVATOR GREATER 1.0
       OR RUDDER LESS -1.0
       OR RUDDER GREATER 1.0
    )
        message(
            FATAL_ERROR
                "surface output out of range in ${CSV_FILE}: ${AILERON},${ELEVATOR},${RUDDER}"
        )
    endif()
    if(AIRSPEED LESS 0.1 OR AIRSPEED GREATER 80.0)
        message(FATAL_ERROR "airspeed outside broad guardrail in ${CSV_FILE}: ${AIRSPEED}")
    endif()
    if(ALTITUDE LESS -100.0 OR ALTITUDE GREATER 2000.0)
        message(FATAL_ERROR "altitude outside broad guardrail in ${CSV_FILE}: ${ALTITUDE}")
    endif()
    if(ROLL LESS -1.8
       OR ROLL GREATER 1.8
       OR PITCH LESS -1.2
       OR PITCH GREATER 1.2
    )
        message(
            FATAL_ERROR
                "attitude outside broad guardrail in ${CSV_FILE}: roll=${ROLL} pitch=${PITCH}"
        )
    endif()
    if(P_RATE LESS -20.0
       OR P_RATE GREATER 20.0
       OR Q_RATE LESS -20.0
       OR Q_RATE GREATER 20.0
       OR R_RATE LESS -20.0
       OR R_RATE GREATER 20.0
    )
        message(
            FATAL_ERROR
                "body rates outside broad guardrail in ${CSV_FILE}: ${P_RATE},${Q_RATE},${R_RATE}"
        )
    endif()
endforeach()

list(GET CSV_ROWS 1 FIRST_ROW)
list(GET CSV_ROWS ${MID_INDEX} MID_ROW)
list(GET CSV_ROWS ${LAST_INDEX} LAST_ROW)
string(REPLACE "," ";" FIRST_FIELDS "${FIRST_ROW}")
string(REPLACE "," ";" MID_FIELDS "${MID_ROW}")
string(REPLACE "," ";" LAST_FIELDS "${LAST_ROW}")

list(GET FIRST_FIELDS 8 FIRST_ROLL)
list(GET MID_FIELDS 8 MID_ROLL)
list(GET LAST_FIELDS 2 LAST_MODE)
list(GET LAST_FIELDS 3 LAST_MOTOR)
list(GET LAST_FIELDS 4 LAST_AILERON)
list(GET LAST_FIELDS 5 LAST_ELEVATOR)
list(GET LAST_FIELDS 6 LAST_RUDDER)
list(GET LAST_FIELDS 11 LAST_GPS_FIX_VALID)
list(GET LAST_FIELDS 31 LAST_ALTITUDE)
list(GET FIRST_FIELDS 47 FIRST_MISSION_LOADED)
list(GET FIRST_FIELDS 48 FIRST_MISSION_ACTIVE)
list(GET FIRST_FIELDS 49 FIRST_MISSION_COUNT)
list(GET LAST_FIELDS 47 LAST_MISSION_LOADED)
list(GET LAST_FIELDS 48 LAST_MISSION_ACTIVE)
list(GET LAST_FIELDS 49 LAST_MISSION_COUNT)

if(PROFILE STREQUAL "turn")
    if(NOT FIRST_ROLL STREQUAL "0.000000")
        message(FATAL_ERROR "turn profile should begin wings-level, got rc_roll=${FIRST_ROLL}")
    endif()
    if(NOT MID_ROLL GREATER 0.20)
        message(
            FATAL_ERROR "turn profile should command positive roll mid-run, got rc_roll=${MID_ROLL}"
        )
    endif()
endif()
if(PROFILE STREQUAL "mission")
    if(NOT FIRST_MISSION_LOADED EQUAL 1 OR NOT FIRST_MISSION_COUNT EQUAL 3)
        message(
            FATAL_ERROR
                "mission profile should load three altitude-step waypoints, got loaded=${FIRST_MISSION_LOADED} count=${FIRST_MISSION_COUNT}"
        )
    endif()
    if(NOT LAST_MODE EQUAL 4)
        message(FATAL_ERROR "mission profile should remain in mission mode, got last=${LAST_MODE}")
    endif()
    if(NOT FIRST_MISSION_ACTIVE GREATER 0)
        message(
            FATAL_ERROR
                "mission profile should advance past the launch waypoint immediately, got first active=${FIRST_MISSION_ACTIVE}"
        )
    endif()
    if(NOT LAST_MISSION_LOADED EQUAL 1 OR NOT LAST_MISSION_COUNT EQUAL 3 OR NOT LAST_MISSION_ACTIVE EQUAL 2)
        message(
            FATAL_ERROR
                "mission profile should keep the altitude-step mission loaded at the climb waypoint, got loaded=${LAST_MISSION_LOADED} count=${LAST_MISSION_COUNT} active=${LAST_MISSION_ACTIVE}"
        )
    endif()
    if(NOT LAST_ALTITUDE GREATER 170.0)
        message(
            FATAL_ERROR
                "mission profile should begin climbing toward the higher waypoint, got altitude=${LAST_ALTITUDE}"
        )
    endif()
endif()
if(PROFILE STREQUAL "failsafe")
    if(NOT LAST_MODE EQUAL 3)
        message(FATAL_ERROR "failsafe profile should enter failsafe mode, got last=${LAST_MODE}")
    endif()
    if(NOT LAST_MOTOR STREQUAL "0.000000"
       OR NOT LAST_AILERON STREQUAL "0.000000"
       OR NOT LAST_ELEVATOR STREQUAL "0.000000"
       OR NOT LAST_RUDDER STREQUAL "0.000000"
    )
        message(
            FATAL_ERROR
                "failsafe profile should command safe outputs, got ${LAST_MOTOR},${LAST_AILERON},${LAST_ELEVATOR},${LAST_RUDDER}"
        )
    endif()
    if(NOT LAST_GPS_FIX_VALID EQUAL 0)
        message(
            FATAL_ERROR
                "failsafe profile should end with gps_fix_valid=0, got ${LAST_GPS_FIX_VALID}"
        )
    endif()
endif()
