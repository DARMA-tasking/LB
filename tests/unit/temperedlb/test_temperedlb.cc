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

#include <utility>
#include <string>

#include "test_parallel_harness.h"
#include "test_helpers.h"
#include "graph_helpers.h"

#include <vt-lb/algo/temperedlb/temperedlb.h>

namespace vt_lb::tests::unit {

using namespace ::comm::tests::unit;

// Wrapper that zips a communicator type with a single integer seed
template <typename CommT, int Seed>
struct CommSeedPack {
  using Comm = CommT;
  static constexpr int seed = Seed;
};

// Helper: build TemperedLB, compute initial and final global distributions
template <typename CommT>
struct LbRunSummary {
  std::unordered_map<int, std::vector<int>> initial;
  std::unordered_map<int, std::vector<int>> final;
};

template <typename CommT>
LbRunSummary<CommT> runTemperedLB(
  CommT& comm,
  vt_lb::algo::temperedlb::Configuration const& config,
  vt_lb::model::PhaseData const& pd
) {
  vt_lb::algo::temperedlb::TemperedLB<CommT> lb(comm, config);
  lb.inputData(std::make_unique<vt_lb::model::PhaseData>(pd));

  auto initial_global = lb.getGlobalDistribution(pd.getTaskIds());
  std::unordered_map<int, std::vector<int>> initial_int;
  for (auto const& [r, vec] : initial_global) {
    auto& out = initial_int[r];
    out.reserve(vec.size());
    for (auto tid : vec) {
      out.push_back(static_cast<int>(tid));
    }
  }

  auto local_after = lb.run();
  (void)local_after; // not needed for summary

  auto final_global = lb.getGlobalDistribution(local_after);
  std::unordered_map<int, std::vector<int>> final_int;
  for (auto const& [r, vec] : final_global) {
    auto& out = final_int[r];
    out.reserve(vec.size());
    for (auto tid : vec) {
      out.push_back(static_cast<int>(tid));
    }
  }

  return {std::move(initial_int), std::move(final_int)};
}

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
  EXPECT_NEAR(I_after, 0.0, 0.02);

  // fmt::print(
  //   "Rank {}: before max={}, avg={}, I={:.4f}; after max={}, avg={}, I={:.4f}\n",
  //   rank, global_before_max, global_before_avg, I_before,
  //   global_after_max, global_after_avg, I_after
  // );
};

REGISTER_TYPED_TEST_SUITE_P(
  TestTemperedLB, test_lb_no_comm_task_counts, test_significant_load_imbalance_reduction
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
