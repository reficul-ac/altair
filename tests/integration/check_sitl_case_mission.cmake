if(NOT DEFINED CSV_FILE)
    message(FATAL_ERROR "CSV_FILE is required")
endif()

file(STRINGS "${CSV_FILE}" CSV_ROWS)
list(LENGTH CSV_ROWS CSV_ROW_COUNT)
if(CSV_ROW_COUNT LESS 2)
    message(FATAL_ERROR "CSV file has too few data rows: ${CSV_FILE}")
endif()

list(GET CSV_ROWS 1 FIRST_ROW)
string(REPLACE "," ";" FIRST_FIELDS "${FIRST_ROW}")
list(LENGTH FIRST_FIELDS FIELD_COUNT)
if(NOT FIELD_COUNT EQUAL 56)
    message(FATAL_ERROR "expected 56 CSV fields in ${CSV_FILE}, got ${FIELD_COUNT}")
endif()

list(GET FIRST_FIELDS 2 FIRST_MODE)
list(GET FIRST_FIELDS 52 FIRST_MISSION_LOADED)
list(GET FIRST_FIELDS 54 FIRST_MISSION_COUNT)

if(NOT FIRST_MODE EQUAL 4)
    message(FATAL_ERROR "case mission should enter mission mode, got mode=${FIRST_MODE}")
endif()
if(NOT FIRST_MISSION_LOADED EQUAL 1 OR NOT FIRST_MISSION_COUNT EQUAL 2)
    message(
        FATAL_ERROR
            "case mission should load two file waypoints, got loaded=${FIRST_MISSION_LOADED} count=${FIRST_MISSION_COUNT}"
    )
endif()
