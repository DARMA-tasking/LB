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

}}} // end namespace vt_lb::tests::unit
