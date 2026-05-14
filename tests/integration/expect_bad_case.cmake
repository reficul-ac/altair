if(NOT DEFINED SITL_RUNNER)
    message(FATAL_ERROR "SITL_RUNNER is required")
endif()
if(NOT DEFINED CASE_FILE)
    message(FATAL_ERROR "CASE_FILE is required")
endif()

execute_process(
    COMMAND "${SITL_RUNNER}" --scenario cruise6dof --case "${CASE_FILE}" --duration 0.02 --dt
            0.01
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE STDOUT_TEXT
    ERROR_VARIABLE STDERR_TEXT
)
if(RESULT EQUAL 0)
    message(FATAL_ERROR "bad case unexpectedly succeeded")
endif()
set(COMBINED_TEXT "${STDOUT_TEXT}${STDERR_TEXT}")
if(NOT COMBINED_TEXT MATCHES "line 2")
    message(FATAL_ERROR "bad case did not report line number: ${COMBINED_TEXT}")
endif()
