
#
#  Load and discover MPI settings (required)
#

find_package(MPI REQUIRED)
if(MPI_FOUND)
  # do nothing
else()
  message(FATAL_ERROR "Failure to locate MPI: MPI is required for LB to build")
endif(MPI_FOUND)


# Set default command for invoking MPI (mpirun) and flag for MPI nprocs
set(MPI_RUN_COMMAND  "${MPIEXEC_EXECUTABLE}")
set(MPI_PRE_FLAGS    "${MPIEXEC_PREFLAGS}")
set(MPI_EPI_FLAGS    "${MPIEXEC_POSTFLAGS}")
set(MPI_NUMPROC_FLAG "${MPIEXEC_NUMPROC_FLAG}")

set(cmake_detected_max_num_nodes ${MPIEXEC_MAX_NUMPROCS})

set(MPI_MAX_NUMPROC "${MPIEXEC_MAX_NUMPROCS}")

if(lb_tests_num_nodes)
  set(MPI_MAX_NUMPROC "${lb_tests_num_nodes}")
endif()

if(${MPI_MAX_NUMPROC} GREATER ${MPIEXEC_MAX_NUMPROCS})
  message(STATUS "Oversubscribing number of nodes to ${MPI_MAX_NUMPROC} with detected ${MPIEXEC_MAX_NUMPROCS}")
endif()

message(STATUS "MPI max nproc: ${MPI_MAX_NUMPROC}")

function(build_mpi_proc_test_list)
  if (ARGC LESS 1)
    message(FATAL_ERROR "no arguments supplied to build_mpi_proc_test_list")
  endif()

  set(noValOption)
  set(singleValArg MAX_PROC VARIABLE_OUT )
  set(multiValueArgs)
  set(allKeywords ${noValOption} ${singleValArg} ${multiValueArgs})
  cmake_parse_arguments(
    ARG "${noValOption}" "${singleValArg}" "${multiValueArgs}" ${ARGN}
  )

  # Stop the configuration if there are any unparsed arguments
  if (ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "found unparsed arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()

  if (NOT DEFINED ARG_MAX_PROC)
    # Default to 8 processors
    set(ARG_MAX_PROC "8")
  elseif(${ARG_MAX_PROC} LESS "2")
    # The min number of processors allowed in the list is 2
    set(ARG_MAX_PROC "2")
  endif()

  if (NOT DEFINED ${ARG_VARIABLE_OUT})
    message(
      FATAL_ERROR
      "Out variable where string is generated must be defined (VARIABLE_OUT)"
    )
  endif()

  set(CUR_N_PROC "1")
  set(CUR_PROC_LIST "")
  while(CUR_N_PROC LESS_EQUAL "${ARG_MAX_PROC}")
    #message("${CUR_N_PROC}")
    list(APPEND CUR_PROC_LIST ${CUR_N_PROC})
    math(EXPR NEW_VAL "${CUR_N_PROC} * 2")
    set(CUR_N_PROC "${NEW_VAL}")
  endwhile()

  set(${ARG_VARIABLE_OUT} "${CUR_PROC_LIST}" PARENT_SCOPE)
endfunction()

set(PROC_TEST_LIST "")
build_mpi_proc_test_list(
  MAX_PROC       ${MPI_MAX_NUMPROC}
  VARIABLE_OUT   PROC_TEST_LIST
)

message(STATUS "MPI proc test list: ${PROC_TEST_LIST}")
message(STATUS "MPI exec: ${MPIEXEC_EXECUTABLE}")