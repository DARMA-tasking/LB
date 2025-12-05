/*
//@HEADER
// *****************************************************************************
//
//                               graph_helpers.h
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

#if !defined INCLUDED_VT_LB_UNIT_GRAPH_HELPERS_H
#define INCLUDED_VT_LB_UNIT_GRAPH_HELPERS_H

#include <gtest/gtest.h>
#include <mpi.h>
#include <random>
#include <sstream>

#include <vt-lb/model/PhaseData.h>

namespace vt_lb { namespace tests { namespace unit {

/**
 * Generate random rank footprint on each rank
 *
 * @param pd The PhaseData for this rank
 * @param seed The seed for this rank
 * @param per_rank_dist Random size distribution
 */
template <typename PerRankDistType>
void generateRankFootprintBytes(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  PerRankDistType &per_rank_dist
) {
  auto rank_footprint_bytes = per_rank_dist(gen);
  pd.setRankFootprintBytes(rank_footprint_bytes);
}

/**
 * Generate a random number of shared blocks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param seed The seed for this rank
 * @param per_rank_dist Random number distribution
 * @param max_blocks_per_rank Cap for the number of shared blocks on any rank
 */
template <typename PerRankDistType>
void generateSharedBlocksCountsPerRank(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  PerRankDistType &per_rank_dist, int max_blocks_per_rank
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  int local_blocks = std::max(per_rank_dist(gen), max_blocks_per_rank);
  for (int i = 0; i < local_blocks; ++i) {
    SharedBlockType bid = static_cast<SharedBlockType>(rank * max_blocks_per_rank + i);
    SharedBlock b{bid, 0, rank};
    pd.addSharedBlock(b);
  }
}

/**
 * Generate random memory for shared blocks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param seed The seed for this rank
 * @param block_memory_dist Random memory distribution
 */
template <typename BlockMemoryDistType>
void generateSharedBlockMemory(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  BlockMemoryDistType &block_memory_dist
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  auto blockmap = pd.getSharedBlocksMap();
  for (auto item : blockmap) {
    auto &b = item.second;
    b.setSize(block_memory_dist(gen));
  }
}

/**
 * Generate a random number of tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param seed The seed for this rank
 * @param per_rank_dist Random number distribution
 * @param max_tasks_per_rank The cap for the number of tasks on any rank
 */
template <typename PerRankDistType>
void generateTaskCountsPerRank(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  PerRankDistType &per_rank_dist, int max_tasks_per_rank
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  int local_tasks = std::max(per_rank_dist(gen), max_tasks_per_rank);
  for (int i = 0; i < local_tasks; ++i) {
    TaskType tid = static_cast<TaskType>(rank * max_tasks_per_rank + i);
    Task t{tid, rank, rank, true, TaskMemory{}, 0.0};
    pd.addTask(t);
  }
}

/**
 * Generate a random number of tasks on each shared block
 *
 * @param pd The PhaseData for this rank
 * @param seed The seed for this rank
 * @param per_block_dist Random number distribution
 * @param max_tasks_per_rank The cap for the number of tasks on any rank
 */
template <typename PerSharedBlockDistType>
void generateTaskCountsPerSharedBlock(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  PerSharedBlockDistType &per_block_dist, int max_tasks_per_rank,
  int max_tasks_per_block
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  int rank_tasks = 0;
  auto blockmap = pd.getSharedBlocksMap();
  for (auto item : blockmap) {
    auto bid = item.first;

    int block_tasks = std::max(per_block_dist(gen), max_tasks_per_block);
    block_tasks = std::max(block_tasks, max_tasks_per_rank - rank_tasks);
    rank_tasks += block_tasks;
    for (int i = 0; i < block_tasks; ++i) {
      TaskType tid = static_cast<TaskType>(rank * max_tasks_per_rank + i);
      Task t{tid, rank, rank, true, TaskMemory{}, 0.0};
      t.addSharedBlock(bid);
      pd.addTask(t);
    }
  }
}

/**
 * Generate random working memory for tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param seed The seed for this rank
 * @param task_working_memory_dist Random memory distribution
 */
template <typename TaskMemoryDistType>
void generateTaskMemory(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  TaskMemoryDistType &task_working_memory_dist,
  double task_footprint_mem, double task_serialized_mem
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  auto taskmap = pd.getTasksMap();
  for (auto item : taskmap) {
    auto &t = item.second;
    TaskMemory m(
      task_working_memory_dist(gen), task_footprint_mem, task_serialized_mem
    );
    t.setMemory(m);
  }
}

/**
 * Generate random loads for tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param seed The seed for this rank
 * @param task_load_dist Random load distribution
 */
template <typename TaskLoadDistType>
void generateTaskLoads(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  TaskLoadDistType &task_load_dist
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  auto taskmap = pd.getTasksMap();
  for (auto item : taskmap) {
    auto &t = item.second;
    t.setLoad(task_load_dist(gen));
  }
}

}}} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_UNIT_GRAPH_HELPERS_H*/
