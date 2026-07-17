/*
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia, LLC
// SPDX-License-Identifier: BSD-3-Clause
*/

#if !defined INCLUDED_VT_LB_UNIT_TEST_PARALLEL_HARNESS_H
#define INCLUDED_VT_LB_UNIT_TEST_PARALLEL_HARNESS_H

#include <comm/comm/MPI/comm_mpi.h>
#include <comm/comm/comm_traits.h>
#include <comm/config/cmake_config.h>

#if vt_backend_enabled
#include <comm/comm/vt/comm_vt.h>
#endif

#include <gtest/gtest.h>
#include <mpi.h>

#include <exception>
#include <string>
#include <type_traits>
#include <vector>

namespace vt_lb::tests::unit {

extern int test_argc;
extern char** test_argv;

template <comm::Communicator CommType>
struct TestParallelHarness : testing::Test {
  void SetUp() override {
    int initialized = 0;
    MPI_Initialized(&initialized);
    if (!initialized) {
      MPI_Init(&test_argc, &test_argv);
    }

    args_.assign(test_argv, test_argv + test_argc);
    args_.push_back(nullptr);
    auto argc = static_cast<int>(args_.size()) - 1;
    auto argv = args_.data();
    comm.init(argc, argv, MPI_COMM_WORLD);
  }

  void TearDown() override {
    try {
      while (comm.poll()) {
      }
    } catch (std::exception const& e) {
      ADD_FAILURE() << "Communicator polling failed: " << e.what();
    }

    comm.finalize();
  }

  CommType comm;

private:
  std::vector<char*> args_;
};

struct CommNameGenerator {
  template <comm::Communicator CommType>
  static std::string GetName(int) {
    if constexpr (std::is_same_v<CommType, comm::CommMPI>) {
      return "CommMPI";
    }
#if vt_backend_enabled
    if constexpr (std::is_same_v<CommType, comm::CommVT>) {
      return "CommVT";
    }
#endif
    return "Unrecognized";
  }
};

using CommTypesForTesting = ::testing::Types<
  comm::CommMPI
#if vt_backend_enabled
  , comm::CommVT
#endif
>;

} // namespace vt_lb::tests::unit

#endif /* INCLUDED_VT_LB_UNIT_TEST_PARALLEL_HARNESS_H */
