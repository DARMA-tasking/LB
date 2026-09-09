/*
//@HEADER
// *****************************************************************************
//
//                               test_temperedlb.cc
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

#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <algorithm>

#include "test_parallel_harness.h"
#include "test_helpers.h"
#include "graph_helpers.h"

#include <nlohmann-lb/json.hpp>
#include <fmt-lb/format.h>

#include <vt-lb/algo/temperedlb/temperedlb.h>

#include "temperedlb/lb_run_helpers.h"
#include "temperedlb/toy_memory_run.h"

namespace vt_lb::tests::unit {


// Wrapper that zips a communicator type with a single integer seed
template <typename CommT, int Seed>
struct CommSeedPack {
  using Comm = CommT;
  static constexpr int seed = Seed;
};

// Typed fixture over zipped communicator+seeds; communicator type remains implicit
template <typename Pack>
struct TestTemperedLB: TestParallelHarness<typename Pack::Comm> {};

TYPED_TEST_SUITE_P(TestTemperedLB);

TYPED_TEST_P(TestTemperedLB, test_lb_no_comm_task_counts) {
	auto num_ranks = this->comm.numRanks();
	auto rank = this->comm.getRank();

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  int seed = TypeParam::seed;
  vt_lb::model::PhaseData pd(rank);

  // Generate a random graph without shared blocks and without communication
  bool uniform_task_count = false;
  bool include_comm = false;
  int seed_same_across_ranks = seed;
  int seed_diff_each_rank = 9876 * rank + 1;

  generateGraphWithoutSharedBlocks(
    pd, num_ranks, uniform_task_count, include_comm,
    seed_same_across_ranks, seed_diff_each_rank
  );

  // Sanity: no shared blocks or communications
  EXPECT_EQ(pd.getSharedBlocksMap().size(), 0);
  EXPECT_EQ(pd.getCommunications().size(), 0);

  // Build LB once and get initial/final global distributions
  vt_lb::algo::temperedlb::Configuration config(num_ranks);
  auto summary = runTemperedLB(this->comm, config, pd);

  // Compute initial global task count
  auto const& initial_global = summary.initial;
  std::size_t initial_total = 0;
  for (auto const& [_r, vec] : initial_global) {
    initial_total += vec.size();
  }

  // Validate the global task count is preserved
  auto const& final_global = summary.final;
  std::size_t final_total = 0;
  for (auto const& [_r, vec] : final_global) {
    final_total += vec.size();
  }
  EXPECT_EQ(final_total, initial_total);
};

TYPED_TEST_P(TestTemperedLB, test_significant_load_imbalance_reduction) {
	auto num_ranks = this->comm.numRanks();
	auto rank = this->comm.getRank();

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  int seed = TypeParam::seed;
  vt_lb::model::PhaseData pd(rank);

  // Generate random tasks without shared blocks and without communication
  bool uniform_task_count = false;
  bool include_comm = false;
  int seed_same_across_ranks = seed;
  int seed_diff_each_rank = 13579 * rank + 3;

  generateGraphWithoutSharedBlocks(
    pd, num_ranks, uniform_task_count, include_comm,
    seed_same_across_ranks, seed_diff_each_rank
  );

  // Introduce strong load imbalance: amplify loads on rank 0
  if (rank == 0) {
    for (auto tid : pd.getTaskIds()) {
      auto t = pd.getTask(tid);
      t->setLoad(t->getLoad() * 50.0);
    }
  }

  // Compute local total load before balancing
  double local_before = 0.0;
  for (auto const& [tid, task] : pd.getTasksMap()) {
    local_before += task.getLoad();
  }

  // Compute global before stats (max and avg) via communicator handle
  struct CollectiveDummy { } dummy;
  auto handle = this->comm.template registerInstanceCollective<CollectiveDummy>(&dummy);

  double global_before_max = 0.0;
  handle.reduce(0, MPI_DOUBLE, MPI_MAX, &local_before, &global_before_max, 1);
  handle.broadcast(0, MPI_DOUBLE, &global_before_max, 1);

  double global_before_sum = 0.0;
  handle.reduce(0, MPI_DOUBLE, MPI_SUM, &local_before, &global_before_sum, 1);
  handle.broadcast(0, MPI_DOUBLE, &global_before_sum, 1);
  double global_before_avg = global_before_sum / static_cast<double>(num_ranks);

  // Build LB once and get initial/final global distributions
  vt_lb::algo::temperedlb::Configuration config(num_ranks);
  auto summary = runTemperedLB(this->comm, config, pd);

  // Capture initial global task distribution for preservation checks
  auto const& initial_global = summary.initial;
  std::size_t initial_total = 0;
  std::unordered_set<int> initial_task_set;
  for (auto const& [r, vec] : initial_global) {
    initial_total += vec.size();
    for (auto tid : vec) initial_task_set.insert(static_cast<int>(tid));
  }

  // Gather global mapping of tasks per rank after
  auto const& final_global = summary.final;
  std::size_t final_total = 0;
  std::unordered_set<int> final_task_set;
  for (auto const& [r, vec] : final_global) {
    final_total += vec.size();
    for (auto tid : vec) {
      final_task_set.insert(static_cast<int>(tid));
    }
  }

  // Build a global mapping of task loads: allgather ids and loads via communicator
  std::vector<int> local_ids;
  std::vector<double> local_loads;
  local_ids.reserve(pd.getTasksMap().size());
  local_loads.reserve(pd.getTasksMap().size());
  for (auto const& [tid, task] : pd.getTasksMap()) {
    local_ids.push_back(static_cast<int>(tid));
    local_loads.push_back(task.getLoad());
  }

  auto ids_by_rank = handle.allgather(local_ids.data(), static_cast<int>(local_ids.size()));
  auto loads_by_rank = handle.allgather(local_loads.data(), static_cast<int>(local_loads.size()));

  // Build lookup for task id -> load
  std::unordered_map<int, double> id_to_load;
  for (auto const& [r, vec] : ids_by_rank) {
    auto const& loads_vec = loads_by_rank[r];
    for (std::size_t i = 0; i < vec.size(); ++i) {
      id_to_load.emplace(vec[i], loads_vec[i]);
    }
  }

  // Compute local after load: sum loads for tasks assigned to this rank
  double local_after = 0.0;
  auto it = final_global.find(rank);
  if (it != final_global.end()) {
    for (auto tid : it->second) {
      local_after += id_to_load[static_cast<int>(tid)];
    }
  }

  // Compute global after stats (max and avg) via communicator handle
  double global_after_max = 0.0;
  handle.reduce(0, MPI_DOUBLE, MPI_MAX, &local_after, &global_after_max, 1);
  handle.broadcast(0, MPI_DOUBLE, &global_after_max, 1);

  double global_after_sum = 0.0;
  handle.reduce(0, MPI_DOUBLE, MPI_SUM, &local_after, &global_after_sum, 1);
  handle.broadcast(0, MPI_DOUBLE, &global_after_sum, 1);
  double global_after_avg = global_after_sum / static_cast<double>(num_ranks);

  // Expect improvement: global max work decreases after balancing
  EXPECT_GT(global_before_max, global_before_avg);
  EXPECT_LT(global_after_max, global_before_max);

  // Tasks should be preserved globally: same total and same set of task IDs
  EXPECT_EQ(final_total, initial_total);
  EXPECT_EQ(final_task_set.size(), initial_task_set.size());
  // Verify the sets are identical
  for (auto tid : initial_task_set) {
    EXPECT_TRUE(final_task_set.count(tid) == 1);
  }

  // Average work should remain nearly constant (loads are redistributed)
  EXPECT_NEAR(global_after_avg, global_before_avg, 1e-9);

  // Compute imbalance metric I = max/avg - 1 and check it approaches zero
  double I_before = 0.0;
  if (global_before_avg > 0.0) {
    I_before = (global_before_max / global_before_avg) - 1.0;
  }
  double I_after = 0.0;
  if (global_after_avg > 0.0) {
    I_after = (global_after_max / global_after_avg) - 1.0;
  }
  EXPECT_GT(I_before, 0.0);
  EXPECT_LT(I_after, I_before);

  // Tasks are indivisible, so no arrangement can beat one rank holding the
  // single largest task. That bound tightens the more ranks there are, which a
  // fixed tolerance calibrated at two ranks does not capture.
  double local_max_task = 0.0;
  for (auto const& [tid, task] : pd.getTasksMap()) {
    (void)tid;
    local_max_task = std::max(local_max_task, task.getLoad());
  }
  double global_max_task = 0.0;
  handle.reduce(0, MPI_DOUBLE, MPI_MAX, &local_max_task, &global_max_task, 1);
  handle.broadcast(0, MPI_DOUBLE, &global_max_task, 1);

  double const granularity_bound = global_after_avg > 0.0
    ? global_max_task / global_after_avg
    : 0.0;
  EXPECT_LE(I_after, granularity_bound)
    << "left more imbalance than the largest single task forces";
  EXPECT_LT(I_after, I_before / 10.0)
    << "imbalance was not reduced by at least an order of magnitude";

  // fmt::print(
  //   "Rank {}: before max={}, avg={}, I={:.4f}; after max={}, avg={}, I={:.4f}\n",
  //   rank, global_before_max, global_before_avg, I_before,
  //   global_after_max, global_after_avg, I_after
  // );
};

TYPED_TEST_P(TestTemperedLB, test_strict_shared_block_transfer_preserves_tasks) {
	auto num_ranks = this->comm.numRanks();
	auto rank = this->comm.getRank();

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  int seed = TypeParam::seed;
  vt_lb::model::PhaseData pd(rank);

  bool uniform_shared_block_count = false;
  bool uniform_task_count = false;
  bool include_comm = false;
  int seed_same_across_ranks = seed;
  int seed_diff_each_rank = 24601 * rank + 19;

  generateGraphWithSharedBlocks(
    pd, num_ranks, uniform_shared_block_count, uniform_task_count,
    include_comm, seed_same_across_ranks, seed_diff_each_rank
  );

  EXPECT_GT(pd.getSharedBlocksMap().size(), 0);

  if (rank == 0) {
    for (auto tid : pd.getTaskIds()) {
      auto task = pd.getTask(tid);
      task->setLoad(task->getLoad() * 25.0);
    }
  }

  vt_lb::algo::temperedlb::Configuration config(num_ranks);
  config.cluster_based_on_shared_blocks_ = true;
  config.cluster_transfer_strategy_ =
    vt_lb::algo::temperedlb::ClusterTransferStrategy::StrictSharedBlock;
  // Charges ~10 work units per off-home shared block, comparable to task
  // loads of 5-120. At delta=1.0 the 1.6 GB block dwarfs every load and
  // no transfer ever looks beneficial.
  config.work_model_.delta = 6.25e-9;

  auto const memory_usage =
    vt_lb::algo::temperedlb::WorkModelCalculator::computeMemoryUsage(
      config, pd
    ).current_memory_usage;
  pd.setRankMaxMemoryAvailable(memory_usage + 1024.0);

  auto summary = runTemperedLB(this->comm, config, pd);

  auto const& initial_global = summary.initial;
  std::size_t initial_total = 0;
  std::unordered_set<int> initial_task_set;
  for (auto const& [r, vec] : initial_global) {
    (void)r;
    initial_total += vec.size();
    for (auto tid : vec) {
      initial_task_set.insert(tid);
    }
  }

  auto const& final_global = summary.final;
  std::size_t final_total = 0;
  std::unordered_set<int> final_task_set;
  for (auto const& [r, vec] : final_global) {
    (void)r;
    final_total += vec.size();
    for (auto tid : vec) {
      final_task_set.insert(tid);
    }
  }

  EXPECT_EQ(final_total, initial_total);
  EXPECT_EQ(final_task_set, initial_task_set);
}

// Charging ~10 work units per off-home 1.6 GB block puts locality on the same
// scale as the task loads of 5-120. At delta=1.0 the block dwarfs every load
// and no transfer ever looks beneficial.
double constexpr toy_memory_aware_delta = 6.25e-9;

void reportToyMemoryOutcome(char const* label, ToyMemoryOutcome const& out) {
  fmt::print(
    "Toy memory strict ({}): initial_max={}, final_max={}, "
    "exact_optimal_max={}, whole_block_optimal_max={}, gap_to_exact={}, "
    "gap_to_whole_block={}\n",
    label, out.initial_max, out.final_max, out.exact_optimal_max,
    out.whole_block_optimal_max, out.final_max - out.exact_optimal_max,
    out.final_max - out.whole_block_optimal_max
  );
}

void checkToyMemoryInvariants(ToyMemoryOutcome const& out) {
  EXPECT_LE(out.local_shared_bytes_after, toy_rank_memory_limit);
  EXPECT_LE(
    out.local_shared_blocks_after,
    static_cast<std::size_t>(toy_max_shared_blocks_per_rank)
  );
  EXPECT_NEAR(out.exact_optimal_max, 87.5, 1e-9);
  EXPECT_NEAR(out.whole_block_optimal_max, 120.0, 1e-9);
  EXPECT_LT(out.final_max, out.initial_max);
}

// Load only: the memory budget still binds, but off-home blocks cost nothing,
// so the balancer is free to place clusters purely by load.
TYPED_TEST_P(TestTemperedLB, test_strict_shared_block_transfer_toy_problem_load_only) {
  SET_NUM_NODES_CONSTRAINT(4);

  if constexpr (TypeParam::seed != 1) {
    GTEST_SKIP() << "Toy memory test only runs for the canonical seed";
  }

  auto const out = runToyMemoryProblem(this->comm, 0.0);

  if (this->comm.getRank() == 0) {
    reportToyMemoryOutcome("load only", out);
  }

  checkToyMemoryInvariants(out);
  EXPECT_LE(out.final_max, out.whole_block_optimal_max)
    << "Blocks are atomic, so the whole-block optimum is reachable and best";
}

// Memory aware: off-home blocks are charged, so the balancer trades load
// imbalance against locality.
TYPED_TEST_P(TestTemperedLB, test_strict_shared_block_transfer_toy_problem_memory_aware) {
  SET_NUM_NODES_CONSTRAINT(4);

  if constexpr (TypeParam::seed != 1) {
    GTEST_SKIP() << "Toy memory test only runs for the canonical seed";
  }

  auto const out = runToyMemoryProblem(this->comm, toy_memory_aware_delta);

  if (this->comm.getRank() == 0) {
    reportToyMemoryOutcome("memory aware", out);
  }

  checkToyMemoryInvariants(out);
  EXPECT_LE(out.final_max, out.whole_block_optimal_max)
    << "Charging for off-home blocks must not cost load balance here";
}

REGISTER_TYPED_TEST_SUITE_P(
  TestTemperedLB,
  test_lb_no_comm_task_counts,
  test_significant_load_imbalance_reduction,
  test_strict_shared_block_transfer_preserves_tasks,
  test_strict_shared_block_transfer_toy_problem_load_only,
  test_strict_shared_block_transfer_toy_problem_memory_aware
);

// Zip communicator type list with an integer seed sequence
template <typename TypesList, typename IntSeq>
struct ZipCommWithSeeds;

template <typename... CommTs, int... Seeds>
struct ZipCommWithSeeds<::testing::Types<CommTs...>, std::integer_sequence<int, Seeds...>> {
  template <typename CommT>
  struct PacksForComm {
    using type = ::testing::Types<CommSeedPack<CommT, Seeds>...>;
  };

  template <typename... Lists>
  struct ConcatTypes;

  template <typename... Ts>
  struct ConcatTypes<::testing::Types<Ts...>> {
    using type = ::testing::Types<Ts...>;
  };

  template <typename... Ts, typename... Us, typename... Rest>
  struct ConcatTypes<::testing::Types<Ts...>, ::testing::Types<Us...>, Rest...> {
    using type = typename ConcatTypes<::testing::Types<Ts..., Us...>, Rest...>::type;
  };

  using type = typename ConcatTypes<typename PacksForComm<CommTs>::type...>::type;
};

using SeedSeq = std::integer_sequence<int, 1, 17, 12345, 24680, 808017424>;
using CommSeedTypesForTesting = typename ZipCommWithSeeds<CommTypesForTesting, SeedSeq>::type;

// Name generator that appends the seed to the base communicator name
struct SeededCommNameGenerator {
  template <typename Pack>
  static std::string GetName(int i) {
    auto base = CommNameGenerator::template GetName<typename Pack::Comm>(i);
    return base + std::string("_seed_") + std::to_string(Pack::seed);
  }
};

INSTANTIATE_TYPED_TEST_SUITE_P(
  ZippedSeeds, TestTemperedLB, CommSeedTypesForTesting, SeededCommNameGenerator
);

} /* end namespace vt_lb::tests::unit */
