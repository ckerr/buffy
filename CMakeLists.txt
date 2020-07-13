cmake_minimum_required(VERSION 3.10)

# Set policy for setting the MSVC runtime library for static MSVC builds
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(buffy)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)


if(MSVC)
  if(NOT CMAKE_VERSION VERSION_LESS "3.15.0")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "/MT")
    set(CMAKE_C_FLAGS_DEBUG "/MTd")
    set(CMAKE_C_FLAGS_RELEASE "/MT")
  endif()
endif()

include_directories(include)
add_subdirectory(src)

# tests
option(INSTALL_GTEST OFF)
option(BFY_RUN_TESTS "If ON, Buffy tests will be run." ON)
if(${BFY_RUN_TESTS})
  # enable CTest.  This will set BUILD_TESTING to ON
  # unless otherwise specified on the command line.
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(DIRECTORY include/buffy DESTINATION include)
