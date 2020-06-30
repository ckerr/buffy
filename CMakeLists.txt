cmake_minimum_required(VERSION 3.10)
project(buffy)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

include_directories(include)

add_subdirectory(src)


option(BFY_RUN_TESTS "If ON, Buffy tests will be run." ON)

if(${BFY_RUN_TESTS})
  # enable CTest.  This will set BUILD_TESTING to ON
  # unless otherwise specified on the command line.
  include(CTest)
  enable_testing()

  #  check_target(gtest)
  #check_target(gtest_main)
  list(APPEND BFY_TEST_COMMON_LIBRARIES
      gtest_main
      gtest
      ${CMAKE_THREAD_LIBS_INIT}
  ) 

  add_subdirectory(tests)
endif()
