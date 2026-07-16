cmake_minimum_required(VERSION 3.20)

# Set the project name and version
project(vt-lb VERSION 1.0 LANGUAGES CXX)

set(
  VT_LB_COMM_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../comm"
  CACHE PATH
  "Path to the standalone comm source tree"
)

# Try to find ccache to speed up compilation
find_program(ccache_binary ccache)
if (ccache_binary)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${ccache_binary}")
endif()

set(MPI_EXTRA_FLAGS "" CACHE STRING "Flags to pass to mpirun/mpiexec")
string(REPLACE " " ";" MPI_EXTRA_FLAGS_LIST "${MPI_EXTRA_FLAGS}")
message(STATUS "Running tests and examples with additional MPI flags = ${MPI_EXTRA_FLAGS_LIST}")

include(cmake/load_mpi_package.cmake)

option(vt_backend_enabled "Whether VT backend is enabled" ON)

if (vt_backend_enabled)
  find_package(vt)
  if (vt_FOUND)
    set(vt_backend_enabled 1)
  else()
    set(vt_backend_enabled 0)
  endif()
else()
  set(vt_backend_enabled 0)
endif()

# vt includes magistrate and we must have magistrate for the MPI backend
if(NOT vt_FOUND)
  set(vt_backend_feature_enabled 0)
  find_package(magistrate REQUIRED)
else()
  set(vt_backend_feature_enabled 1)
endif()

set(FMT_LIBRARY fmt)
add_subdirectory(tpl/fmt)

# json library always included in the build
set(JSON_BuildTests OFF)
set(JSON_MultipleHeaders ON)
set(JSON_LIBRARY nlohmann_json-lb)
add_subdirectory(tpl/json)

# yaml-cpp library always included in the build
set(YAML_LIBRARY yaml-cpp)
add_subdirectory(tpl/yaml-cpp)

# brotli library always included in the build
set(BROTLI_DISABLE_TESTS ON)
# we need to disable bundled mode so it will install properly
set(BROTLI_BUNDLED_MODE OFF)
set(BROTLI_BUILD_PORTABLE ON)
set(BROTLI_BUILD_SHARED_LIBS OFF)
set(BROTLI_LIBRARY brotlicommon-static brotlidec-static brotlienc-static)
add_subdirectory(tpl/brotli)

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

# Compatibility aliases for the target names expected by comm.
if(vt_backend_enabled)
  if(TARGET vt::runtime::checkpoint AND NOT TARGET vt::lib::checkpoint)
    add_library(vt::lib::checkpoint ALIAS vt::runtime::checkpoint)
  endif()
  if(TARGET vt::runtime::vt AND NOT TARGET vt::vt)
    add_library(vt::vt ALIAS vt::runtime::vt)
  endif()
endif()

if(NOT TARGET fmt::fmt)
  set(COMM_FMT_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/comm-fmt-include")
  file(MAKE_DIRECTORY "${COMM_FMT_INCLUDE_DIR}/fmt")
  file(WRITE "${COMM_FMT_INCLUDE_DIR}/fmt/format.h" "#include <fmt-lb/format.h>\n")
  target_include_directories(fmt-lb PUBLIC "$<BUILD_INTERFACE:${COMM_FMT_INCLUDE_DIR}>")
  add_library(fmt::fmt ALIAS fmt-lb)
endif()

add_subdirectory(
  "${VT_LB_COMM_ROOT}/src"
  "${CMAKE_CURRENT_BINARY_DIR}/comm"
)

set(VT_LB_COMM_TEST_DIR "${VT_LB_COMM_ROOT}/tests")

option(VT_LB_TESTS_ENABLED "Build vt-lb with unit tests" ON)

set(PROJECT_BIN_DIR      ${CMAKE_CURRENT_BINARY_DIR})
set(PROJECT_BASE_DIR     ${CMAKE_CURRENT_SOURCE_DIR})
set(PROJECT_LIB_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(PROJECT_EXAMPLE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_subdirectory(src)
add_subdirectory(examples)

include(CTest) #adds option BUILD_TESTING (default ON)
enable_testing()

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
