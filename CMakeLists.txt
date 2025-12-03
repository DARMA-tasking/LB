cmake_minimum_required(VERSION 3.20)

# Set the project name and version
project(vt-lb VERSION 1.0 LANGUAGES CXX)

# Try to find ccache to speed up compilation
find_program(ccache_binary ccache)
if (ccache_binary)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${ccache_binary}")
endif()

find_package(MPI REQUIRED)
find_package(vt)

# vt includes magistrate
#if(NOT vt_FOUND)
  find_package(magistrate REQUIRED)
#endif()

file(
  GLOB
  HEADER_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

set(VT_LB_LIBRARY vt-lb CACHE INTERNAL "" FORCE)
set(VT_LB_LIBRARY_NS vt::lib::vt-lb)

set(
  CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/"
)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
endif()
message(STATUS "CMAKE_CXX_STANDARD: ${CMAKE_CXX_STANDARD}")

option(VT_LB_TESTS_ENABLED "Build vt-lb with unit tests" ON)

set(PROJECT_BIN_DIR      ${CMAKE_CURRENT_BINARY_DIR})
set(PROJECT_BASE_DIR     ${CMAKE_CURRENT_SOURCE_DIR})
set(PROJECT_LIB_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(PROJECT_EXAMPLE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_subdirectory(src)
add_subdirectory(examples)

include(CTest) #adds option BUILD_TESTING (default ON)

message(STATUS "VT_LB_TESTS_ENABLED: ${VT_LB_TESTS_ENABLED}")
if (BUILD_TESTING AND VT_LB_TESTS_ENABLED)
  set(CTEST_SOURCE_DIRECTORY ${SOURCE_DIR}/src)
  add_custom_target(unit_tests)
  add_subdirectory(tests)
endif()

configure_file(
  cmake/vtLBConfig.cmake.in
  "${PROJECT_BINARY_DIR}/vtLBConfig.cmake" @ONLY
)

install(
  FILES        "${PROJECT_BINARY_DIR}/vtLBConfig.cmake"
  DESTINATION  cmake
  COMPONENT    lib
)
