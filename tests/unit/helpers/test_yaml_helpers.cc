/*
//@HEADER
// *****************************************************************************
//
//                              test_yaml_helpers.cc
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
#include <vt-lb/input/yaml_reader.h>

#include "../test_harness.h"
#include "../test_helpers.h"

#include <string>

namespace vt_lb { namespace tests { namespace unit {

struct TestYamlHelpers: vt_lb::tests::unit::TestHarness {
};

TEST_F(TestYamlHelpers, test_read_yaml_config_complete) {
  using namespace vt_lb::input;
  // Arrange
  YAMLReader reader;
  reader.loadYamlString("# Specify the load balancer input files\n"
                        "from_data:\n"
                        "  data_folder: ../some_data_folder/\n"
                        "  phase_ids:\n"
                        "    - 0\n"
                        "    - 1\n"
                        "\n"
                        "# Load balancer configuration parameters\n"
                        "configuration:\n"
                        "  num_trials: 10\n"
                        "  num_iters: 20\n"
                        "  fanout: 8\n"
                        "  n_rounds: 5\n"
                        "  deterministic: false\n"
                        "  seed: 42\n"
                        "\n"
                        "  transfer_decisions:\n"
                        "    criterion: Grapevine\n"
                        "    obj_ordering: Arbitrary\n"
                        "    cmf_type: Original\n"
                        "\n"
                        "  work_model:\n"
                        "    parameters:\n"
                        "      rank_alpha: 2.3\n"
                        "      beta: 0.7\n"
                        "      gamma: 0.1\n"
                        "      delta: 6.2\n"
                        "    memory_info:\n"
                        "      has_mem_info: true\n"
                        "      has_task_serialized_mem_info: false\n"
                        "      has_task_working_mem_info: true\n"
                        "      has_task_footprint_mem_info: true\n"
                        "      has_shared_block_mem_info: true\n"
                        "\n"
                        "  clustering:\n"
                        "    based_on_shared_blocks: false\n"
                        "    based_on_communication: true\n"
                        "    visualize_task_graph: false\n"
                        "    visualize_clusters: false\n"
                        "    visualize_full_graph: false\n"
                        "\n"
                        "  converge_tolerance: 0.01\n");

  // Act
  auto lb_config = reader.parseLBConfig(4);

  // Assert
  EXPECT_EQ(lb_config.num_trials_, 10);
  EXPECT_EQ(lb_config.num_iters_, 20);
  EXPECT_EQ(lb_config.f_, 8);
  EXPECT_EQ(lb_config.k_max_, 5);
  EXPECT_EQ(lb_config.deterministic_, false);
  EXPECT_EQ(lb_config.seed_, 42);
  EXPECT_EQ(lb_config.criterion_, vt_lb::algo::temperedlb::CriterionEnum::Grapevine);
  EXPECT_EQ(lb_config.obj_ordering_, vt_lb::algo::temperedlb::TransferUtil::ObjectOrder::Arbitrary);
  EXPECT_EQ(lb_config.cmf_type_, vt_lb::algo::temperedlb::TransferUtil::CMFType::Original);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.rank_alpha, 2.3);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.beta, 0.7);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.gamma, 0.1);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.delta, 6.2);
  EXPECT_EQ(lb_config.work_model_.has_memory_info, true);
  EXPECT_EQ(lb_config.work_model_.has_task_serialized_memory_info, false);
  EXPECT_EQ(lb_config.work_model_.has_task_working_memory_info, true);
  EXPECT_EQ(lb_config.work_model_.has_task_footprint_memory_info, true);
  EXPECT_EQ(lb_config.work_model_.has_shared_block_memory_info, true);
  EXPECT_EQ(lb_config.cluster_based_on_shared_blocks_, false);
  EXPECT_EQ(lb_config.cluster_based_on_communication_, true);
  EXPECT_EQ(lb_config.visualize_task_graph_, false);
  EXPECT_EQ(lb_config.visualize_clusters_, false);
  EXPECT_EQ(lb_config.visualize_full_graph_, false);
  EXPECT_DOUBLE_EQ(lb_config.converge_tolerance_, 0.01);
}

TEST_F(TestYamlHelpers, test_read_yaml_config_incomplete) {
  using namespace vt_lb::input;
  // Arrange
  YAMLReader reader;
  reader.loadYamlString("# Load balancer configuration parameters\n"
                        "\n"
                        "configuration:\n"
                        "  work_model:\n"
                        "    parameters:\n"
                        "      rank_alpha: 2.3\n"
                        "      beta: 0.7\n"
                        "      gamma: 0.1\n"
                        "      delta: 6.2\n");

  // Act
  auto lb_config = reader.parseLBConfig(4);
  // Assert
  EXPECT_EQ(lb_config.num_trials_, 1);
  EXPECT_EQ(lb_config.num_iters_, 10);
  EXPECT_EQ(lb_config.f_, 2);
  EXPECT_EQ(lb_config.k_max_, 2);
  EXPECT_EQ(lb_config.deterministic_, true);
  EXPECT_EQ(lb_config.seed_, 29);
  EXPECT_EQ(lb_config.criterion_, vt_lb::algo::temperedlb::CriterionEnum::ModifiedGrapevine);
  EXPECT_EQ(lb_config.obj_ordering_, vt_lb::algo::temperedlb::TransferUtil::ObjectOrder::ElmID);
  EXPECT_EQ(lb_config.cmf_type_, vt_lb::algo::temperedlb::TransferUtil::CMFType::Original);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.rank_alpha, 2.3);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.beta, 0.7);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.gamma, 0.1);
  EXPECT_DOUBLE_EQ(lb_config.work_model_.delta, 6.2);
  EXPECT_EQ(lb_config.work_model_.has_memory_info, true);
  EXPECT_EQ(lb_config.work_model_.has_task_serialized_memory_info, true);
  EXPECT_EQ(lb_config.work_model_.has_task_working_memory_info, true);
  EXPECT_EQ(lb_config.work_model_.has_task_footprint_memory_info, true);
  EXPECT_EQ(lb_config.work_model_.has_shared_block_memory_info, true);
  EXPECT_EQ(lb_config.cluster_based_on_shared_blocks_, false);
  EXPECT_EQ(lb_config.cluster_based_on_communication_, false);
  EXPECT_EQ(lb_config.visualize_task_graph_, false);
  EXPECT_EQ(lb_config.visualize_clusters_, false);
  EXPECT_EQ(lb_config.visualize_full_graph_, false);
  EXPECT_DOUBLE_EQ(lb_config.converge_tolerance_, 0.01);
}

}}} // end namespace vt_lb::tests::unit
