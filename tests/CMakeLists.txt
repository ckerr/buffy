include(GoogleTest)

add_subdirectory("${PROJECT_SOURCE_DIR}/extern/googletest" "extern/googletest")

macro(package_add_test TESTNAME)
    # create an exectuable in which the tests will be stored
    add_executable(${TESTNAME} ${ARGN})
    # link the Google test infrastructure, mocking library, and a default main fuction to
    # the test executable.  Remove g_test_main if writing your own main function.
    target_link_libraries(${TESTNAME} gtest gtest_main)
    target_link_libraries(${TESTNAME} ${CMAKE_PROJECT_NAME})
    # gtest_discover_tests replaces gtest_add_tests,
    # see https://cmake.org/cmake/help/v3.10/module/GoogleTest.html for more options to pass to it
    gtest_discover_tests(${TESTNAME}
        # set a working directory so your project root so that you can find test data via paths relative to the project root
        WORKING_DIRECTORY ${PROJECT_DIR}
        PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_DIR}"
    )
    if (MSVC)
        target_compile_options(${TESTNAME} PRIVATE /W4 /WX)
    else()
        target_compile_options(${TESTNAME} PRIVATE -Wall -Wextra -Wshadow)
    endif()

    set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
endmacro()

package_add_test(buffer-test
                 buffer-test.cc)

# ctest -D ExperimentalMemCheck
find_program(MEMORYCHECK_COMMAND valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --error-exitcode=1")
include(CTest)
