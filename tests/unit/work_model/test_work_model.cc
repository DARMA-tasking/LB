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

#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/model/PhaseData.h>

namespace vt_lb { namespace tests { namespace unit {

template <comm::Communicator CommType>
struct TestWorkModelBasic : TestParallelHarness<CommType> {};

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

}}} // end namespace vt_lb::tests::unit
