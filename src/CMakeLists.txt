set(
  TOP_LEVEL_SUBDIRS
  algo algo/baselb algo/temperedlb algo/driver
  comm
  input
  model
)

set(VT_LB_INSTALL_DESTINATION "include/vt-lb")
set(VT_LB_EXTERNAL_DESTINATION "include")

foreach(DIR ${TOP_LEVEL_SUBDIRS})
  install(
    DIRECTORY "vt-lb/${DIR}"
    DESTINATION ${VT_LB_INSTALL_DESTINATION}
    FILES_MATCHING PATTERN "*.h"
  )
endforeach()

file(GLOB TOP_HEADERS "vt-lb/*.h")

install(
  FILES ${TOP_HEADERS}
  DESTINATION ${VT_LB_INSTALL_DESTINATION}
)

file(
  GLOB
  HEADER_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/vt-lb/*.h
)

file(
  GLOB
  SOURCE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/vt-lb/*.cc
)

foreach(SUB_DIR ${TOP_LEVEL_SUBDIRS})
  file(
    GLOB
    "${SUB_DIR}_HEADER_FILES"
    RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/vt-lb/${SUB_DIR}/*.h"
  )

  file(
    GLOB
    "${SUB_DIR}_SOURCE_FILES"
    RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/vt-lb/${SUB_DIR}/*.cc"
  )

  list(
    APPEND
    HEADER_FILES
    "${${SUB_DIR}_HEADER_FILES}"
  )

  list(
    APPEND
    SOURCE_FILES
    "${${SUB_DIR}_SOURCE_FILES}"
  )
endforeach()

message(STATUS "VT_LB_HEADER_FILES: ${HEADER_FILES}")

add_library(
  ${VT_LB_LIBRARY}
  STATIC
  ${HEADER_FILES} ${SOURCE_FILES}
)

add_library(${VT_LB_LIBRARY_NS} ALIAS ${VT_LB_LIBRARY})

include(turn_on_warnings)
turn_on_warnings(${VT_LB_LIBRARY})

# target_link_libraries(
#   ${VT_LB_LIBRARY} PUBLIC ${JSON_LIBRARY}
# )

# target_link_libraries(
#   ${VT_LB_LIBRARY} PUBLIC ${FMT_LIBRARY}
# )

# target_link_libraries(
#   ${VT_LB_LIBRARY} PUBLIC ${BROTLI_LIBRARY}
# )

target_link_libraries(
  ${VT_LB_LIBRARY} PUBLIC MPI::MPI_CXX
)

target_link_libraries(
  ${VT_LB_LIBRARY} PUBLIC vt::lib::magistrate
)
target_link_libraries(
  ${VT_LB_LIBRARY} PUBLIC vt::runtime::vt
)

target_include_directories(
  ${VT_LB_LIBRARY} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_include_directories(
  ${VT_LB_LIBRARY} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

# add_vtlb_definitions(${VT_LB_LIBRARY})

install(
  TARGETS                   ${VT_LB_LIBRARY}
  EXPORT                    ${VT_LB_LIBRARY}
  LIBRARY DESTINATION       lib
  ARCHIVE DESTINATION       lib
  RUNTIME DESTINATION       bin
  INCLUDES DESTINATION      ${VT_LB_EXTERNAL_DESTINATION}
)

install(
  EXPORT                    ${VT_LB_LIBRARY}
  DESTINATION               cmake
  FILE                      "vtLBTargets.cmake"
  NAMESPACE                 vt::lib::
  COMPONENT                 runtime
)

# install(TARGETS ${FMT_LIBRARY} EXPORT ${VT_LB_LIBRARY})
# install(TARGETS ${JSON_LIBRARY} EXPORT ${VT_LB_LIBRARY})
# install(TARGETS ${BROTLI_LIBRARY} EXPORT ${VT_LB_LIBRARY})
# install(TARGETS ${YAML_LIBRARY} EXPORT ${VT_LB_LIBRARY})

export(
  TARGETS                   ${VT_LB_LIBRARY}
                            # ${FMT_LIBRARY}
                            # ${JSON_LIBRARY}
                            # ${BROTLI_LIBRARY}
                            # ${YAML_LIBRARY}
  FILE                      "vtLBTargets.cmake"
  NAMESPACE                 vt::lib::
)
