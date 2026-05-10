add_executable(test_math test_math.c)
target_link_libraries(test_math PRIVATE bayek_common)
add_test(NAME unit_math COMMAND test_math)

add_executable(test_control test_control.c)
target_link_libraries(test_control PRIVATE bayek_common)
add_test(NAME unit_control COMMAND test_control)

add_executable(test_mixer test_mixer.c)
target_link_libraries(test_mixer PRIVATE altair_vehicle bayek_common)
add_test(NAME unit_mixer COMMAND test_mixer)
