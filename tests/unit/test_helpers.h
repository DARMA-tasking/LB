/*
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia, LLC
// SPDX-License-Identifier: BSD-3-Clause
*/

#if !defined INCLUDED_VT_LB_TESTS_UNIT_TEST_HELPERS_H
#define INCLUDED_VT_LB_TESTS_UNIT_TEST_HELPERS_H

#include <gtest/gtest.h>
#include <fmt/format.h>

namespace vt_lb::tests::unit {

/**
 * Used for tests that shouldn't run on less than 'min_req_num_nodes' nodes
 */
#define SET_MIN_NUM_NODES_CONSTRAINT(min_req_num_nodes)                    \
{                                                                          \
  auto const num_nodes = this->comm.numRanks();                            \
  if (num_nodes < min_req_num_nodes) {                                     \
    GTEST_SKIP() << fmt::format(                                           \
      "Skipping the run on {} nodes. This test should run on at least {} " \
      "nodes!\n",                                                          \
      num_nodes, min_req_num_nodes                                         \
    );                                                                     \
  }                                                                        \
}

/**
 * Used for tests that should only run on 'req_num_nodes'
 */
#define SET_NUM_NODES_CONSTRAINT(req_num_nodes)                            \
{                                                                          \
  auto const num_nodes = this->comm.numRanks();                            \
  if (num_nodes != req_num_nodes) {                                        \
    GTEST_SKIP() << fmt::format(                                           \
      "Skipping the run on {} nodes. This test should run only on {} "     \
      "nodes!\n",                                                          \
      num_nodes, req_num_nodes                                             \
    );                                                                     \
  }                                                                        \
}

} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_TESTS_UNIT_TEST_HELPERS_H*/
