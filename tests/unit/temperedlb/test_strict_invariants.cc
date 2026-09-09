/*
//@HEADER
// *****************************************************************************
//
//                          test_strict_invariants.cc
//                 DARMA/vt-lb => Virtual Transport/Load Balancers
//
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact darma@sandia.gov
//
// *****************************************************************************
//@HEADER
*/

#include <vector>

#include <gtest/gtest.h>

#include "test_parallel_harness.h"
#include "test_helpers.h"
#include "temperedlb/random_shared_block_problem.h"
#include "temperedlb/strict_invariants.h"
#include "temperedlb/toy_memory_problem.h"

namespace vt_lb::tests::unit {

namespace {

vt_lb::algo::temperedlb::Configuration makeStrictConfig(
  int num_ranks, double delta
) {
  vt_lb::algo::temperedlb::Configuration config(num_ranks);
  config.cluster_based_on_shared_blocks_ = true;
  config.cluster_transfer_strategy_ =
    vt_lb::algo::temperedlb::ClusterTransferStrategy::StrictSharedBlock;
  config.work_model_.delta = delta;
  config.work_model_.has_memory_info = true;
  config.work_model_.has_task_serialized_memory_info = false;
  config.work_model_.has_task_working_memory_info = false;
  config.work_model_.has_task_footprint_memory_info = false;
  config.work_model_.has_shared_block_memory_info = true;
  config.deterministic_ = true;
  config.seed_ = 1;
  return config;
}

/// Properties that must hold whatever the balancer decides
void expectInvariants(
  StrictInvariantReport const& report,
  std::size_t allowed_blocks,
  double allowed_bytes
) {
  EXPECT_TRUE(report.tasks_partitioned)
    << "placed " << report.final_task_count << " of "
    << report.initial_task_count << " tasks";
  EXPECT_NEAR(report.final_total_load, report.initial_total_load, 1e-9)
    << "load was created or destroyed";
  EXPECT_LE(report.final_max_load, report.initial_max_load + 1e-9)
    << "balancing made the busiest rank busier";
  EXPECT_LE(report.max_blocks_on_a_rank, allowed_blocks)
    << "a rank exceeded its shared block budget";
  EXPECT_LE(report.max_block_bytes_on_a_rank, allowed_bytes + 1e-9)
    << "a rank exceeded its memory budget";
  EXPECT_EQ(report.pinned_tasks_moved, 0u)
    << "a task marked non-migratable was moved";
}

} // namespace

template <typename CommT>
struct TestStrictInvariants : TestParallelHarness<CommT> {};

TYPED_TEST_SUITE_P(TestStrictInvariants);

// A single fixture cannot cover a protocol whose outcome depends on message
// arrival order, so sweep many shapes and assert the properties instead of
// specific placements.
TYPED_TEST_P(TestStrictInvariants, randomized_problems_hold_every_invariant) {
  auto const num_ranks = this->comm.numRanks();

  for (int seed = 1; seed <= 8; ++seed) {
    RandomProblemSpec spec;
    spec.blocks_per_rank = 2 + (seed % 3);
    spec.max_tasks_per_block = 2 + (seed % 4);
    spec.max_blocks_per_rank = spec.blocks_per_rank * 2;

    auto pd = makeRandomSharedBlockProblem(
      this->comm.getRank(), num_ranks, seed, spec
    );
    auto const allowed =
      capBudgetAtInitialMax(this->comm, pd, spec.block_bytes, 1);
    auto const report = runAndMeasure(
      this->comm, makeStrictConfig(num_ranks, 6.25e-9), pd
    );

    SCOPED_TRACE("seed " + std::to_string(seed));
    expectInvariants(report, allowed, allowed * spec.block_bytes);
  }
}

// Off-home blocks costing nothing removes the pressure that kept ranks under
// their budget by accident, which is how the budget bug first showed up.
TYPED_TEST_P(TestStrictInvariants, load_only_still_respects_the_memory_budget) {
  auto const num_ranks = this->comm.numRanks();

  for (int seed = 1; seed <= 8; ++seed) {
    RandomProblemSpec spec;
    spec.blocks_per_rank = 3;
    spec.max_tasks_per_block = 2 + (seed % 4);
    // Only one block of slack per rank, so the budget actually binds
    spec.max_blocks_per_rank = spec.blocks_per_rank + 1;

    auto pd = makeRandomSharedBlockProblem(
      this->comm.getRank(), num_ranks, seed, spec
    );
    auto const allowed =
      capBudgetAtInitialMax(this->comm, pd, spec.block_bytes, 1);
    auto const report = runAndMeasure(
      this->comm, makeStrictConfig(num_ranks, 0.0), pd
    );

    SCOPED_TRACE("seed " + std::to_string(seed));
    expectInvariants(report, allowed, allowed * spec.block_bytes);
  }
}

// A budget that admits exactly what a rank already homes permits no arrivals
TYPED_TEST_P(TestStrictInvariants, an_exactly_full_budget_admits_no_blocks) {
  auto const num_ranks = this->comm.numRanks();

  RandomProblemSpec spec;
  spec.blocks_per_rank = 3;
  spec.max_blocks_per_rank = 3;

  auto pd = makeRandomSharedBlockProblem(
    this->comm.getRank(), num_ranks, 4242, spec
  );
  auto const allowed = capBudgetAtInitialMax(this->comm, pd, spec.block_bytes, 0);
  auto const report = runAndMeasure(
    this->comm, makeStrictConfig(num_ranks, 0.0), pd
  );

  expectInvariants(report, allowed, allowed * spec.block_bytes);
}

// A block whose tasks start on several ranks gives each of them a cluster for
// that block, so two clusters for one block can meet on one rank. Accounting
// for the block twice there is what let a rank overrun its budget.
TYPED_TEST_P(TestStrictInvariants, blocks_spanning_ranks_are_counted_once) {
  auto const num_ranks = this->comm.numRanks();

  for (int seed = 1; seed <= 20; ++seed) {
    RandomProblemSpec spec;
    spec.blocks_span_ranks = true;
    spec.blocks_per_rank = 6;
    spec.min_tasks_per_block = 2;
    spec.max_tasks_per_block = 5;

    auto pd = makeRandomSharedBlockProblem(
      this->comm.getRank(), num_ranks, seed, spec
    );
    auto const allowed =
      capBudgetAtInitialMax(this->comm, pd, spec.block_bytes, 0);
    auto const report = runAndMeasure(
      this->comm, makeStrictConfig(num_ranks, 0.0), pd
    );

    SCOPED_TRACE("seed " + std::to_string(seed));
    expectInvariants(report, allowed, allowed * spec.block_bytes);
  }
}

// Clustering used to ignore isMigratable entirely, so a pinned task rode along
// with whatever cluster it happened to be grouped into.
TYPED_TEST_P(TestStrictInvariants, pinned_tasks_never_move) {
  auto const num_ranks = this->comm.numRanks();

  for (int seed = 1; seed <= 8; ++seed) {
    RandomProblemSpec spec;
    spec.blocks_per_rank = 3;
    spec.min_tasks_per_block = 2;
    spec.max_tasks_per_block = 5;
    // Pins one task in most blocks, so whole clusters are constrained
    spec.pin_every_nth_task = 3;

    auto pd = makeRandomSharedBlockProblem(
      this->comm.getRank(), num_ranks, seed, spec
    );
    auto const allowed =
      capBudgetAtInitialMax(this->comm, pd, spec.block_bytes, 1);
    auto const report = runAndMeasure(
      this->comm, makeStrictConfig(num_ranks, 0.0), pd
    );

    SCOPED_TRACE("seed " + std::to_string(seed));
    expectInvariants(report, allowed, allowed * spec.block_bytes);
  }
}

// Blocks are indivisible, so the best reachable maximum is the best packing of
// whole blocks. Comparing against that exhaustive optimum says whether the
// search is actually any good, not merely that it broke nothing.
TYPED_TEST_P(TestStrictInvariants, results_stay_near_the_exhaustive_optimum) {
  auto const num_ranks = this->comm.numRanks();
  if (num_ranks < 2) {
    GTEST_SKIP() << "nothing to balance on a single rank";
  }

  for (int seed = 1; seed <= 6; ++seed) {
    RandomProblemSpec spec;
    // Small enough that the exhaustive packing stays cheap
    spec.blocks_per_rank = 2;
    spec.min_tasks_per_block = 1;
    spec.max_tasks_per_block = 4;

    auto pd = makeRandomSharedBlockProblem(
      this->comm.getRank(), num_ranks, seed, spec
    );
    auto const allowed =
      capBudgetAtInitialMax(this->comm, pd, spec.block_bytes, 2);
    auto const report = runAndMeasure(
      this->comm, makeStrictConfig(num_ranks, 0.0), pd
    );

    SCOPED_TRACE("seed " + std::to_string(seed));
    expectInvariants(report, allowed, allowed * spec.block_bytes);

    auto const optimal = computeWholeBlockOptimalMaxLoad(
      report.block_loads, num_ranks, static_cast<int>(allowed)
    );
    EXPECT_GE(report.final_max_load, optimal - 1e-9)
      << "beat the whole-block optimum, so a block must have been split";
    // Measured at exactly the optimum on every sampled shape; the margin is
    // headroom for shapes this sweep does not reach
    EXPECT_LE(report.final_max_load, optimal * 1.10)
      << "left more than 10% on the table against the exhaustive optimum";
  }
}

REGISTER_TYPED_TEST_SUITE_P(
  TestStrictInvariants,
  randomized_problems_hold_every_invariant,
  load_only_still_respects_the_memory_budget,
  an_exactly_full_budget_admits_no_blocks,
  blocks_spanning_ranks_are_counted_once,
  pinned_tasks_never_move,
  results_stay_near_the_exhaustive_optimum
);

INSTANTIATE_TYPED_TEST_SUITE_P(
  Comms, TestStrictInvariants, CommTypesForTesting, CommNameGenerator
);

} /* end namespace vt_lb::tests::unit */
