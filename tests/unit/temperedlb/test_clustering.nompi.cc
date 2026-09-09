/*
//@HEADER
// *****************************************************************************
//
//                           test_clustering.nompi.cc
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

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/model/PhaseData.h>

namespace vt_lb::tests::unit {

namespace {

/// Group task ids by the cluster the clusterer assigned them to
std::unordered_map<int, std::vector<vt_lb::model::TaskType>> membersByCluster(
  vt_lb::algo::temperedlb::Clusterer const& clusterer
) {
  std::unordered_map<int, std::vector<vt_lb::model::TaskType>> out;
  for (auto const& [task_id, cluster_id] : clusterer.taskToCluster()) {
    out[cluster_id].push_back(task_id);
  }
  for (auto& [cluster_id, members] : out) {
    (void)cluster_id;
    std::sort(members.begin(), members.end());
  }
  return out;
}

vt_lb::model::Task makeTask(
  vt_lb::model::TaskType id, double load, int shared_block,
  bool migratable = true
) {
  vt_lb::model::Task task{
    id, 0, 0, migratable, vt_lb::model::TaskMemory{}, load
  };
  if (shared_block >= 0) {
    task.addSharedBlock(
      static_cast<vt_lb::model::SharedBlockType>(shared_block)
    );
  }
  return task;
}

} // namespace

// Matching tasks pairwise used to split one block into several clusters, which
// let a migration leave the block resident on both ranks at once.
TEST(TestSharedBlockClustering, every_task_of_a_block_lands_in_one_cluster) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  PhaseData pd(0);
  pd.addSharedBlock(SharedBlock{7, 1000.0, 0});
  pd.addSharedBlock(SharedBlock{9, 500.0, 0});

  for (TaskType id = 1; id <= 5; ++id) {
    pd.addTask(makeTask(id, 1.0, 7));
  }
  for (TaskType id = 6; id <= 8; ++id) {
    pd.addTask(makeTask(id, 2.0, 9));
  }

  SharedBlockClusterer clusterer(pd);
  clusterer.compute();

  auto const members = membersByCluster(clusterer);
  ASSERT_EQ(members.size(), 2u);

  auto const& big = clusterer.taskToCluster().at(1);
  EXPECT_EQ(
    members.at(big), (std::vector<TaskType>{1, 2, 3, 4, 5})
  );

  auto const& small = clusterer.taskToCluster().at(6);
  EXPECT_EQ(members.at(small), (std::vector<TaskType>{6, 7, 8}));

  EXPECT_NE(big, small);
}

TEST(TestSharedBlockClustering, tasks_without_a_block_are_their_own_cluster) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  PhaseData pd(0);
  pd.addSharedBlock(SharedBlock{7, 1000.0, 0});

  pd.addTask(makeTask(1, 1.0, 7));
  pd.addTask(makeTask(2, 1.0, 7));
  pd.addTask(makeTask(3, 1.0, -1));
  pd.addTask(makeTask(4, 1.0, -1));

  SharedBlockClusterer clusterer(pd);
  clusterer.compute();

  auto const members = membersByCluster(clusterer);
  EXPECT_EQ(members.size(), 3u);
  EXPECT_EQ(
    members.at(clusterer.taskToCluster().at(1)), (std::vector<TaskType>{1, 2})
  );
  EXPECT_NE(clusterer.taskToCluster().at(3), clusterer.taskToCluster().at(4));
}

// Cluster ids drive migration decisions, so they must not depend on hash order
TEST(TestSharedBlockClustering, cluster_ids_follow_shared_block_order) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  PhaseData pd(0);
  pd.addSharedBlock(SharedBlock{4, 100.0, 0});
  pd.addSharedBlock(SharedBlock{2, 100.0, 0});

  pd.addTask(makeTask(10, 1.0, 4));
  pd.addTask(makeTask(20, 1.0, 2));

  SharedBlockClusterer clusterer(pd);
  clusterer.compute();

  // Block 2 sorts first, so its cluster takes the lower id
  EXPECT_LT(
    clusterer.taskToCluster().at(20), clusterer.taskToCluster().at(10)
  );

  auto const& clusters = clusterer.clusters();
  ASSERT_EQ(clusters.size(), 2u);
  EXPECT_LT(clusters[0].id, clusters[1].id);
}

// Clusters are the unit of migration, so a pinned task must not be inside one.
// BasicTransfer already checked isMigratable; the cluster path did not.
TEST(TestSharedBlockClustering, pinned_tasks_are_left_out_of_clusters) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  PhaseData pd(0);
  pd.addSharedBlock(SharedBlock{7, 1000.0, 0});

  pd.addTask(makeTask(1, 1.0, 7));
  pd.addTask(makeTask(2, 1.0, 7, /*migratable=*/false));
  pd.addTask(makeTask(3, 1.0, -1, /*migratable=*/false));

  SharedBlockClusterer clusterer(pd);
  clusterer.compute();

  EXPECT_EQ(clusterer.taskToCluster().count(2), 0u);
  EXPECT_EQ(clusterer.taskToCluster().count(3), 0u);

  auto const members = membersByCluster(clusterer);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(
    members.at(clusterer.taskToCluster().at(1)), (std::vector<TaskType>{1})
  );
}

TEST(TestCommunicationClustering, pinned_tasks_are_left_out_of_clusters) {
  using namespace vt_lb::model;
  using namespace vt_lb::algo::temperedlb;

  PhaseData pd(0);
  pd.addTask(makeTask(1, 1.0, -1));
  pd.addTask(makeTask(2, 1.0, -1, /*migratable=*/false));

  CommunicationClusterer clusterer(pd);
  clusterer.compute();

  EXPECT_EQ(clusterer.taskToCluster().count(1), 1u);
  EXPECT_EQ(clusterer.taskToCluster().count(2), 0u);
}

} // end namespace vt_lb::tests::unit
