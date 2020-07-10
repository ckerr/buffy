cmake_minimum_required(VERSION 3.15)
project(buffy)
include_directories(include)
add_subdirectory(src)

# buffy
set(CMAKE_C_STANDARD 99)

# the tests
set(CMAKE_CXX_STANDARD 20)

# tests
option(BFY_RUN_TESTS "If ON, Buffy tests will be run." ON)
if(${BFY_RUN_TESTS})
  # enable CTest.  This will set BUILD_TESTING to ON
  # unless otherwise specified on the command line.
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
