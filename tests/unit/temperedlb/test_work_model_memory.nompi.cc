/*
//@HEADER
// *****************************************************************************
//
//                      test_work_model_memory.nompi.cc
//                 DARMA/vt-lb => Virtual Transport/Load Balancers
//
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia,
// LLC (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
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

#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/algo/temperedlb/task_cluster_summary_info.h>
#include <vt-lb/algo/temperedlb/transfer_util.h>
#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/SharedBlock.h>
#include <vt-lb/model/Task.h>

namespace vt_lb::tests::unit {

TEST(TestWorkModelMemory, compute_memory_usage_matches_modeled_formula) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  Configuration config;
  PhaseData pd(0);
  pd.setRankFootprintBytes(7.0);
  pd.setRankMaxMemoryAvailable(1000.0);

  pd.addSharedBlock(SharedBlock{10, 100.0, 0});
  pd.addSharedBlock(SharedBlock{20, 30.0, 1});

  Task task1{1, 0, 0, true, TaskMemory{50.0, 20.0, 30.0}, 1.0};
  task1.addSharedBlock(10);
  task1.addSharedBlock(20);
  pd.addTask(task1);

  Task task2{2, 0, 0, true, TaskMemory{10.0, 40.0, 5.0}, 1.0};
  task2.addSharedBlock(20);
  pd.addTask(task2);

  auto const memory = WorkModelCalculator::computeMemoryUsage(config, pd);

  EXPECT_DOUBLE_EQ(memory.current_memory_usage, 277.0);
  EXPECT_DOUBLE_EQ(memory.current_max_task_working_bytes, 50.0);
  EXPECT_DOUBLE_EQ(memory.current_max_task_serialized_bytes, 30.0);
}

TEST(TestWorkModelMemory, check_memory_fit_update_uses_rank_budget_and_dedups_shared_blocks) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  Configuration config;

  RankClusterInfo rank_info;
  rank_info.rank_footprint_bytes = 15.0;
  rank_info.rank_breakdown.memory_breakdown.current_memory_usage = 260.0;
  rank_info.rank_breakdown.memory_breakdown.current_max_task_working_bytes = 40.0;
  rank_info.rank_breakdown.memory_breakdown.current_max_task_serialized_bytes = 25.0;

  TaskClusterSummaryInfo local_cluster_to_remove;
  local_cluster_to_remove.cluster_id = 1;
  local_cluster_to_remove.cluster_footprint = 60.0;
  local_cluster_to_remove.max_object_working_bytes_outside = 10.0;
  local_cluster_to_remove.max_object_serialized_bytes_outside = 10.0;
  local_cluster_to_remove.shared_block_bytes_ = {{1, 100.0}};

  TaskClusterSummaryInfo local_cluster_to_keep;
  local_cluster_to_keep.cluster_id = 2;
  local_cluster_to_keep.cluster_footprint = 20.0;
  local_cluster_to_keep.shared_block_bytes_ = {{2, 40.0}};

  rank_info.cluster_summaries.emplace(1, local_cluster_to_remove);
  rank_info.cluster_summaries.emplace(2, local_cluster_to_keep);

  TaskClusterSummaryInfo remote_cluster_to_add;
  remote_cluster_to_add.cluster_id = 7;
  remote_cluster_to_add.cluster_footprint = 15.0;
  remote_cluster_to_add.max_object_working_bytes = 20.0;
  remote_cluster_to_add.max_object_serialized_bytes = 12.0;
  remote_cluster_to_add.shared_block_bytes_ = {{2, 40.0}, {3, 30.0}};

  rank_info.rank_available_memory = 132.0;
  EXPECT_TRUE(
    WorkModelCalculator::checkMemoryFitUpdate(
      config, rank_info, remote_cluster_to_add, local_cluster_to_remove
    )
  );

  rank_info.rank_available_memory = 111.0;
  EXPECT_FALSE(
    WorkModelCalculator::checkMemoryFitUpdate(
      config, rank_info, remote_cluster_to_add, local_cluster_to_remove
    )
  );
}

// Clusters migrated from different ranks can reference the same shared block.
// Dropping one of them used to credit back the block's bytes, which let a rank
// accept clusters past its memory budget.
TEST(TestWorkModelMemory, dropping_one_cluster_keeps_a_block_another_cluster_holds) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  Configuration config;
  config.work_model_.has_memory_info = true;
  config.work_model_.has_shared_block_memory_info = true;
  config.work_model_.has_task_footprint_memory_info = false;
  config.work_model_.has_task_working_memory_info = false;
  config.work_model_.has_task_serialized_memory_info = false;

  TaskClusterSummaryInfo first_holder;
  first_holder.cluster_id = 1;
  first_holder.shared_block_bytes_ = {{1, 100.0}};

  TaskClusterSummaryInfo second_holder;
  second_holder.cluster_id = 2;
  second_holder.shared_block_bytes_ = {{1, 100.0}};

  RankClusterInfo rank_info;
  rank_info.cluster_summaries.emplace(1, first_holder);
  rank_info.cluster_summaries.emplace(2, second_holder);
  rank_info.rank_breakdown.memory_breakdown.current_memory_usage = 100.0;

  auto const after_one = WorkModelCalculator::computeMemoryUpdateSummary(
    config, rank_info, TaskClusterSummaryInfo{}, first_holder
  );
  EXPECT_DOUBLE_EQ(after_one.current_memory_usage, 100.0);

  RankClusterInfo sole_holder_info;
  sole_holder_info.cluster_summaries.emplace(1, first_holder);
  sole_holder_info.rank_breakdown.memory_breakdown.current_memory_usage = 100.0;

  auto const after_last = WorkModelCalculator::computeMemoryUpdateSummary(
    config, sole_holder_info, TaskClusterSummaryInfo{}, first_holder
  );
  EXPECT_DOUBLE_EQ(after_last.current_memory_usage, 0.0);

  // Swapping the block out for a new one still needs room for both
  TaskClusterSummaryInfo incoming;
  incoming.cluster_id = 7;
  incoming.shared_block_bytes_ = {{2, 100.0}};

  rank_info.rank_available_memory = 100.0;
  EXPECT_FALSE(
    WorkModelCalculator::checkMemoryFitUpdate(
      config, rank_info, incoming, first_holder
    )
  );

  sole_holder_info.rank_available_memory = 100.0;
  EXPECT_TRUE(
    WorkModelCalculator::checkMemoryFitUpdate(
      config, sole_holder_info, incoming, first_holder
    )
  );
}

} // end namespace vt_lb::tests::unit