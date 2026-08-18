/*
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia, LLC
// SPDX-License-Identifier: BSD-3-Clause
*/

#include <gtest/gtest.h>
#include <mpi.h>

namespace vt_lb::tests::unit {

int test_argc = 0;
char** test_argv = nullptr;

} // namespace vt_lb::tests::unit

int main(int argc, char** argv) {
  using namespace vt_lb::tests::unit;

  test_argc = argc;
  test_argv = argv;
  ::testing::InitGoogleTest(&test_argc, test_argv);

  auto const result = RUN_ALL_TESTS();

  int initialized = 0;
  int finalized = 0;
  MPI_Initialized(&initialized);
  MPI_Finalized(&finalized);
  if (initialized && !finalized) {
    MPI_Finalize();
  }

  return result;
}
