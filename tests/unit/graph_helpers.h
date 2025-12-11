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

#include <algorithm>
#include <gtest/gtest.h>
#include <mpi.h>
#include <random>
#include <sstream>

#include <vt-lb/model/Communication.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/SharedBlock.h>
#include <vt-lb/model/Task.h>
#include <vt-lb/model/types.h>
#include <vt-lb/model/PhaseData.h>

namespace vt_lb { namespace tests { namespace unit {

/**
 * Generate random rank footprint on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
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
 * @param gen The seeded generator for this rank
 * @param per_rank_dist Random number distribution
 * @param max_blocks_per_rank Cap for the number of shared blocks on any rank
 */
template <typename PerRankDistType>
void generateSharedBlockCountsPerRank(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  PerRankDistType &per_rank_dist, int max_blocks_per_rank
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  int local_blocks = std::min(per_rank_dist(gen), max_blocks_per_rank);
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
 * @param gen The seeded generator for this rank
 * @param block_memory_dist Random memory distribution
 */
template <typename BlockMemoryDistType>
void generateSharedBlockMemory(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  BlockMemoryDistType &block_memory_dist
) {
  using namespace vt_lb::model;

  auto block_ids = pd.getSharedBlockIds();
  for (auto bid : block_ids) {
    auto b = pd.getSharedBlock(bid);
    b->setSize(block_memory_dist(gen));
  }
}

/**
 * Generate a random number of tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
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

  int local_tasks = std::min(per_rank_dist(gen), max_tasks_per_rank);
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
 * @param gen The seeded generator for this rank
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
  int local_block_num = 0;
  auto block_ids = pd.getSharedBlockIds();
  for (auto bid : block_ids) {
    int block_tasks = std::min(per_block_dist(gen), max_tasks_per_block);
    block_tasks = std::min(block_tasks, max_tasks_per_rank - rank_tasks);
    rank_tasks += block_tasks;
    for (int i = 0; i < block_tasks; ++i) {
      TaskType tid = static_cast<TaskType>(
        rank * max_tasks_per_rank + local_block_num * max_tasks_per_block + i
      );
      Task t{tid, rank, rank, true, TaskMemory{}, 0.0};
      t.addSharedBlock(bid);
      pd.addTask(t);
    }
    ++local_block_num;
  }
}

/**
 * Generate random working memory for tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param task_working_memory_dist Random memory distribution
 * @param task_footprint_mem Task footprint memory
 * @param task_serialized_mem Task serialized memory
 */
template <typename TaskMemoryDistType>
void generateTaskMemory(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  TaskMemoryDistType &task_working_memory_dist,
  int task_footprint_mem, int task_serialized_mem
) {
  using namespace vt_lb::model;

  auto local_ids = pd.getTaskIds();
  for (auto tid : local_ids) {
    auto t = pd.getTask(tid);
    TaskMemory m(
      task_working_memory_dist(gen), task_footprint_mem, task_serialized_mem
    );
    t->setMemory(m);
  }
}

/**
 * Generate random loads for tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param task_load_dist Random load distribution
 */
template <typename TaskLoadDistType>
void generateTaskLoads(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  TaskLoadDistType &task_load_dist
) {
  using namespace vt_lb::model;

  auto local_ids = pd.getTaskIds();
  for (auto tid : local_ids) {
    auto t = pd.getTask(tid);
    t->setLoad(task_load_dist(gen));
  }
}

/**
 * Generate random intra-rank communications on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param endpoints_per_task_dist Random endpoints per task distribution
 * @param weight_per_edge_dist Random edge weights distribution
 */
template <typename EdgesPerTaskDistType, typename WeightPerEdgeDistType>
void generateIntraRankComm(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  EdgesPerTaskDistType &endpoints_per_task_dist,
  WeightPerEdgeDistType &weight_per_edge_dist
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  auto local_ids = pd.getTaskIds();
  std::size_t num_tasks = local_ids.size();

  if (num_tasks == 1) {
    return;
  }

  std::vector<TaskType> endpoints;
  for (auto t : local_ids) {
    int count = endpoints_per_task_dist(gen);
    for (int c = 0; c < count; ++c) {
      endpoints.push_back(t);
    }
  }
  std::shuffle(endpoints.begin(), endpoints.end(), gen);

  // if we generated a odd number of endpoints, one will be dropped
  std::size_t edge_count = endpoints.size() / 2;
  std::uniform_int_distribution<> fix_self_edge_dist(0, num_tasks-1);
  for (std::size_t e = 0; e < edge_count; ++e) {
    TaskType from = endpoints[e*2];
    TaskType to = endpoints[e*2+1];
    if (from == to) {
      // avoid self-comm by minimal reshuffling
      bool fixed = false;
      for (std::size_t i = e*2+2; i < endpoints.size(); ++i) {
        if (endpoints[i] != to) {
          endpoints[e*2+1] = endpoints[i];
          endpoints[i] = to;
          to = endpoints[e*2+1];
          fixed = true;
          break;
        }
      }
      if (!fixed) {
        // fallback approach to avoiding self-comm is to break the distribution
        TaskType new_task = fix_self_edge_dist(gen);
        if (new_task != to) {
          to = new_task;
        } else {
          to = (new_task + 1) % num_tasks;
        }
      }
    }
    double bytes = weight_per_edge_dist(gen);
    pd.addCommunication(Edge{from, to, bytes, rank, rank});
  }
}

/**
 * Generate random inter-rank communications on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param endpoints_per_local_task_dist Random endpoints per local task distribution
 * @param weight_per_edge_dist Random edge weights distribution
 * @param min_tasks_per_rank The minimum number of tasks on each rank
 * @param num_ranks The number of ranks being used
 * @param locally_gen_in_edge_frac Fraction of locally generated endpoints that will be in-edges
 */
template <typename EdgesPerTaskDistType, typename WeightPerEdgeDistType>
void generateInterRankComm(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  EdgesPerTaskDistType &endpoints_per_local_task_dist,
  WeightPerEdgeDistType &weight_per_edge_dist,
  int min_tasks_per_rank, int num_ranks, double locally_gen_in_edge_frac
) {
  using namespace vt_lb::model;

  if (num_ranks == 1) {
    return;
  }

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  auto local_ids = pd.getTaskIds();
  std::vector<TaskType> local_endpoints;
  for (auto t : local_ids) {
    int count = endpoints_per_local_task_dist(gen);
    for (int c = 0; c < count; ++c) {
      local_endpoints.push_back(t);
    }
  }
  std::shuffle(local_endpoints.begin(), local_endpoints.end(), gen);

  std::size_t to_edge_count = static_cast<std::size_t>(
    local_endpoints.size() * locally_gen_in_edge_frac
  );
  std::size_t from_edge_count = local_endpoints.size() - to_edge_count;

  std::uniform_int_distribution<> remote_task_dist(0, min_tasks_per_rank-1);
  std::uniform_int_distribution<> remote_rank_dist(0, num_ranks-1);

  for (std::size_t e = 0; e < from_edge_count; ++e) {
    TaskType from = local_endpoints[e];
    int remote_rank = rank;
    while ((remote_rank = remote_rank_dist(gen)) == rank) {}
    TaskType to = remote_task_dist(gen);
    double bytes = weight_per_edge_dist(gen);
    pd.addCommunication(Edge{from, to, bytes, rank, remote_rank});
  }
  for (std::size_t e = from_edge_count; e < local_endpoints.size(); ++e) {
    TaskType to = local_endpoints[e];
    int remote_rank = rank;
    while ((remote_rank = remote_rank_dist(gen)) == rank) {}
    TaskType from = remote_task_dist(gen);
    double bytes = weight_per_edge_dist(gen);
    pd.addCommunication(Edge{from, to, bytes, remote_rank, rank});
  }
}

/**
 * Generate random intra- and inter-rank communications on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param endpoints_per_local_task_dist Random endpoints per local task distribution
 * @param weight_per_edge_dist Random edge weights distribution
 * @param min_tasks_per_rank The minimum number of tasks on each rank
 * @param num_ranks The number of ranks being used
 * @param locally_gen_in_edge_frac Fraction of locally generated endpoints that will be in-edges
 */
template <typename EdgesPerTaskDistType, typename WeightPerEdgeDistType>
void generateRankComm(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  EdgesPerTaskDistType &endpoints_per_local_task_dist,
  WeightPerEdgeDistType &weight_per_edge_dist,
  int min_tasks_per_rank, int num_ranks, double locally_gen_in_edge_frac
) {
  using namespace vt_lb::model;

  int const rank = pd.getRank();
  assert(rank != invalid_node);

  auto local_ids = pd.getTaskIds();
  if (num_ranks == 1 and local_ids.size() == 1) {
    return;
  }

  std::vector<TaskType> local_endpoints;
  for (auto t : local_ids) {
    int count = endpoints_per_local_task_dist(gen);
    for (int c = 0; c < count; ++c) {
      local_endpoints.push_back(t);
    }
  }
  std::shuffle(local_endpoints.begin(), local_endpoints.end(), gen);

  std::size_t to_edge_count = static_cast<std::size_t>(
    local_endpoints.size() * locally_gen_in_edge_frac
  );
  std::size_t from_edge_count = local_endpoints.size() - to_edge_count;

  std::uniform_int_distribution<> remote_task_dist(0, min_tasks_per_rank-1);
  std::uniform_int_distribution<> remote_rank_dist(0, num_ranks-1);

  for (std::size_t e = 0; e < from_edge_count; ++e) {
    TaskType from = local_endpoints[e];
    int remote_rank = remote_rank_dist(gen);
    TaskType to = remote_task_dist(gen);
    while ((remote_rank == rank) and ((to = remote_task_dist(gen)) == from)) {}
    double bytes = weight_per_edge_dist(gen);
    pd.addCommunication(Edge{from, to, bytes, rank, remote_rank});
  }
  for (std::size_t e = from_edge_count; e < local_endpoints.size(); ++e) {
    TaskType to = local_endpoints[e];
    int remote_rank = remote_rank_dist(gen);
    TaskType from = remote_task_dist(gen);
    while ((remote_rank == rank) and ((from = remote_task_dist(gen)) == to)) {}
    double bytes = weight_per_edge_dist(gen);
    pd.addCommunication(Edge{from, to, bytes, remote_rank, rank});
  }
}

/**
 * Generate random intra- and inter-rank communications on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param max_endpoints The maximum number of endpoints
 * @param weight_per_edge_dist Random edge weights distribution
 * @param min_tasks_per_rank The minimum number of tasks on each rank
 * @param num_ranks The number of ranks being used
 * @param locally_gen_in_edge_frac Fraction of locally generated endpoints that will be in-edges
 */
template <typename WeightPerEdgeDistType>
void generateRankComm(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen, int max_endpoints,
  WeightPerEdgeDistType &weight_per_edge_dist, int min_tasks_per_rank,
  int num_ranks, double locally_gen_in_edge_frac = 0.1
) {
  std::uniform_int_distribution<> endpoints_per_local_task_dist(
    0, max_endpoints
  );

  generateRankComm(
    pd, gen, endpoints_per_local_task_dist, weight_per_edge_dist,
    min_tasks_per_rank, num_ranks, locally_gen_in_edge_frac
  );
}

/**
 * Generate shared blocks with tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param per_rank_block_dist Random number distribution for shared blocks per rank
 * @param max_blocks_per_rank Cap for the number of shared blocks on any rank
 * @param block_memory_dist Random memory distribution for shared blocks
 * @param per_block_task_dist Random number distribution for tasks per shared block
 * @param max_tasks_per_rank The cap for the number of tasks on any rank
 * @param max_tasks_per_block The cap for the number of tasks on any block
 * @param task_load_dist Random task load distribution
 */
template <
  typename PerRankBlockDistType, typename BlockMemoryDistType,
  typename PerBlockTaskDistType, typename TaskLoadDistType
>
void generateSharedBlocksWithTasks(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  PerRankBlockDistType &per_rank_block_dist, int max_blocks_per_rank,
  BlockMemoryDistType &block_memory_dist,
  PerBlockTaskDistType &per_block_task_dist,
  int max_tasks_per_rank, int max_tasks_per_block,
  TaskLoadDistType &task_load_dist
) {
  generateSharedBlockCountsPerRank(
    pd, gen, per_rank_block_dist, max_blocks_per_rank
  );
  generateSharedBlockMemory(pd, gen, block_memory_dist);

  generateTaskCountsPerSharedBlock(
    pd, gen, per_block_task_dist, max_tasks_per_rank, max_tasks_per_block
  );
  generateTaskLoads(pd, gen, task_load_dist);
}

/**
 * Generate shared blocks with tasks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param min_blocks_per_rank Lower limit for the number of shared blocks on any rank
 * @param max_blocks_per_rank Upper limit for the number of shared blocks on any rank
 * @param min_blocks_mem Lower limit for the shared block memory
 * @param max_blocks_mem Upper limit for the shared block memory
 * @param min_tasks_per_rank Lower limit for the number of tasks on any rank
 * @param max_tasks_per_rank Upper limit for the number of tasks on any rank
 * @param max_tasks_per_block The cap for the number of tasks on any block
 * @param task_load_dist Random task load distribution
 */
template <typename TaskLoadDistType>
void generateSharedBlocksWithTasks(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen, int min_blocks_per_rank,
  int max_blocks_per_rank, int min_block_mem, int max_block_mem,
  int min_tasks_per_block, int max_tasks_per_block,
  TaskLoadDistType &task_load_dist
) {
  std::uniform_int_distribution<> block_dist(
    min_blocks_per_rank, max_blocks_per_rank
  );
  std::uniform_int_distribution<> block_mem_dist(
    min_block_mem, max_block_mem
  );
  std::uniform_int_distribution<> task_dist(
    min_tasks_per_block, max_tasks_per_block
  );

  generateSharedBlocksWithTasks(
    pd, gen, block_dist, max_blocks_per_rank, block_mem_dist, task_dist,
    max_tasks_per_block * max_blocks_per_rank, max_tasks_per_block,
    task_load_dist
  );
}

/**
 * Generate tasks without shared blocks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param per_rank_dist Random number distribution
 * @param max_tasks_per_rank The cap for the number of tasks on any rank
 * @param task_load_dist Random load distribution
 */
template <typename PerRankDistType, typename TaskLoadDistType>
void generateTasksWithoutSharedBlocks(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  PerRankDistType &per_rank_dist, int max_tasks_per_rank,
  TaskLoadDistType &task_load_dist
) {
  generateTaskCountsPerRank(pd, gen, per_rank_dist, max_tasks_per_rank);
  generateTaskLoads(pd, gen, task_load_dist);
}

/**
 * Generate tasks without shared blocks on each rank
 *
 * @param pd The PhaseData for this rank
 * @param gen The seeded generator for this rank
 * @param min_tasks_per_rank Lower limit for the number of tasks on any rank
 * @param max_tasks_per_rank Upper limit for the number of tasks on any rank
 * @param task_load_dist Random load distribution
 */
template <typename TaskLoadDistType>
void generateTasksWithoutSharedBlocks(
  vt_lb::model::PhaseData& pd, std::mt19937 &gen,
  int min_tasks_per_rank, int max_tasks_per_rank,
  TaskLoadDistType &task_load_dist
) {
  std::uniform_int_distribution<> task_dist(
    min_tasks_per_rank, max_tasks_per_rank
  );
  generateTasksWithoutSharedBlocks(
    pd, gen, task_dist, max_tasks_per_rank, task_load_dist
  );
}

/**
 * Generate scale of problem
 *
 * @param gen The seeded generator for this rank
 * @param largest_max_allowed The highest number for the range max
 * @param smallest_max_allowed The lowest number for the range max
 * @param min_as_frac_of_max The fraction of the range max used for the range min
 */
std::pair<int, int> generateScaleRel(
  std::mt19937 &gen, int largest_max_allowed, int smallest_max_allowed,
  double min_as_frac_of_max
) {
  std::uniform_int_distribution<> uni(smallest_max_allowed, largest_max_allowed);
  int max_chosen = uni(gen);
  int min_chosen = max_chosen;
  if (min_as_frac_of_max < 1.0) {
    std::uniform_int_distribution<> uni2(
      static_cast<int>(max_chosen * min_as_frac_of_max), max_chosen
    );
    min_chosen = uni2(gen);
  }
  return std::make_pair<int, int>(std::move(max_chosen), std::move(min_chosen));
}

/**
 * Generate scale of problem
 *
 * @param gen The seeded generator for this rank
 * @param largest_max_allowed The highest number for the range max
 * @param smallest_max_allowed The lowest number for the range max
 * @param min_allowed The highest number for the range min
 */
std::pair<int, int> generateScaleAbs(
  std::mt19937 &gen, int largest_max_allowed, int smallest_max_allowed,
  int min_allowed
) {
  std::uniform_int_distribution<> uni(
    smallest_max_allowed, largest_max_allowed
  );
  int max_chosen = uni(gen);
  int min_chosen = max_chosen;
  if (min_allowed < max_chosen) {
    std::uniform_int_distribution<> uni2(min_allowed, max_chosen);
    min_chosen = uni2(gen);
  }
  return std::make_pair<int, int>(std::move(max_chosen), std::move(min_chosen));
}

/**
 * Generate graph with shared blocks
 *
 * @param pd The PhaseData for this rank
 * @param num_ranks The number of ranks
 * @param uniform_shared_blocks Should all ranks have the same number of shared blocks
 * @param uniform_tasks Should all shared blocks have the same number of tasks
 * @param include_comm Should communication be generated
 * @param seed_same_across_ranks Seed that matches across ranks so they agree on scale
 * @param seed_diff_each_rank Seed that defines what gets generated on this rank
 */
void generateGraphWithSharedBlocks(
  vt_lb::model::PhaseData &pd, int num_ranks, bool uniform_shared_blocks,
  bool uniform_tasks, bool include_comm, int seed_same_across_ranks,
  int seed_diff_each_rank
) {
  std::mt19937 gen_same_across_ranks(seed_same_across_ranks);
  std::mt19937 gen_diff_each_rank(seed_diff_each_rank);

  // tune the max allowed problem size to keep tests fast enough
  int largest_allowed_max_blocks = 25;
  int smallest_allowed_max_blocks = 15;
  // tune the allowed imbalance in the number of blocks
  double min_allowed_blocks_frac = uniform_shared_blocks ? 1.0 : 0.25;

  auto [max_blocks_per_rank, min_blocks_per_rank] = generateScaleRel(
    gen_same_across_ranks, largest_allowed_max_blocks,
    smallest_allowed_max_blocks, min_allowed_blocks_frac
  );

  // tune the max allowed problem size to keep tests fast enough
  int largest_allowed_max_tasks_per_block = 20;
  int smallest_allowed_max_tasks_per_block = 5;
  // tune the allowed imbalance in the number of tasks per block
  int largest_allowed_min_tasks_per_block
    = uniform_tasks ? largest_allowed_max_tasks_per_block : 2;

  auto [max_tasks_per_block, min_tasks_per_block] = generateScaleAbs(
    gen_same_across_ranks, largest_allowed_max_tasks_per_block,
    smallest_allowed_max_tasks_per_block, largest_allowed_min_tasks_per_block
  );
  auto min_tasks_per_rank = min_blocks_per_rank * min_tasks_per_block;

  double min_load = 10.0, max_load = 20.0;
  int task_fmem = 256, task_smem = 128;
  int min_task_wmem = 1020, max_task_wmem = 2048;
  int min_block_mem = 1024*1024, max_block_mem = min_block_mem + 1024;

  std::uniform_real_distribution<> load_dist(min_load, max_load);
  generateSharedBlocksWithTasks(
    pd, gen_diff_each_rank, min_blocks_per_rank, max_blocks_per_rank,
    min_block_mem, max_block_mem, min_tasks_per_block, max_tasks_per_block,
    load_dist
  );

  std::uniform_int_distribution<> task_wmem_dist(min_task_wmem, max_task_wmem);
  generateTaskMemory(
    pd, gen_diff_each_rank, task_wmem_dist, task_fmem, task_smem
  );

  if (include_comm) {
    double edge_weight_lambda = 1000.0, locally_gen_in_edge_frac = 0.5;
    int max_endpoints = 4;

    std::exponential_distribution<> edge_weight_dist(edge_weight_lambda);
    // with such a poor estimate of min_tasks_per_rank, this will generate
    // a very odd distribution of edges
    generateRankComm(
      pd, gen_diff_each_rank, max_endpoints, edge_weight_dist,
      min_tasks_per_rank, num_ranks, locally_gen_in_edge_frac
    );
  }
}

/**
 * Generate graph without shared blocks
 *
 * @param pd The PhaseData for this rank
 * @param num_ranks The number of ranks
 * @param uniform_tasks Should all shared blocks have the same number of tasks
 * @param include_comm Should communication be generated
 * @param seed_same_across_ranks Seed that matches across ranks so they agree on scale
 * @param seed_diff_each_rank Seed that defines what gets generated on this rank
 */
void generateGraphWithoutSharedBlocks(
  vt_lb::model::PhaseData &pd, int num_ranks, bool uniform_tasks,
  bool include_comm, int seed_same_across_ranks, int seed_diff_each_rank
) {
  std::mt19937 gen_same_across_ranks(seed_same_across_ranks);
  std::mt19937 gen_diff_each_rank(seed_diff_each_rank);

  // tune the max allowed problem size to keep tests fast enough
  int max_allowed_max_tasks = 30;
  int min_allowed_max_tasks = 10;
  // tune the allowed imbalance in the number of tasks
  double min_allowed_tasks_frac = uniform_tasks ? 1.0 : 0.5;

  auto [max_tasks_per_rank, min_tasks_per_rank] = generateScaleRel(
    gen_same_across_ranks, max_allowed_max_tasks, min_allowed_max_tasks,
    min_allowed_tasks_frac
  );

  double min_load = 10.0, max_load = 20.0;
  int task_fmem = 256, task_smem = 128;
  int min_task_wmem = 1024*1024, max_task_wmem = 2048*1024;

  std::uniform_real_distribution<> load_dist(min_load, max_load);
  generateTasksWithoutSharedBlocks(
    pd, gen_diff_each_rank, min_tasks_per_rank, max_tasks_per_rank, load_dist
  );

  std::uniform_int_distribution<> task_wmem_dist(min_task_wmem, max_task_wmem);
  generateTaskMemory(
    pd, gen_diff_each_rank, task_wmem_dist, task_fmem, task_smem
  );

  if (include_comm) {
    double edge_weight_lambda = 100000.0;
    int max_endpoints = 6;

    std::exponential_distribution<> edge_weight_dist(edge_weight_lambda);
    generateRankComm(
      pd, gen_diff_each_rank, max_endpoints, edge_weight_dist,
      min_tasks_per_rank, num_ranks
    );
  }
}

}}} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_UNIT_GRAPH_HELPERS_H*/
