find_package(Doxygen)

if (DOXYGEN_FOUND)
  set(doxygen_in ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
  set(doxygen_out ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

  set(DOXYGEN_PROJECT_NAME ${PROJECT_NAME})
  set(DOXYGEN_DOCS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/docs/")
  set(DOXYGEN_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/")
  set(DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs/")

  configure_file(${doxygen_in} ${doxygen_out} @ONLY)
  configure_file(${doxygen_in}-mcss ${doxygen_out}-mcss @ONLY)
  message(STATUS "LB doxygen build started")

  add_custom_target(
    docs ALL
    COMMAND ${DOXYGEN_EXECUTABLE} ${doxygen_out}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "LB generating API documentation with Doxygen"
  )
else()
  message(FATAL_ERROR "Doxygen could not be found")
endif()
