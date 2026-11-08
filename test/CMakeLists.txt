# test

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME pytest
    COMMAND Python3::Interpreter -B -m pytest
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(pytest PROPERTIES ENVIRONMENT "NCP=$<TARGET_FILE:ncp>")
