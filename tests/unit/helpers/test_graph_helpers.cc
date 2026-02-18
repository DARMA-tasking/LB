/*
//@HEADER
// *****************************************************************************
//
//                              test_graph_helpers.cc
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

#include <vt-lb/model/PhaseData.h>

#include <random>

namespace vt_lb { namespace tests { namespace unit {

template <comm::Communicator CommType>
struct TestGraphHelpers: TestParallelHarness<CommType> {
};

TYPED_TEST_SUITE(TestGraphHelpers, CommTypesForTesting, CommNameGenerator);

TYPED_TEST(TestGraphHelpers, test_generate_rank_footprint_bytes) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(3635 * rank);

  int min_mem = 100;
  int max_mem = 101;
  std::uniform_int_distribution<> dist(min_mem, max_mem);

  generateRankFootprintBytes(pd, gen, dist);

  // check that the value falls within the distribution
  EXPECT_GE(pd.getRankFootprintBytes(), min_mem);
  EXPECT_LE(pd.getRankFootprintBytes(), max_mem);
};

TYPED_TEST(TestGraphHelpers, test_generate_shared_block_counts_per_rank) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(837 * rank);

  int min_blocks = 10;
  int max_blocks = 30;
  std::uniform_int_distribution<> dist(min_blocks, max_blocks);

  generateSharedBlockCountsPerRank(pd, gen, dist, max_blocks);

  // check that the value falls within the distribution
  auto &blocks = pd.getSharedBlocksMap();
  int count = blocks.size();
  EXPECT_GE(count, min_blocks);
  EXPECT_LE(count, max_blocks);
};

TYPED_TEST(TestGraphHelpers, test_generate_shared_block_counts_per_rank_limit) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(777 * rank);

  int min_blocks = 17;
  int max_blocks = 300;
  int max_blocks_limit = 18;
  std::uniform_int_distribution<> dist(min_blocks, max_blocks);

  generateSharedBlockCountsPerRank(pd, gen, dist, max_blocks_limit);

  // check that the value falls within the distribution
  auto &blocks = pd.getSharedBlocksMap();
  int count = blocks.size();
  EXPECT_GE(count, min_blocks);
  EXPECT_LE(count, max_blocks);
  EXPECT_LE(count, max_blocks_limit);
};

TYPED_TEST(TestGraphHelpers, test_generate_shared_block_memory) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(697 * rank);

  int exact_blocks = 20;
  std::uniform_int_distribution<> count_dist(exact_blocks, exact_blocks);

  generateSharedBlockCountsPerRank(pd, gen, count_dist, exact_blocks);

  auto &blocks = pd.getSharedBlocksMap();
  int count = blocks.size();
  EXPECT_EQ(count, exact_blocks);

  int min_mem = 10000;
  int max_mem = 100000;
  std::uniform_int_distribution<> mem_dist(min_mem, max_mem);

  generateSharedBlockMemory(pd, gen, mem_dist);

  // check that the values fall within the distribution
  for (auto &b : blocks) {
    EXPECT_GE(b.second.getSize(), min_mem);
    EXPECT_LE(b.second.getSize(), max_mem);
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_tasks_per_rank) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(4341 * rank);

  int min_tasks = 10;
  int max_tasks = 30;
  std::uniform_int_distribution<> dist(min_tasks, max_tasks);

  generateTaskCountsPerRank(pd, gen, dist, max_tasks);

  // check that the value falls within the distribution
  auto &tasks = pd.getTasksMap();
  int count = tasks.size();
  EXPECT_GE(count, min_tasks);
  EXPECT_LE(count, max_tasks);
};

TYPED_TEST(TestGraphHelpers, test_generate_tasks_per_rank_limit) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(7846 * rank);

  int min_tasks = 12;
  int max_tasks = 200;
  int max_tasks_limit = 14;
  std::uniform_int_distribution<> dist(min_tasks, max_tasks);

  generateTaskCountsPerRank(pd, gen, dist, max_tasks_limit);

  // check that the value falls within the distribution
  auto &tasks = pd.getTasksMap();
  int count = tasks.size();
  EXPECT_GE(count, min_tasks);
  EXPECT_LE(count, max_tasks);
  EXPECT_LE(count, max_tasks_limit);
};

TYPED_TEST(TestGraphHelpers, test_generate_task_counts_per_shared_block) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(51 * rank);

  int min_blocks = 8;
  int max_blocks = 15;
  std::uniform_int_distribution<> block_dist(min_blocks, max_blocks);

  generateSharedBlockCountsPerRank(pd, gen, block_dist, max_blocks);

  auto block_ids = pd.getSharedBlockIds();
  int block_count = block_ids.size();

  int min_tasks_per_block = 2;
  int max_tasks_per_block = 7;
  std::uniform_int_distribution<> task_dist(
    min_tasks_per_block, max_tasks_per_block
  );
  int max_tasks_per_rank = max_blocks * max_tasks_per_block;

  generateTaskCountsPerSharedBlock(
    pd, gen, task_dist, max_tasks_per_rank, max_tasks_per_block
  );

  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  EXPECT_LE(task_count, max_tasks_per_rank);
  EXPECT_GE(task_count, block_count * min_tasks_per_block);
  EXPECT_LE(task_count, block_count * max_tasks_per_block);

  // check that the values fall within the distribution
  for (auto bid : block_ids) {
    int count_this_block = 0;
    for (auto tid : task_ids) {
      auto t = pd.getTask(tid);
      auto &bids_this_task = t->getSharedBlocks();
      for (auto block_id : bids_this_task) {
        if (block_id == bid) {
          ++count_this_block;
        }
      }
    }
    EXPECT_GE(count_this_block, min_tasks_per_block);
    EXPECT_LE(count_this_block, max_tasks_per_block);
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_task_counts_per_shared_block_limit) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(99 * rank);

  int exact_blocks = 10;
  std::uniform_int_distribution<> block_dist(exact_blocks, exact_blocks);

  generateSharedBlockCountsPerRank(pd, gen, block_dist, exact_blocks);

  auto block_ids = pd.getSharedBlockIds();

  int max_tasks_per_rank = 37;
  int min_tasks_per_block = 4;
  int max_tasks_per_block = 5;
  std::uniform_int_distribution<> task_dist(
    min_tasks_per_block, max_tasks_per_block
  );

  generateTaskCountsPerSharedBlock(
    pd, gen, task_dist, max_tasks_per_rank, max_tasks_per_block
  );

  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  EXPECT_EQ(task_count, max_tasks_per_rank);

  // check that the values fall within the distribution
  int running_count = 0;
  for (auto bid : block_ids) {
    int count_this_block = 0;
    for (auto tid : task_ids) {
      auto t = pd.getTask(tid);
      auto &bids_this_task = t->getSharedBlocks();
      for (auto block_id : bids_this_task) {
        if (block_id == bid) {
          ++count_this_block;
          ++running_count;
        }
      }
    }
    EXPECT_LE(count_this_block, max_tasks_per_block);
    if (running_count < max_tasks_per_rank) {
      EXPECT_GE(count_this_block, min_tasks_per_block);
    }
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_task_memory) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(697 * rank);

  int exact_tasks = 20;
  std::uniform_int_distribution<> count_dist(exact_tasks, exact_tasks);

  generateTaskCountsPerRank(pd, gen, count_dist, exact_tasks);

  auto &tasks = pd.getTasksMap();
  int count = tasks.size();
  EXPECT_EQ(count, exact_tasks);

  int fmem = 256;
  int smem = 128;
  int min_wmem = 1000;
  int max_wmem = 3000;
  std::uniform_int_distribution<> wmem_dist(min_wmem, max_wmem);

  generateTaskMemory(pd, gen, wmem_dist, fmem, smem);

  // check that the values fall within the distribution
  for (auto &t : tasks) {
    auto &tm = t.second.getMemory();
    EXPECT_GE(tm.getWorking(), min_wmem);
    EXPECT_LE(tm.getWorking(), max_wmem);
    EXPECT_EQ(tm.getFootprint(), fmem);
    EXPECT_EQ(tm.getSerialized(), smem);
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_task_loads) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(11 * rank);

  int exact_tasks = 13;
  std::uniform_int_distribution<> count_dist(exact_tasks, exact_tasks);

  generateTaskCountsPerRank(pd, gen, count_dist, exact_tasks);

  auto &tasks = pd.getTasksMap();
  int count = tasks.size();
  EXPECT_EQ(count, exact_tasks);

  double min_load = 10.1;
  double max_load = 49.2;
  std::uniform_real_distribution<> load_dist(min_load, max_load);

  generateTaskLoads(pd, gen, load_dist);

  // check that the values fall within the distribution
  for (auto &t : tasks) {
    EXPECT_GE(t.second.getLoad(), min_load);
    EXPECT_LE(t.second.getLoad(), max_load);
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_shared_blocks_with_tasks) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(765 * rank);

  int min_blocks = 4;
  int max_blocks = 7;
  std::uniform_int_distribution<> block_dist(min_blocks, max_blocks);

  int min_bmem = 400000;
  int max_bmem = 500000;
  std::uniform_int_distribution<> bmem_dist(min_bmem, max_bmem);

  int min_tasks_per_block = 5;
  int max_tasks_per_block = 8;
  std::uniform_int_distribution<> task_dist(
    min_tasks_per_block, max_tasks_per_block
  );
  int max_tasks_per_rank = max_blocks * max_tasks_per_block;

  double min_load = 456.7;
  double max_load = 678.9;
  std::uniform_real_distribution<> load_dist(min_load, max_load);

  generateSharedBlocksWithTasks(
    pd, gen, block_dist, max_blocks, bmem_dist, task_dist, max_tasks_per_rank,
    max_tasks_per_block, load_dist
  );

  // check block counts
  auto block_ids = pd.getSharedBlockIds();
  int block_count = block_ids.size();
  EXPECT_GE(block_count, min_blocks);
  EXPECT_LE(block_count, max_blocks);

  // check block memory
  for (auto bid : block_ids) {
    auto b = pd.getSharedBlock(bid);
    EXPECT_GE(b->getSize(), min_bmem);
    EXPECT_LE(b->getSize(), max_bmem);
  }

  // check task count on rank
  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  EXPECT_LE(task_count, max_tasks_per_rank);
  EXPECT_GE(task_count, block_count * min_tasks_per_block);
  EXPECT_LE(task_count, block_count * max_tasks_per_block);

  // check task count per block
  for (auto bid : block_ids) {
    int count_this_block = 0;
    for (auto tid : task_ids) {
      auto t = pd.getTask(tid);
      auto &bids_this_task = t->getSharedBlocks();
      for (auto block_id : bids_this_task) {
        if (block_id == bid) {
          ++count_this_block;
        }
      }
    }
    EXPECT_GE(count_this_block, min_tasks_per_block);
    EXPECT_LE(count_this_block, max_tasks_per_block);
  }

  // check task loads
  for (auto tid : task_ids) {
    auto t = pd.getTask(tid);
    EXPECT_GE(t->getLoad(), min_load);
    EXPECT_LE(t->getLoad(), max_load);
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_shared_blocks_with_tasks2) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(991 * rank);

  int min_blocks = 3;
  int max_blocks = 8;

  int min_bmem = 6000000;
  int max_bmem = 8000000;

  int min_tasks_per_block = 4;
  int max_tasks_per_block = 7;
  int max_tasks_per_rank = max_blocks * max_tasks_per_block;

  double min_load = 45.6;
  double max_load = 78.9;
  std::uniform_real_distribution<> load_dist(min_load, max_load);

  generateSharedBlocksWithTasks(
    pd, gen, min_blocks, max_blocks, min_bmem, max_bmem, min_tasks_per_block,
    max_tasks_per_block, load_dist
  );

  // check block counts
  auto block_ids = pd.getSharedBlockIds();
  int block_count = block_ids.size();
  EXPECT_GE(block_count, min_blocks);
  EXPECT_LE(block_count, max_blocks);

  // check block memory
  for (auto bid : block_ids) {
    auto b = pd.getSharedBlock(bid);
    EXPECT_GE(b->getSize(), min_bmem);
    EXPECT_LE(b->getSize(), max_bmem);
  }

  // check task count on rank
  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  EXPECT_LE(task_count, max_tasks_per_rank);
  EXPECT_GE(task_count, block_count * min_tasks_per_block);
  EXPECT_LE(task_count, block_count * max_tasks_per_block);

  // check task count per block
  for (auto bid : block_ids) {
    int count_this_block = 0;
    for (auto tid : task_ids) {
      auto t = pd.getTask(tid);
      auto &bids_this_task = t->getSharedBlocks();
      for (auto block_id : bids_this_task) {
        if (block_id == bid) {
          ++count_this_block;
        }
      }
    }
    EXPECT_GE(count_this_block, min_tasks_per_block);
    EXPECT_LE(count_this_block, max_tasks_per_block);
  }

  // check task loads
  for (auto tid : task_ids) {
    auto t = pd.getTask(tid);
    EXPECT_GE(t->getLoad(), min_load);
    EXPECT_LE(t->getLoad(), max_load);
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_tasks_without_shared_blocks) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(808 * rank);

  int min_tasks = 5;
  int max_tasks = 500;
  int max_tasks_limit = 7;
  std::uniform_int_distribution<> task_dist(min_tasks, max_tasks);

  double min_load = 3469.9;
  double max_load = 95795.0;
  std::uniform_real_distribution<> load_dist(min_load, max_load);

  generateTasksWithoutSharedBlocks(
    pd, gen, task_dist, max_tasks_limit, load_dist
  );

  // check task counts
  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  EXPECT_LE(task_count, max_tasks);
  EXPECT_GE(task_count, min_tasks);
  EXPECT_LE(task_count, max_tasks_limit);

  // check task loads
  for (auto tid : task_ids) {
    auto t = pd.getTask(tid);
    EXPECT_GE(t->getLoad(), min_load);
    EXPECT_LE(t->getLoad(), max_load);
  }
};

TYPED_TEST(TestGraphHelpers, test_generate_tasks_without_shared_blocks2) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(723 * rank);

  int min_tasks_per_rank = 8;
  int max_tasks_per_rank = 12;

  double min_load = 128.6;
  double max_load = 199.4;
  std::uniform_real_distribution<> load_dist(min_load, max_load);

  generateTasksWithoutSharedBlocks(
    pd, gen, min_tasks_per_rank, max_tasks_per_rank, load_dist
  );

  // check task counts
  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  EXPECT_GE(task_count, min_tasks_per_rank);
  EXPECT_LE(task_count, max_tasks_per_rank);

  // check task loads
  for (auto tid : task_ids) {
    auto t = pd.getTask(tid);
    EXPECT_GE(t->getLoad(), min_load);
    EXPECT_LE(t->getLoad(), max_load);
  }
};

// TYPED_TEST(TestGraphHelpers, test_generate_intra_rank_comm) {
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   std::mt19937 gen(6745 * rank);

//   int min_tasks = 10;
//   int max_tasks = 20;
//   std::uniform_int_distribution<> dist(min_tasks, max_tasks);

//   generateTaskCountsPerRank(pd, gen, dist, max_tasks);

//   int task_count = pd.getTasksMap().size();

//   int min_endpoints = 3;
//   int max_endpoints = 5;
//   std::uniform_int_distribution<> ep_dist(min_endpoints, max_endpoints);

//   int min_weight = 100;
//   int max_weight = 200;
//   std::uniform_int_distribution<> weight_dist(min_weight, max_weight);

//   generateIntraRankComm(pd, gen, ep_dist, weight_dist);

//   auto &edges = pd.getCommunications();

//   if (task_count <= 1) {
//     EXPECT_EQ(edges.size(), 0);
//     return;
//   }

//   EXPECT_GE(edges.size(), task_count * min_endpoints / 2);
//   EXPECT_LE(edges.size(), task_count * max_endpoints / 2);

//   std::vector<int> from_task(task_count);
//   std::vector<int> to_task(task_count);

//   for (auto &e : edges) {
//     EXPECT_EQ(e.getFromRank(), rank);
//     EXPECT_EQ(e.getToRank(), rank);
//     EXPECT_GE(e.getVolume(), min_weight);
//     EXPECT_LE(e.getVolume(), max_weight);

//     auto from_task_id = e.getFrom();
//     auto to_task_id = e.getTo();
//     EXPECT_GE(from_task_id, rank * max_tasks);
//     EXPECT_LT(from_task_id, (rank + 1) * max_tasks);
//     EXPECT_GE(to_task_id, rank * max_tasks);
//     EXPECT_LT(to_task_id, (rank + 1) * max_tasks);

//     int from_lid = from_task_id - rank * max_tasks;
//     int to_lid = to_task_id - rank * max_tasks;
//     if (from_lid >= 0 and from_lid < task_count) {
//       ++(from_task[from_lid]);
//     }
//     if (to_lid >= 0 and to_lid < task_count) {
//       ++(to_task[to_lid]);
//     }
//   }

//   for (int tlid = 0; tlid < task_count; ++tlid) {
//     // in case we generated an odd number of endpoints and one was dropped
//     int adjusted_min_endpoints = std::max(min_endpoints - 1, 0);
//     EXPECT_GE(from_task[tlid] + to_task[tlid], adjusted_min_endpoints);
//     EXPECT_LE(from_task[tlid] + to_task[tlid], max_endpoints);
//   }
// };

// TYPED_TEST(TestGraphHelpers, test_generate_inter_rank_comm_out_only) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   std::mt19937 gen(741 * rank);

//   int min_tasks = 5;
//   int max_tasks = 7;
//   std::uniform_int_distribution<> dist(min_tasks, max_tasks);

//   generateTaskCountsPerRank(pd, gen, dist, max_tasks);

//   int task_count = pd.getTasksMap().size();

//   int min_endpoints = 1;
//   int max_endpoints = 4;
//   std::uniform_int_distribution<> ep_dist(min_endpoints, max_endpoints);

//   int min_weight = 1000;
//   int max_weight = 2000;
//   std::uniform_int_distribution<> weight_dist(min_weight, max_weight);

//   double frac = 0.0;

//   generateInterRankComm(
//     pd, gen, ep_dist, weight_dist, min_tasks, num_ranks, frac
//   );

//   auto &edges = pd.getCommunications();

//   if (num_ranks == 1) {
//     EXPECT_EQ(edges.size(), 0);
//     return;
//   }

//   EXPECT_GE(edges.size(), task_count * min_endpoints);
//   EXPECT_LE(edges.size(), task_count * max_endpoints);

//   std::vector<int> from_task(task_count);

//   for (auto &e : edges) {
//     EXPECT_EQ(e.getFromRank(), rank);
//     EXPECT_NE(e.getToRank(), rank);
//     EXPECT_GE(e.getVolume(), min_weight);
//     EXPECT_LE(e.getVolume(), max_weight);

//     auto from_task_id = e.getFrom();
//     EXPECT_GE(from_task_id, rank * max_tasks);
//     EXPECT_LT(from_task_id, (rank + 1) * max_tasks);

//     int from_lid = from_task_id - rank * max_tasks;
//     if (from_lid >= 0 and from_lid < task_count) {
//       ++(from_task[from_lid]);
//     }
//   }

//   for (int tlid = 0; tlid < task_count; ++tlid) {
//     EXPECT_GE(from_task[tlid], min_endpoints);
//     EXPECT_LE(from_task[tlid], max_endpoints);
//   }
// };

// TYPED_TEST(TestGraphHelpers, test_generate_inter_rank_comm_in_only) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   std::mt19937 gen(975 * rank);

//   int min_tasks = 4;
//   int max_tasks = 8;
//   std::uniform_int_distribution<> dist(min_tasks, max_tasks);

//   generateTaskCountsPerRank(pd, gen, dist, max_tasks);

//   int task_count = pd.getTasksMap().size();

//   int min_endpoints = 2;
//   int max_endpoints = 5;
//   std::uniform_int_distribution<> ep_dist(min_endpoints, max_endpoints);

//   int min_weight = 500;
//   int max_weight = 3000;
//   std::uniform_int_distribution<> weight_dist(min_weight, max_weight);

//   double frac = 1.0;

//   generateInterRankComm(
//     pd, gen, ep_dist, weight_dist, min_tasks, num_ranks, frac
//   );

//   auto &edges = pd.getCommunications();

//   if (num_ranks == 1) {
//     EXPECT_EQ(edges.size(), 0);
//     return;
//   }

//   EXPECT_GE(edges.size(), task_count * min_endpoints);
//   EXPECT_LE(edges.size(), task_count * max_endpoints);

//   std::vector<int> to_task(task_count);

//   for (auto &e : edges) {
//     EXPECT_NE(e.getFromRank(), rank);
//     EXPECT_EQ(e.getToRank(), rank);
//     EXPECT_GE(e.getVolume(), min_weight);
//     EXPECT_LE(e.getVolume(), max_weight);

//     auto to_task_id = e.getTo();
//     EXPECT_GE(to_task_id, rank * max_tasks);
//     EXPECT_LT(to_task_id, (rank + 1) * max_tasks);

//     int to_lid = to_task_id - rank * max_tasks;
//     if (to_lid >= 0 and to_lid < task_count) {
//       ++(to_task[to_lid]);
//     }
//   }

//   for (int tlid = 0; tlid < task_count; ++tlid) {
//     EXPECT_GE(to_task[tlid], min_endpoints);
//     EXPECT_LE(to_task[tlid], max_endpoints);
//   }
// };

// TYPED_TEST(TestGraphHelpers, test_generate_rank_comm_out_only) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   std::mt19937 gen(468 * rank);

//   int min_tasks = 3;
//   int max_tasks = 8;
//   std::uniform_int_distribution<> dist(min_tasks, max_tasks);

//   generateTaskCountsPerRank(pd, gen, dist, max_tasks);

//   int task_count = pd.getTasksMap().size();

//   int min_endpoints = 3;
//   int max_endpoints = 6;
//   std::uniform_int_distribution<> ep_dist(min_endpoints, max_endpoints);

//   int min_weight = 10000;
//   int max_weight = 20000;
//   std::uniform_int_distribution<> weight_dist(min_weight, max_weight);

//   double frac = 0.0;

//   generateRankComm(
//     pd, gen, ep_dist, weight_dist, min_tasks, num_ranks, frac
//   );

//   auto &edges = pd.getCommunications();

//   if (num_ranks == 1 and task_count <= 1) {
//     EXPECT_EQ(edges.size(), 0);
//     return;
//   }

//   EXPECT_GE(edges.size(), task_count * min_endpoints);
//   EXPECT_LE(edges.size(), task_count * max_endpoints);

//   std::vector<int> from_task(task_count);

//   for (auto &e : edges) {
//     EXPECT_EQ(e.getFromRank(), rank);
//     EXPECT_GE(e.getVolume(), min_weight);
//     EXPECT_LE(e.getVolume(), max_weight);

//     auto from_task_id = e.getFrom();
//     EXPECT_GE(from_task_id, rank * max_tasks);
//     EXPECT_LT(from_task_id, (rank + 1) * max_tasks);

//     int from_lid = from_task_id - rank * max_tasks;
//     if (from_lid >= 0 and from_lid < task_count) {
//       ++(from_task[from_lid]);
//     }
//   }

//   for (int tlid = 0; tlid < task_count; ++tlid) {
//     EXPECT_GE(from_task[tlid], min_endpoints);
//     EXPECT_LE(from_task[tlid], max_endpoints);
//   }
// };

// TYPED_TEST(TestGraphHelpers, test_generate_rank_comm_in_only) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   std::mt19937 gen(357 * rank);

//   int min_tasks = 2;
//   int max_tasks = 8;
//   std::uniform_int_distribution<> dist(min_tasks, max_tasks);

//   generateTaskCountsPerRank(pd, gen, dist, max_tasks);

//   int task_count = pd.getTasksMap().size();

//   int min_endpoints = 1;
//   int max_endpoints = 7;
//   std::uniform_int_distribution<> ep_dist(min_endpoints, max_endpoints);

//   int min_weight = 3000;
//   int max_weight = 5000;
//   std::uniform_int_distribution<> weight_dist(min_weight, max_weight);

//   double frac = 1.0;

//   generateRankComm(
//     pd, gen, ep_dist, weight_dist, min_tasks, num_ranks, frac
//   );

//   auto &edges = pd.getCommunications();

//   if (num_ranks == 1 and task_count <= 1) {
//     EXPECT_EQ(edges.size(), 0);
//     return;
//   }

//   EXPECT_GE(edges.size(), task_count * min_endpoints);
//   EXPECT_LE(edges.size(), task_count * max_endpoints);

//   std::vector<int> to_task(task_count);

//   for (auto &e : edges) {
//     EXPECT_EQ(e.getToRank(), rank);
//     EXPECT_GE(e.getVolume(), min_weight);
//     EXPECT_LE(e.getVolume(), max_weight);

//     auto to_task_id = e.getTo();
//     EXPECT_GE(to_task_id, rank * max_tasks);
//     EXPECT_LT(to_task_id, (rank + 1) * max_tasks);

//     int to_lid = to_task_id - rank * max_tasks;
//     if (to_lid >= 0 and to_lid < task_count) {
//       ++(to_task[to_lid]);
//     }
//   }

//   for (int tlid = 0; tlid < task_count; ++tlid) {
//     EXPECT_GE(to_task[tlid], min_endpoints);
//     EXPECT_LE(to_task[tlid], max_endpoints);
//   }
// };

// TYPED_TEST(TestGraphHelpers, test_generate_rank_comm_out_only2) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   std::mt19937 gen(147 * rank);

//   int min_tasks = 4;
//   int max_tasks = 10;
//   std::uniform_int_distribution<> dist(min_tasks, max_tasks);

//   generateTaskCountsPerRank(pd, gen, dist, max_tasks);

//   int task_count = pd.getTasksMap().size();

//   int max_endpoints = 4;

//   int min_weight = 1000;
//   int max_weight = 20000;
//   std::uniform_int_distribution<> weight_dist(min_weight, max_weight);

//   double frac = 0.0;

//   generateRankComm(
//     pd, gen, max_endpoints, weight_dist, min_tasks, num_ranks, frac
//   );

//   auto &edges = pd.getCommunications();

//   if (num_ranks == 1 and task_count <= 1) {
//     EXPECT_EQ(edges.size(), 0);
//     return;
//   }

//   EXPECT_LE(edges.size(), task_count * max_endpoints);

//   std::vector<int> from_task(task_count);

//   for (auto &e : edges) {
//     EXPECT_EQ(e.getFromRank(), rank);
//     EXPECT_GE(e.getVolume(), min_weight);
//     EXPECT_LE(e.getVolume(), max_weight);

//     auto from_task_id = e.getFrom();
//     EXPECT_GE(from_task_id, rank * max_tasks);
//     EXPECT_LT(from_task_id, (rank + 1) * max_tasks);

//     int from_lid = from_task_id - rank * max_tasks;
//     if (from_lid >= 0 and from_lid < task_count) {
//       ++(from_task[from_lid]);
//     }
//   }

//   for (int tlid = 0; tlid < task_count; ++tlid) {
//     EXPECT_LE(from_task[tlid], max_endpoints);
//   }
// };

// TYPED_TEST(TestGraphHelpers, test_generate_rank_comm_in_only2) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   std::mt19937 gen(258 * rank);

//   int min_tasks = 5;
//   int max_tasks = 9;
//   std::uniform_int_distribution<> dist(min_tasks, max_tasks);

//   generateTaskCountsPerRank(pd, gen, dist, max_tasks);

//   int task_count = pd.getTasksMap().size();

//   int max_endpoints = 7;

//   int min_weight = 70;
//   int max_weight = 300;
//   std::uniform_int_distribution<> weight_dist(min_weight, max_weight);

//   double frac = 1.0;

//   generateRankComm(
//     pd, gen, max_endpoints, weight_dist, min_tasks, num_ranks, frac
//   );

//   auto &edges = pd.getCommunications();

//   if (num_ranks == 1 and task_count <= 1) {
//     EXPECT_EQ(edges.size(), 0);
//     return;
//   }

//   EXPECT_LE(edges.size(), task_count * max_endpoints);

//   std::vector<int> to_task(task_count);

//   for (auto &e : edges) {
//     EXPECT_EQ(e.getToRank(), rank);
//     EXPECT_GE(e.getVolume(), min_weight);
//     EXPECT_LE(e.getVolume(), max_weight);

//     auto to_task_id = e.getTo();
//     EXPECT_GE(to_task_id, rank * max_tasks);
//     EXPECT_LT(to_task_id, (rank + 1) * max_tasks);

//     int to_lid = to_task_id - rank * max_tasks;
//     if (to_lid >= 0 and to_lid < task_count) {
//       ++(to_task[to_lid]);
//     }
//   }

//   for (int tlid = 0; tlid < task_count; ++tlid) {
//     EXPECT_LE(to_task[tlid], max_endpoints);
//   }
// };

// TYPED_TEST(TestGraphHelpers, test_generate_scale_rel) {
//   std::mt19937 gen(123456);

//   int largest_max_allowed = 100;
//   int smallest_max_allowed = 50;
//   double min_as_frac_of_max = 0.3;

//   auto [max_chosen, min_chosen] = generateScaleRel(
//     gen, largest_max_allowed, smallest_max_allowed, min_as_frac_of_max
//   );

//   EXPECT_GE(max_chosen, smallest_max_allowed);
//   EXPECT_LE(max_chosen, largest_max_allowed);
//   EXPECT_GE(min_chosen, static_cast<int>(max_chosen * min_as_frac_of_max));
// };

// TYPED_TEST(TestGraphHelpers, test_generate_scale_abs) {
//   std::mt19937 gen(123456);

//   int largest_max_allowed = 100;
//   int smallest_max_allowed = 50;
//   int min_allowed = 40;

//   auto [max_chosen, min_chosen] = generateScaleAbs(
//     gen, largest_max_allowed, smallest_max_allowed, min_allowed
//   );

//   EXPECT_GE(max_chosen, smallest_max_allowed);
//   EXPECT_LE(max_chosen, largest_max_allowed);
//   EXPECT_GE(min_chosen, min_allowed);
// };

void sanityCheckBlockMem(const vt_lb::model::PhaseData &pd) {
  // check block memory
  auto block_ids = pd.getSharedBlockIds();
  for (auto bid : block_ids) {
    auto b = pd.getSharedBlock(bid);
    EXPECT_GT(b->getSize(), 0);
  }
}

void sanityCheckBlocks(const vt_lb::model::PhaseData &pd, bool mem_required) {
  // check block count
  auto block_ids = pd.getSharedBlockIds();
  int block_count = block_ids.size();
  EXPECT_GT(block_count, 0);

  if (mem_required) {
    sanityCheckBlockMem(pd);
  }
}

void sanityCheckBlockTasks(
  const vt_lb::model::PhaseData &pd, bool uni_across_blocks
) {
  // check task count per block
  auto task_ids = pd.getTaskIds();
  auto block_ids = pd.getSharedBlockIds();
  int first_count = -1;
  for (auto bid : block_ids) {
    int count_this_block = 0;
    for (auto tid : task_ids) {
      auto t = pd.getTask(tid);
      auto &bids_this_task = t->getSharedBlocks();
      for (auto block_id : bids_this_task) {
        if (block_id == bid) {
          ++count_this_block;
        }
      }
    }
    if (!uni_across_blocks or first_count < 0) {
      EXPECT_GT(count_this_block, 0);
      first_count = count_this_block;
    } else {
      EXPECT_EQ(count_this_block, first_count);
    }
  }
}

void sanityCheckTaskLoads(const vt_lb::model::PhaseData &pd) {
  // check task loads
  auto task_ids = pd.getTaskIds();
  for (auto tid : task_ids) {
    auto t = pd.getTask(tid);
    EXPECT_GT(t->getLoad(), 0.0);
  }
}

void sanityCheckTaskMem(const vt_lb::model::PhaseData &pd) {
  // check task memory
  auto task_ids = pd.getTaskIds();
  for (auto tid : task_ids) {
    auto t = pd.getTask(tid);
    auto &tm = t->getMemory();
    EXPECT_GT(tm.getWorking(), 0);
    EXPECT_GT(tm.getFootprint(), 0);
    EXPECT_GT(tm.getSerialized(), 0);
  }
}

void sanityCheckTasks(
  const vt_lb::model::PhaseData &pd, bool mem_required, bool uni_across_blocks
) {
  auto block_ids = pd.getSharedBlockIds();
  int block_count = block_ids.size();

  // check task count
  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  EXPECT_GE(task_count, block_count);

  if (block_count > 0) {
    sanityCheckBlockTasks(pd, uni_across_blocks);
  }

  sanityCheckTaskLoads(pd);

  if (mem_required) {
    sanityCheckTaskMem(pd);
  }
}

void sanityCheckEdges(const vt_lb::model::PhaseData &pd, bool expect_edges, int num_ranks) {
  // check edges
  auto task_ids = pd.getTaskIds();
  int task_count = task_ids.size();
  auto &edges = pd.getCommunications();
  if (!expect_edges or (num_ranks == 1 and task_count <= 1)) {
    EXPECT_EQ(edges.size(), 0);
  } else {
    // this allows zero because we could have drawn all zero endpoint counts;
    // we should figure out how to test this better because it will still pass
    // if edge-generation was skipped altogether
    EXPECT_GE(edges.size(), 0);
  }

  // check edge weights
  for (auto &e : edges) {
    EXPECT_GT(e.getVolume(), 0.0);
  }
}

TYPED_TEST(TestGraphHelpers, test_generate_graph_with_shared_blocks_no_comm) {
  auto num_ranks = this->comm.numRanks();
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  int seed_same_across_ranks = 12;
  int seed_diff_each_rank = 34 * rank;

  bool uniform_shared_block_count = false;
  bool uniform_task_count = false;
  bool include_comm = false;

  generateGraphWithSharedBlocks(
    pd, num_ranks, uniform_shared_block_count, uniform_task_count, include_comm,
    seed_same_across_ranks, seed_diff_each_rank
  );

  sanityCheckBlocks(pd, true);
  sanityCheckTasks(pd, true, uniform_task_count);
  sanityCheckEdges(pd, include_comm, num_ranks);
};

// TYPED_TEST(TestGraphHelpers, test_generate_graph_with_shared_blocks_with_comm) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   int seed_same_across_ranks = 13;
//   int seed_diff_each_rank = 35 * rank;

//   bool uniform_shared_block_count = false;
//   bool uniform_task_count = false;
//   bool include_comm = true;

//   generateGraphWithSharedBlocks(
//     pd, num_ranks, uniform_shared_block_count, uniform_task_count, include_comm,
//     seed_same_across_ranks, seed_diff_each_rank
//   );

//   sanityCheckBlocks(pd, true);
//   sanityCheckTasks(pd, true, uniform_task_count);
//   sanityCheckEdges(pd, include_comm, num_ranks);
// };

TYPED_TEST(TestGraphHelpers, test_generate_graph_with_shared_blocks_no_comm_unib) {
  auto num_ranks = this->comm.numRanks();
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  int seed_same_across_ranks = 14;
  int seed_diff_each_rank = 36 * rank;

  bool uniform_shared_block_count = true;
  bool uniform_task_count = false;
  bool include_comm = false;

  generateGraphWithSharedBlocks(
    pd, num_ranks, uniform_shared_block_count, uniform_task_count, include_comm,
    seed_same_across_ranks, seed_diff_each_rank
  );

  sanityCheckBlocks(pd, true);
  sanityCheckTasks(pd, true, uniform_task_count);
  sanityCheckEdges(pd, include_comm, num_ranks);
};

TYPED_TEST(TestGraphHelpers, test_generate_graph_with_shared_blocks_no_comm_unibt) {
  auto num_ranks = this->comm.numRanks();
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  int seed_same_across_ranks = 15;
  int seed_diff_each_rank = 37 * rank;

  bool uniform_shared_block_count = true;
  bool uniform_task_count = true;
  bool include_comm = false;

  generateGraphWithSharedBlocks(
    pd, num_ranks, uniform_shared_block_count, uniform_task_count, include_comm,
    seed_same_across_ranks, seed_diff_each_rank
  );

  sanityCheckBlocks(pd, true);
  sanityCheckTasks(pd, true, uniform_task_count);
  sanityCheckEdges(pd, include_comm, num_ranks);
};

// TYPED_TEST(TestGraphHelpers, test_generate_graph_without_shared_blocks_with_comm) {
//   auto num_ranks = this->comm.numRanks();
//   auto rank = this->comm.getRank();
//   vt_lb::model::PhaseData pd(rank);

//   int seed_same_across_ranks = 16;
//   int seed_diff_each_rank = 38 * rank;

//   bool uniform_task_count = false;
//   bool include_comm = true;

//   generateGraphWithoutSharedBlocks(
//     pd, num_ranks, uniform_task_count, include_comm, seed_same_across_ranks,
//     seed_diff_each_rank
//   );

//   sanityCheckTasks(pd, true, false);
//   sanityCheckEdges(pd, include_comm, num_ranks);
// };

}}} // end namespace vt_lb::tests::unit
