/*
//@HEADER
// *****************************************************************************
//
//                                 test_work_model.cc
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

#include "test_parallel_harness.h"
#include "test_helpers.h"
#include "graph_helpers.h"

#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/symmetrize_comm.h>

namespace vt_lb { namespace tests { namespace unit {

template <comm::Communicator CommType>
struct TestWorkModelBasic : TestParallelHarness<CommType> {
  static constexpr int num_seeds = 100;

  void setupRandomNonzeroWorkModel(
    std::mt19937 &gen, algo::temperedlb::WorkModel &wm
  );
  void setupNoMemoryInfo(algo::temperedlb::Configuration &cfg);
  void setupRandomTaskMemory(std::mt19937 &gen, model::PhaseData &pd);
  void setupUniformTaskMemory(
    std::mt19937 &gen, model::PhaseData &pd, double working_mem,
    double footprint_mem, double serialized_mem
  );

  double setupRandomUniformLoadOnlyNoMemProblem(
    std::mt19937 &gen, model::PhaseData &pd,
    algo::temperedlb::Configuration &cfg,
    algo::temperedlb::WorkBreakdown &expected_bd,
    algo::temperedlb::WorkModel &wm
  );
  double setupRandomLoadOnlyNoMemProblem(
    std::mt19937 &gen, model::PhaseData &pd,
    algo::temperedlb::Configuration &cfg,
    algo::temperedlb::WorkBreakdown &expected_bd,
    algo::temperedlb::WorkModel &wm
  );
  double setupRandomUniformSharedBlocksProblem(
    std::mt19937 &gen, model::PhaseData &pd,
    algo::temperedlb::Configuration &cfg,
    algo::temperedlb::WorkBreakdown &expected_bd,
    algo::temperedlb::WorkModel &wm
  );
  double setupRandomSharedBlocksProblem(
    std::mt19937 &gen, model::PhaseData &pd,
    algo::temperedlb::Configuration &cfg,
    algo::temperedlb::WorkBreakdown &expected_bd,
    algo::temperedlb::WorkModel &wm
  );
  double setupRandomLoadAndIntraCommNoMemProblem(
    std::mt19937 &gen, model::PhaseData &pd,
    algo::temperedlb::Configuration &cfg,
    algo::temperedlb::WorkBreakdown &expected_bd,
    algo::temperedlb::WorkModel &wm
  );
  double setupRandomLoadAndInterCommNoMemProblem(
    std::mt19937 &gen, model::PhaseData &pd,
    algo::temperedlb::Configuration &cfg,
    algo::temperedlb::WorkBreakdown &expected_bd,
    algo::temperedlb::WorkModel &wm
  );
  double setupRandomLoadAndMixedCommNoMemProblem(
    std::mt19937 &gen, model::PhaseData &pd,
    algo::temperedlb::Configuration &cfg,
    algo::temperedlb::WorkBreakdown &expected_bd,
    algo::temperedlb::WorkModel &wm
  );

  void verifyNoChangeUpdate(
    const model::PhaseData &pd, const algo::temperedlb::Configuration &cfg,
    const algo::temperedlb::WorkBreakdown &expected_bd,
    const algo::temperedlb::WorkModel &wm, double expected_work
  );
};

template <comm::Communicator CommType>
void TestWorkModelBasic<CommType>::setupRandomNonzeroWorkModel(
  std::mt19937 &gen, algo::temperedlb::WorkModel &wm
) {
  std::uniform_real_distribution<> uni_dist(0.0, 1.0);
  wm.rank_alpha = 2.0 * uni_dist(gen);
  wm.beta = uni_dist(gen);
  wm.gamma = uni_dist(gen);
  wm.delta = uni_dist(gen);
}

template <comm::Communicator CommType>
void TestWorkModelBasic<CommType>::setupNoMemoryInfo(
  algo::temperedlb::Configuration &cfg
) {
  cfg.work_model_.has_memory_info = false;
  // Make sure that the above always overrides the below by leaving them on
  cfg.work_model_.has_shared_block_memory_info = true;
  cfg.work_model_.has_task_footprint_memory_info = true;
  cfg.work_model_.has_task_working_memory_info = true;
  cfg.work_model_.has_task_serialized_memory_info = true;
}

template <comm::Communicator CommType>
void TestWorkModelBasic<CommType>::setupRandomTaskMemory(
  std::mt19937 &gen, model::PhaseData &pd
) {
  std::exponential_distribution<> expo_dist(1000.0);
  int smem = static_cast<int>(expo_dist(gen));
  int fmem = smem + static_cast<int>(expo_dist(gen));
  generateTaskMemory(pd, gen, expo_dist, fmem, smem);
}

template <comm::Communicator CommType>
void TestWorkModelBasic<CommType>::setupUniformTaskMemory(
  std::mt19937 &gen, model::PhaseData &pd, double working_mem,
  double footprint_mem, double serialized_mem
) {
  std::uniform_real_distribution<> dist(working_mem, working_mem);
  generateTaskMemory(pd, gen, dist, footprint_mem, serialized_mem);
}

template <comm::Communicator CommType>
double TestWorkModelBasic<CommType>::setupRandomUniformLoadOnlyNoMemProblem(
  std::mt19937 &gen, model::PhaseData &pd, algo::temperedlb::Configuration &cfg,
  algo::temperedlb::WorkBreakdown &expected_bd, algo::temperedlb::WorkModel &wm
) {
  // We're using all non-zero coefficients but there will only be load
  setupRandomNonzeroWorkModel(gen, wm);

  // Do not consider memory even though we will define some
  setupNoMemoryInfo(cfg);

  // Generate graph: task load will be uniform across all tasks on a given rank;
  // there will be no shared blocks or communication
  int min_tasks_per_rank = 0;
  int max_tasks_per_rank = 100;
  double expo_lambda = 100.0;
  std::exponential_distribution<> expo_dist(expo_lambda);
  double uniform_load = expo_dist(gen);
  std::uniform_real_distribution<> load_dist(uniform_load, uniform_load);
  generateTasksWithoutSharedBlocks(
    pd, gen, min_tasks_per_rank, max_tasks_per_rank, load_dist
  );

  // Define random task memory even though we will not use it
  setupRandomTaskMemory(gen, pd);

  // Define expected breakdown and work
  expected_bd.compute = pd.getTasksMap().size() * uniform_load;
  expected_bd.inter_node_recv_comm = 0.0;
  expected_bd.inter_node_send_comm = 0.0;
  expected_bd.intra_node_recv_comm = 0.0;
  expected_bd.intra_node_send_comm = 0.0;
  expected_bd.shared_mem_comm = 0.0;
  expected_bd.memory_breakdown = {0.0, 0.0, 0.0};

  // Compute expected work
  double expected_work = expected_bd.compute * wm.rank_alpha;
  return expected_work;
}

template <comm::Communicator CommType>
double TestWorkModelBasic<CommType>::setupRandomLoadOnlyNoMemProblem(
  std::mt19937 &gen, model::PhaseData &pd, algo::temperedlb::Configuration &cfg,
  algo::temperedlb::WorkBreakdown &expected_bd, algo::temperedlb::WorkModel &wm
) {
  // We're using all non-zero coefficients but there will only be load
  setupRandomNonzeroWorkModel(gen, wm);

  // Do not consider memory even though we will define some
  setupNoMemoryInfo(cfg);

  // Generate graph: task load will be uniform across all tasks on a given rank;
  // there will be no shared blocks or communication
  int min_tasks_per_rank = 0;
  int max_tasks_per_rank = 100;
  double expo_lambda = 1000.0;
  std::exponential_distribution<> load_dist(expo_lambda);
  generateTasksWithoutSharedBlocks(
    pd, gen, min_tasks_per_rank, max_tasks_per_rank, load_dist
  );

  // Define random task memory even though we will not use it
  setupRandomTaskMemory(gen, pd);

  // Define expected breakdown and work
  double expected_compute = 0.0;
  auto &taskmap = pd.getTasksMap();
  for (auto &t : taskmap) {
    expected_compute += t.second.getLoad();
  }
  expected_bd.compute = expected_compute;
  expected_bd.inter_node_recv_comm = 0.0;
  expected_bd.inter_node_send_comm = 0.0;
  expected_bd.intra_node_recv_comm = 0.0;
  expected_bd.intra_node_send_comm = 0.0;
  expected_bd.shared_mem_comm = 0.0;
  expected_bd.memory_breakdown = {0.0, 0.0, 0.0};

  // Compute expected work
  double expected_work = expected_bd.compute * wm.rank_alpha;
  return expected_work;
}

template <comm::Communicator CommType>
double TestWorkModelBasic<CommType>::setupRandomUniformSharedBlocksProblem(
  std::mt19937 &gen, model::PhaseData &pd, algo::temperedlb::Configuration &cfg,
  algo::temperedlb::WorkBreakdown &expected_bd, algo::temperedlb::WorkModel &wm
) {
  // We're using all non-zero coefficients but there will only be load
  setupRandomNonzeroWorkModel(gen, wm);

  // Consider all types of memory
  cfg.work_model_.has_memory_info = true;
  cfg.work_model_.has_shared_block_memory_info = true;
  cfg.work_model_.has_task_footprint_memory_info = true;
  cfg.work_model_.has_task_working_memory_info = true;
  cfg.work_model_.has_task_serialized_memory_info = true;

  // Generate graph: task load will be uniform across all tasks on a given rank;
  // there will be no shared blocks or communication
  int min_blocks_per_rank = 0;
  int max_blocks_per_rank = 100;
  double expo_lambda = 1000000.0;
  std::exponential_distribution<> expo_dist(expo_lambda);
  int uniform_mem = static_cast<int>(expo_dist(gen));
  int min_tasks_per_block = 1;
  int max_tasks_per_block = 10;
  double uniform_load = expo_dist(gen);
  std::uniform_real_distribution<> load_dist(uniform_load, uniform_load);
  generateSharedBlocksWithTasks(
    pd, gen, min_blocks_per_rank, max_blocks_per_rank, uniform_mem, uniform_mem,
    min_tasks_per_block, max_tasks_per_block, load_dist
  );

  auto num_tasks = pd.getTasksMap().size();
  std::uniform_int_distribution<> uni_dist(100, 1000);
  double working_mem = 0.0;
  double serialized_mem = 0.0;
  if (num_tasks > 0) {
    working_mem = uni_dist(gen);
    serialized_mem = uni_dist(gen);
  }
  double footprint_mem = serialized_mem * 2.0;
  setupUniformTaskMemory(gen, pd, working_mem, footprint_mem, serialized_mem);

  // Define expected breakdown and work
  double expected_block_mem =
    static_cast<double>(uniform_mem) * pd.getSharedBlocksMap().size();
  double expected_task_mem =
    footprint_mem * num_tasks + working_mem + serialized_mem;
  expected_bd.compute = num_tasks * uniform_load;
  expected_bd.inter_node_recv_comm = 0.0;
  expected_bd.inter_node_send_comm = 0.0;
  expected_bd.intra_node_recv_comm = 0.0;
  expected_bd.intra_node_send_comm = 0.0;
  expected_bd.shared_mem_comm = 0.0;  // all at home
  expected_bd.memory_breakdown = {
    expected_block_mem + expected_task_mem, working_mem, serialized_mem
  };

  // Compute expected work
  double expected_work =
    expected_bd.compute * wm.rank_alpha +
    expected_bd.shared_mem_comm * wm.delta;

  return expected_work;
}

template <comm::Communicator CommType>
double TestWorkModelBasic<CommType>::setupRandomSharedBlocksProblem(
  std::mt19937 &gen, model::PhaseData &pd, algo::temperedlb::Configuration &cfg,
  algo::temperedlb::WorkBreakdown &expected_bd, algo::temperedlb::WorkModel &wm
) {
  // We're using all non-zero coefficients but there will only be load
  setupRandomNonzeroWorkModel(gen, wm);

  // Consider all types of memory even though only shared blocks have it
  cfg.work_model_.has_memory_info = true;
  cfg.work_model_.has_shared_block_memory_info = true;
  cfg.work_model_.has_task_footprint_memory_info = true;
  cfg.work_model_.has_task_working_memory_info = true;
  cfg.work_model_.has_task_serialized_memory_info = true;

  // Generate graph: task load will be uniform across all tasks on a given rank;
  // there will be no shared blocks or communication
  int min_blocks_per_rank = 0;
  int max_blocks_per_rank = 100;
  double expo_lambda = 10000000.0;
  std::exponential_distribution<> expo_dist(expo_lambda);
  int max_mem = static_cast<int>(expo_dist(gen));
  int min_mem = max_mem / 2;
  int min_tasks_per_block = 1;
  int max_tasks_per_block = 10;
  std::exponential_distribution<> load_dist(expo_lambda / 10);
  generateSharedBlocksWithTasks(
    pd, gen, min_blocks_per_rank, max_blocks_per_rank, min_mem, max_mem,
    min_tasks_per_block, max_tasks_per_block, load_dist
  );

  // Define expected breakdown and work
  double expected_compute = 0.0;
  auto &taskmap = pd.getTasksMap();
  for (auto &t : taskmap) {
    expected_compute += t.second.getLoad();
  }
  double expected_block_mem = 0.0;
  auto &blockmap = pd.getSharedBlocksMap();
  for (auto &b : blockmap) {
    expected_block_mem += b.second.getSize();
  }
  expected_bd.compute = expected_compute;
  expected_bd.inter_node_recv_comm = 0.0;
  expected_bd.inter_node_send_comm = 0.0;
  expected_bd.intra_node_recv_comm = 0.0;
  expected_bd.intra_node_send_comm = 0.0;
  expected_bd.shared_mem_comm = 0.0;  // all at home
  expected_bd.memory_breakdown = {expected_block_mem, 0.0, 0.0};

  // Compute expected work
  double expected_work =
    expected_bd.compute * wm.rank_alpha +
    expected_bd.shared_mem_comm * wm.delta;

  return expected_work;
}

template <comm::Communicator CommType>
double TestWorkModelBasic<CommType>::setupRandomLoadAndIntraCommNoMemProblem(
  std::mt19937 &gen, model::PhaseData &pd, algo::temperedlb::Configuration &cfg,
  algo::temperedlb::WorkBreakdown &expected_bd, algo::temperedlb::WorkModel &wm
) {
  // We're using all non-zero coefficients but there will only be load
  setupRandomNonzeroWorkModel(gen, wm);

  // Do not consider memory even though we will define some
  setupNoMemoryInfo(cfg);

  // Generate graph: task load will be uniform across all tasks on a given rank;
  // there will be no shared blocks or communication
  int min_tasks_per_rank = 0;
  int max_tasks_per_rank = 100;
  double expo_lambda = 5000.0;
  std::exponential_distribution<> load_dist(expo_lambda);
  generateTasksWithoutSharedBlocks(
    pd, gen, min_tasks_per_rank, max_tasks_per_rank, load_dist
  );

  // Define random task memory even though we will not use it
  setupRandomTaskMemory(gen, pd);

  std::uniform_int_distribution<> eps_per_task_dist(0, 10);
  std::exponential_distribution<> weight_per_edge_dist(100.0);
  generateIntraRankComm(
    pd, gen, eps_per_task_dist, weight_per_edge_dist
  );

  // Define expected breakdown and work
  double expected_compute = 0.0;
  auto &taskmap = pd.getTasksMap();
  for (auto &t : taskmap) {
    expected_compute += t.second.getLoad();
  }
  double expected_intra_recv = 0.0;
  double expected_intra_send = 0.0;
  auto &edges = pd.getCommunications();
  for (auto &e : edges) {
    expected_intra_recv += e.getVolume();
    expected_intra_send += e.getVolume();
  }
  expected_bd.compute = expected_compute;
  expected_bd.inter_node_recv_comm = 0.0;
  expected_bd.inter_node_send_comm = 0.0;
  expected_bd.intra_node_recv_comm = expected_intra_recv;
  expected_bd.intra_node_send_comm = expected_intra_send;
  expected_bd.shared_mem_comm = 0.0;
  expected_bd.memory_breakdown = {0.0, 0.0, 0.0};

  // Compute expected work
  double expected_work =
    expected_bd.compute * wm.rank_alpha +
    std::max(
     expected_bd.intra_node_recv_comm, expected_bd.intra_node_send_comm
    ) * wm.gamma;
  return expected_work;
}

template <comm::Communicator CommType>
double TestWorkModelBasic<CommType>::setupRandomLoadAndInterCommNoMemProblem(
  std::mt19937 &gen, model::PhaseData &pd, algo::temperedlb::Configuration &cfg,
  algo::temperedlb::WorkBreakdown &expected_bd, algo::temperedlb::WorkModel &wm
) {
  // We're using all non-zero coefficients but there will only be load
  setupRandomNonzeroWorkModel(gen, wm);

  // Do not consider memory even though we will define some
  setupNoMemoryInfo(cfg);

  // Generate graph: task load will be uniform across all tasks on a given rank;
  // there will be no shared blocks or communication
  int min_tasks_per_rank = 20;
  int max_tasks_per_rank = 50;
  std::uniform_real_distribution<> load_dist(1.0, 1000.0);
  generateTasksWithoutSharedBlocks(
    pd, gen, min_tasks_per_rank, max_tasks_per_rank, load_dist
  );

  // Define random task memory even though we will not use it
  setupRandomTaskMemory(gen, pd);

  std::uniform_int_distribution<> eps_per_task_dist(0, 10);
  std::exponential_distribution<> weight_per_edge_dist(100.0);
  generateInterRankComm(
    pd, gen, eps_per_task_dist, weight_per_edge_dist, min_tasks_per_rank,
    this->comm.numRanks(), 0.5
  );

  vt_lb::algo::temperedlb::CommunicationsSymmetrizer cs(this->comm, pd);
  cs.run();

  // Define expected breakdown and work
  double expected_compute = 0.0;
  auto &taskmap = pd.getTasksMap();
  for (auto &t : taskmap) {
    expected_compute += t.second.getLoad();
  }
  auto rank = this->comm.getRank();
  double expected_inter_recv = 0.0;
  double expected_inter_send = 0.0;
  auto &edges = pd.getCommunications();
  for (auto &e : edges) {
    if (e.getFromRank() != e.getToRank()) {
      if (e.getToRank() == rank) {
        expected_inter_recv += e.getVolume();
      } else {
        expected_inter_send += e.getVolume();
      }
    }
  }
  expected_bd.compute = expected_compute;
  expected_bd.inter_node_recv_comm = expected_inter_recv;
  expected_bd.inter_node_send_comm = expected_inter_send;
  expected_bd.intra_node_recv_comm = 0.0;
  expected_bd.intra_node_send_comm = 0.0;
  expected_bd.shared_mem_comm = 0.0;
  expected_bd.memory_breakdown = {0.0, 0.0, 0.0};

  // Compute expected work
  double expected_work =
    expected_bd.compute * wm.rank_alpha +
    std::max(
      expected_bd.inter_node_recv_comm, expected_bd.inter_node_send_comm
    ) * wm.beta;
  return expected_work;
}

template <comm::Communicator CommType>
double TestWorkModelBasic<CommType>::setupRandomLoadAndMixedCommNoMemProblem(
  std::mt19937 &gen, model::PhaseData &pd, algo::temperedlb::Configuration &cfg,
  algo::temperedlb::WorkBreakdown &expected_bd, algo::temperedlb::WorkModel &wm
) {
  // We're using all non-zero coefficients but there will only be load
  setupRandomNonzeroWorkModel(gen, wm);

  // Do not consider memory even though we will define some
  setupNoMemoryInfo(cfg);

  // Generate graph: task load will be uniform across all tasks on a given rank;
  // there will be no shared blocks or communication
  int min_tasks_per_rank = 20;
  int max_tasks_per_rank = 50;
  std::uniform_real_distribution<> load_dist(0.1, 50.0);
  generateTasksWithoutSharedBlocks(
    pd, gen, min_tasks_per_rank, max_tasks_per_rank, load_dist
  );

  // Define random task memory even though we will not use it
  setupRandomTaskMemory(gen, pd);

  std::uniform_int_distribution<> eps_per_task_dist(0, 10);
  std::exponential_distribution<> weight_per_edge_dist(1000.0);
  generateRankComm(
    pd, gen, eps_per_task_dist, weight_per_edge_dist, min_tasks_per_rank,
    this->comm.numRanks(), 0.5
  );

  vt_lb::algo::temperedlb::CommunicationsSymmetrizer cs(this->comm, pd);
  cs.run();

  // Define expected breakdown and work
  double expected_compute = 0.0;
  auto &taskmap = pd.getTasksMap();
  for (auto &t : taskmap) {
    expected_compute += t.second.getLoad();
  }
  auto rank = this->comm.getRank();
  double expected_intra_recv = 0.0;
  double expected_intra_send = 0.0;
  double expected_inter_recv = 0.0;
  double expected_inter_send = 0.0;
  auto &edges = pd.getCommunications();
  for (auto &e : edges) {
    if (e.getFromRank() != e.getToRank()) {
      if (e.getToRank() == rank) {
        expected_inter_recv += e.getVolume();
      } else {
        expected_inter_send += e.getVolume();
      }
    } else {
      expected_intra_recv += e.getVolume();
      expected_intra_send += e.getVolume();
    }
  }
  expected_bd.compute = expected_compute;
  expected_bd.inter_node_recv_comm = expected_inter_recv;
  expected_bd.inter_node_send_comm = expected_inter_send;
  expected_bd.intra_node_recv_comm = expected_intra_recv;
  expected_bd.intra_node_send_comm = expected_intra_send;
  expected_bd.shared_mem_comm = 0.0;
  expected_bd.memory_breakdown = {0.0, 0.0, 0.0};

  // Compute expected work
  double expected_work =
    expected_bd.compute * wm.rank_alpha +
    std::max(
      expected_bd.inter_node_recv_comm, expected_bd.inter_node_send_comm
    ) * wm.beta +
    std::max(
      expected_bd.intra_node_recv_comm, expected_bd.intra_node_send_comm
    ) * wm.gamma;
  return expected_work;
}

template <comm::Communicator CommType>
void TestWorkModelBasic<CommType>::verifyNoChangeUpdate(
  const model::PhaseData &pd, const algo::temperedlb::Configuration &cfg,
  const algo::temperedlb::WorkBreakdown &expected_bd,
  const algo::temperedlb::WorkModel &wm, double expected_work
) {
  auto bd = algo::temperedlb::WorkModelCalculator::computeWorkBreakdown(pd, cfg);
  auto &mb = bd.memory_breakdown;
  auto &emb = expected_bd.memory_breakdown;

  // Verify computed work breakdown
  EXPECT_FLOAT_EQ(bd.compute, expected_bd.compute);
  EXPECT_DOUBLE_EQ(bd.inter_node_recv_comm, expected_bd.inter_node_recv_comm);
  EXPECT_DOUBLE_EQ(bd.inter_node_send_comm, expected_bd.inter_node_send_comm);
  EXPECT_DOUBLE_EQ(bd.intra_node_recv_comm, expected_bd.intra_node_recv_comm);
  EXPECT_DOUBLE_EQ(bd.intra_node_send_comm, expected_bd.intra_node_send_comm);
  EXPECT_DOUBLE_EQ(bd.shared_mem_comm, expected_bd.shared_mem_comm);

  // Verify computed memory breakdown
  EXPECT_DOUBLE_EQ(mb.current_memory_usage, emb.current_memory_usage);
  EXPECT_DOUBLE_EQ(
    mb.current_max_task_working_bytes, emb.current_max_task_working_bytes
  );
  EXPECT_DOUBLE_EQ(
    mb.current_max_task_serialized_bytes, emb.current_max_task_serialized_bytes
  );

  // Verify baseline work
  double base = algo::temperedlb::WorkModelCalculator::computeWork(wm, bd);
  EXPECT_FLOAT_EQ(base, expected_work);

  // Verify update under no changes
  std::vector<model::Task> add_tasks;
  std::vector<model::Edge> add_edges;
  std::vector<model::TaskType> remove_ids;
  double updated = algo::temperedlb::WorkModelCalculator::computeWorkUpdate(
    pd, wm, bd, add_tasks, add_edges, remove_ids
  );
  EXPECT_DOUBLE_EQ(updated, base);
}

TYPED_TEST_SUITE(TestWorkModelBasic, CommTypesForTesting, CommNameGenerator);

TYPED_TEST(TestWorkModelBasic, compute_work_uses_max_comm_components) {
  // Build a breakdown with differing send/recv values to test max selection
  algo::temperedlb::WorkBreakdown bd;
  bd.compute = 10.0;
  bd.inter_node_recv_comm = 5.0;
  bd.inter_node_send_comm = 7.5;   // inter-node max should be 7.5
  bd.intra_node_recv_comm = 1.0;
  bd.intra_node_send_comm = 2.0;   // intra-node max should be 2.0
  bd.shared_mem_comm = 3.0;

  algo::temperedlb::WorkModel wm;
  wm.rank_alpha = 2.0;
  wm.beta = 1.0;
  wm.gamma = 0.5;
  wm.delta = 3.0;

  double w = algo::temperedlb::WorkModelCalculator::computeWork(wm, bd);
  // Expected: 2*10 + 1*7.5 + 0.5*2 + 3*3 = 20 + 7.5 + 1 + 9 = 37.5
  EXPECT_DOUBLE_EQ(w, 37.5);
}

TYPED_TEST(TestWorkModelBasic, compute_memory_usage_rank_only) {
  auto& the_comm = this->comm;
  algo::temperedlb::Configuration cfg;
  // Disable all task/shared-block memory flags so only rank footprint contributes
  cfg.work_model_.has_memory_info = true;
  cfg.work_model_.has_task_serialized_memory_info = false;
  cfg.work_model_.has_task_working_memory_info = false;
  cfg.work_model_.has_task_footprint_memory_info = false;
  cfg.work_model_.has_shared_block_memory_info = false;

  model::PhaseData pd(the_comm.getRank());
  pd.setRankFootprintBytes(12345.0);
  pd.setRankMaxMemoryAvailable(999999.0);

  auto mb = algo::temperedlb::WorkModelCalculator::computeMemoryUsage(cfg, pd);
  EXPECT_DOUBLE_EQ(mb.current_memory_usage, 12345.0);
  EXPECT_DOUBLE_EQ(mb.current_max_task_working_bytes, 0.0);
  EXPECT_DOUBLE_EQ(mb.current_max_task_serialized_bytes, 0.0);
}

TYPED_TEST(TestWorkModelBasic, check_memory_fit_basic) {
  auto& the_comm = this->comm;
  algo::temperedlb::Configuration cfg;
  cfg.work_model_.has_memory_info = true; // enable memory checks

  model::PhaseData pd(the_comm.getRank());
  pd.setRankMaxMemoryAvailable(1000.0);

  // Fits
  EXPECT_TRUE(algo::temperedlb::WorkModelCalculator::checkMemoryFit(cfg, pd, 999.9));
  // Boundary
  EXPECT_TRUE(algo::temperedlb::WorkModelCalculator::checkMemoryFit(cfg, pd, 1000.0));
  // Exceeds
  EXPECT_FALSE(algo::temperedlb::WorkModelCalculator::checkMemoryFit(cfg, pd, 1000.1));
}

TYPED_TEST(TestWorkModelBasic, compute_work_update_no_changes_is_identity) {
  auto& the_comm = this->comm;

  // Work model with non-zero coefficients
  algo::temperedlb::WorkModel wm;
  wm.rank_alpha = 0.5;
  wm.beta = 0.2;
  wm.gamma = 0.3;
  wm.delta = 1.0;

  // Phase data with tasks referencing shared blocks; shared blocks off-home produce bytes
  model::PhaseData pd(the_comm.getRank());
  // Define two shared blocks with specific homes/sizes
  model::SharedBlock sbA{1, 100.0, (the_comm.getRank() + 1) % the_comm.numRanks()};
  model::SharedBlock sbB{2, 50.0,  the_comm.getRank()}; // local-home; should not add shared_mem_comm
  pd.addSharedBlock(sbA);
  pd.addSharedBlock(sbB);

  // Two tasks that reference both blocks
  model::Task t1{10, 11.0};
  t1.addSharedBlock(sbA.getId());
  t1.addSharedBlock(sbB.getId());

  model::Task t2{11, 0.0};
  t2.addSharedBlock(sbA.getId());

  pd.addTask(t1);
  pd.addTask(t2);

  // Current breakdown computed from PhaseData and a default config with memory info on
  algo::temperedlb::Configuration cfg;
  cfg.work_model_.has_memory_info = true;
  cfg.work_model_.has_shared_block_memory_info = true;
  cfg.work_model_.has_task_footprint_memory_info = false;
  cfg.work_model_.has_task_working_memory_info = false;
  cfg.work_model_.has_task_serialized_memory_info = false;

  auto bd = algo::temperedlb::WorkModelCalculator::computeWorkBreakdown(pd, cfg);

  // Baseline work
  double base = algo::temperedlb::WorkModelCalculator::computeWork(wm, bd);

  // No changes
  std::vector<model::Task> add_tasks;
  std::vector<model::Edge> add_edges;
  std::vector<model::TaskType> remove_ids;

  double updated = algo::temperedlb::WorkModelCalculator::computeWorkUpdate(
    pd, wm, bd, add_tasks, add_edges, remove_ids
  );

  EXPECT_DOUBLE_EQ(updated, base);
}

TYPED_TEST(TestWorkModelBasic, computeWorkUpdateNoChangesUniformLoadOnlyNoMem) {
  auto& the_comm = this->comm;
  auto rank = the_comm.getRank();

  algo::temperedlb::Configuration cfg;
  algo::temperedlb::WorkBreakdown bd;
  algo::temperedlb::WorkModel wm;

  std::mt19937 gen;
  for (int i=0; i<this->num_seeds; ++i) {
    gen.seed(rank * 89 + i * 3);
    model::PhaseData pd(rank);
    double expected_work = this->setupRandomUniformLoadOnlyNoMemProblem(
      gen, pd, cfg, bd, wm
    );

    this->verifyNoChangeUpdate(pd, cfg, bd, wm, expected_work);
  }
}

TYPED_TEST(TestWorkModelBasic, computeWorkUpdateNoChangesLoadOnlyNoMem) {
  auto& the_comm = this->comm;
  auto rank = the_comm.getRank();

  algo::temperedlb::Configuration cfg;
  algo::temperedlb::WorkBreakdown bd;
  algo::temperedlb::WorkModel wm;

  std::mt19937 gen;
  for (int i=0; i<this->num_seeds; ++i) {
    gen.seed(rank * 21 + i * 4);
    model::PhaseData pd(rank);
    double expected_work = this->setupRandomLoadOnlyNoMemProblem(
      gen, pd, cfg, bd, wm
    );

    this->verifyNoChangeUpdate(pd, cfg, bd, wm, expected_work);
  }
}

TYPED_TEST(TestWorkModelBasic, computeWorkUpdateNoChangesUniformSharedBlocks) {
  auto& the_comm = this->comm;
  auto rank = the_comm.getRank();

  algo::temperedlb::Configuration cfg;
  algo::temperedlb::WorkBreakdown bd;
  algo::temperedlb::WorkModel wm;

  std::mt19937 gen;
  for (int i=0; i<this->num_seeds; ++i) {
    gen.seed(rank * 191 + i * 5);
    model::PhaseData pd(rank);
    double expected_work = this->setupRandomUniformSharedBlocksProblem(
      gen, pd, cfg, bd, wm
    );

    this->verifyNoChangeUpdate(pd, cfg, bd, wm, expected_work);
  }
}

TYPED_TEST(TestWorkModelBasic, computeWorkUpdateNoChangesSharedBlocks) {
  auto& the_comm = this->comm;
  auto rank = the_comm.getRank();

  algo::temperedlb::Configuration cfg;
  algo::temperedlb::WorkBreakdown bd;
  algo::temperedlb::WorkModel wm;

  std::mt19937 gen;
  for (int i=0; i<this->num_seeds; ++i) {
    gen.seed(rank * 88 + i * 6);
    model::PhaseData pd(rank);
    double expected_work = this->setupRandomSharedBlocksProblem(
      gen, pd, cfg, bd, wm
    );

    this->verifyNoChangeUpdate(pd, cfg, bd, wm, expected_work);
  }
}

TYPED_TEST(TestWorkModelBasic, computeWorkUpdateNoChangesLoadAndIntraComm) {
  auto& the_comm = this->comm;
  auto rank = the_comm.getRank();

  algo::temperedlb::Configuration cfg;
  algo::temperedlb::WorkBreakdown bd;
  algo::temperedlb::WorkModel wm;

  std::mt19937 gen;
  for (int i=0; i<this->num_seeds; ++i) {
    gen.seed(rank * 67 + i * 7);
    model::PhaseData pd(rank);
    double expected_work = this->setupRandomLoadAndIntraCommNoMemProblem(
      gen, pd, cfg, bd, wm
    );

    this->verifyNoChangeUpdate(pd, cfg, bd, wm, expected_work);
  }
}

TYPED_TEST(TestWorkModelBasic, computeWorkUpdateNoChangesLoadAndInterComm) {
  auto& the_comm = this->comm;
  auto rank = the_comm.getRank();

  algo::temperedlb::Configuration cfg;
  algo::temperedlb::WorkBreakdown bd;
  algo::temperedlb::WorkModel wm;

  std::mt19937 gen;
  for (int i=0; i<this->num_seeds; ++i) {
    gen.seed(rank * 42 + i * 8);
    model::PhaseData pd(rank);
    double expected_work = this->setupRandomLoadAndInterCommNoMemProblem(
      gen, pd, cfg, bd, wm
    );

    this->verifyNoChangeUpdate(pd, cfg, bd, wm, expected_work);
  }
}

TYPED_TEST(TestWorkModelBasic, computeWorkUpdateNoChangesLoadAndMixedComm) {
  auto& the_comm = this->comm;
  auto rank = the_comm.getRank();

  algo::temperedlb::Configuration cfg;
  algo::temperedlb::WorkBreakdown bd;
  algo::temperedlb::WorkModel wm;

  std::mt19937 gen;
  for (int i=0; i<this->num_seeds; ++i) {
    gen.seed(rank * 11 + i * 9);
    model::PhaseData pd(rank);
    double expected_work = this->setupRandomLoadAndMixedCommNoMemProblem(
      gen, pd, cfg, bd, wm
    );

    this->verifyNoChangeUpdate(pd, cfg, bd, wm, expected_work);
  }
}

}}} // end namespace vt_lb::tests::unit
