/*
//@HEADER
// *****************************************************************************
//
//                      test_cluster_bookkeeping.cc
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

#include <unordered_map>
#include <utility>
#include <vector>

#include "test_parallel_harness.h"

#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/algo/temperedlb/relaxed_cluster_transfer.h>
#include <vt-lb/algo/temperedlb/statistics.h>
#include <vt-lb/algo/temperedlb/strict_cluster_transfer.h>
#include <vt-lb/algo/temperedlb/transfer_util.h>
#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/model/Communication.h>
#include <vt-lb/model/PhaseData.h>

namespace vt_lb::tests::unit {

using namespace vt_lb::algo::temperedlb;
using namespace vt_lb::model;

struct TestClusterBookkeeping : TestParallelHarness<comm::CommMPI> {};

namespace {

// Cluster 1 and cluster 2 are both local and joined by an inter-cluster edge,
// and cluster 1 holds a shared block that is not homed here. Moving cluster 1
// therefore has to reclassify the edge (intra <-> inter) and adjust the shared
// volume, which is exactly what a post-mutation call would miss.
RankClusterInfo makeRankInfo() {
  TaskClusterSummaryInfo cluster_one;
  cluster_one.cluster_id = 1;
  cluster_one.cluster_load = 10.0;
  cluster_one.cluster_intra_send_bytes = 2.0;
  cluster_one.cluster_intra_recv_bytes = 3.0;
  cluster_one.inter_edges_ = {ClusterEdge{1, 2, 100.0}};
  cluster_one.shared_block_bytes_ = {{5, 40.0}};

  TaskClusterSummaryInfo cluster_two;
  cluster_two.cluster_id = 2;
  cluster_two.cluster_load = 20.0;
  cluster_two.shared_block_bytes_ = {{6, 10.0}};

  RankClusterInfo info;
  info.cluster_summaries.emplace(1, cluster_one);
  info.cluster_summaries.emplace(2, cluster_two);
  info.shared_blocks_homed = {6};
  info.rank_breakdown.compute = 100.0;
  info.rank_breakdown.intra_node_send_comm = 500.0;
  info.rank_breakdown.intra_node_recv_comm = 500.0;
  info.rank_breakdown.inter_node_send_comm = 300.0;
  info.rank_breakdown.inter_node_recv_comm = 300.0;
  info.rank_breakdown.shared_mem_comm = 200.0;
  return info;
}

// Clusters carrying load only. With beta/gamma/delta at their defaults the work
// model reduces to the compute term, so a transfer's value is the net load moved.
RankClusterInfo makeLoadOnlyRankInfo(
  double compute, std::vector<std::pair<int, double>> const& clusters
) {
  RankClusterInfo info;
  for (auto const& [gid, load] : clusters) {
    TaskClusterSummaryInfo summary;
    summary.cluster_id = gid;
    summary.cluster_load = load;
    info.cluster_summaries.emplace(gid, summary);
  }
  info.rank_breakdown.compute = compute;
  info.rank_available_memory = 1.0e9;
  return info;
}

void expectBreakdownEq(WorkBreakdown const& actual, WorkBreakdown const& expected) {
  EXPECT_DOUBLE_EQ(actual.compute, expected.compute);
  EXPECT_DOUBLE_EQ(actual.inter_node_send_comm, expected.inter_node_send_comm);
  EXPECT_DOUBLE_EQ(actual.inter_node_recv_comm, expected.inter_node_recv_comm);
  EXPECT_DOUBLE_EQ(actual.intra_node_send_comm, expected.intra_node_send_comm);
  EXPECT_DOUBLE_EQ(actual.intra_node_recv_comm, expected.intra_node_recv_comm);
  EXPECT_DOUBLE_EQ(actual.shared_mem_comm, expected.shared_mem_comm);
}

// Fails loudly if the fixture stops exercising reclassification, which would
// otherwise let these tests pass without discriminating the ordering
void assertFixtureIsDiscriminating(
  WorkBreakdown const& before, WorkBreakdown const& expected
) {
  ASSERT_NE(expected.inter_node_send_comm, before.inter_node_send_comm);
  ASSERT_NE(expected.intra_node_send_comm, before.intra_node_send_comm);
  ASSERT_NE(expected.shared_mem_comm, before.shared_mem_comm);
}

} // namespace

TEST_F(TestClusterBookkeeping, relaxed_outgoing_cluster_uses_preswap_summaries) {
  auto const this_rank = comm.getRank();
  auto const info = makeRankInfo();
  auto const cluster_one = info.cluster_summaries.at(1);

  Configuration config;
  auto const expected =
    WorkModelCalculator::computeWorkUpdateSummary(config, info, {}, cluster_one);
  assertFixtureIsDiscriminating(info.rank_breakdown, expected);

  PhaseData pd(this_rank);
  RelaxedClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  transfer.outgoingCluster(1, cluster_one);

  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, expected);
  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(1), 0u);
}

TEST_F(TestClusterBookkeeping, relaxed_incoming_cluster_uses_preswap_summaries) {
  auto const this_rank = comm.getRank();
  auto info = makeRankInfo();
  auto const cluster_one = info.cluster_summaries.at(1);
  info.cluster_summaries.erase(1);

  Configuration config;
  auto const expected =
    WorkModelCalculator::computeWorkUpdateSummary(config, info, cluster_one, {});
  assertFixtureIsDiscriminating(info.rank_breakdown, expected);

  PhaseData pd(this_rank);
  RelaxedClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  transfer.incomingCluster(1, cluster_one);

  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, expected);
  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(1), 1u);
}

TEST_F(TestClusterBookkeeping, strict_outgoing_cluster_uses_preswap_summaries) {
  auto const this_rank = comm.getRank();
  auto const info = makeRankInfo();
  auto const cluster_one = info.cluster_summaries.at(1);

  Configuration config;
  auto const expected =
    WorkModelCalculator::computeWorkUpdateSummary(config, info, {}, cluster_one);
  assertFixtureIsDiscriminating(info.rank_breakdown, expected);

  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  transfer.outgoingCluster(1, cluster_one);

  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, expected);
  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(1), 0u);
}

TEST_F(TestClusterBookkeeping, strict_incoming_cluster_uses_preswap_summaries) {
  auto const this_rank = comm.getRank();
  auto info = makeRankInfo();
  auto const cluster_one = info.cluster_summaries.at(1);
  info.cluster_summaries.erase(1);

  Configuration config;
  auto const expected =
    WorkModelCalculator::computeWorkUpdateSummary(config, info, cluster_one, {});
  assertFixtureIsDiscriminating(info.rank_breakdown, expected);

  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  transfer.incomingCluster(1, cluster_one);

  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, expected);
  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(1), 1u);
}

TEST_F(TestClusterBookkeeping, relaxed_send_back_of_null_cluster_adds_no_phantom) {
  auto const this_rank = comm.getRank();
  auto const info = makeRankInfo();

  Configuration config;
  PhaseData pd(this_rank);
  RelaxedClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  // A rejected receive-only swap sends back the -1 sentinel with no tasks
  transfer.sendBackClusterHandler(-1, {}, {});

  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(-1), 0u);
  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.size(), 2u);
  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, info.rank_breakdown);
}

TEST_F(TestClusterBookkeeping, relaxed_send_back_restores_a_real_cluster) {
  auto const this_rank = comm.getRank();
  auto info = makeRankInfo();
  auto const cluster_one = info.cluster_summaries.at(1);
  info.cluster_summaries.erase(1);

  Configuration config;
  auto const expected =
    WorkModelCalculator::computeWorkUpdateSummary(config, info, cluster_one, {});
  assertFixtureIsDiscriminating(info.rank_breakdown, expected);

  PhaseData pd(this_rank);
  RelaxedClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  transfer.sendBackClusterHandler(1, cluster_one, {});

  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(1), 1u);
  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, expected);
}

TEST_F(TestClusterBookkeeping, strict_send_back_of_null_cluster_adds_no_phantom) {
  auto const this_rank = comm.getRank();
  auto const info = makeRankInfo();

  Configuration config;
  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  StrictClusterTransfer<comm::CommMPI>::LockToken token{};
  transfer.sendBackClusterHandler(token, -1, {}, {});

  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(-1), 0u);
  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.size(), 2u);
  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, info.rank_breakdown);
}

TEST_F(TestClusterBookkeeping, strict_send_back_restores_a_real_cluster) {
  auto const this_rank = comm.getRank();
  auto info = makeRankInfo();
  auto const cluster_one = info.cluster_summaries.at(1);
  info.cluster_summaries.erase(1);

  Configuration config;
  auto const expected =
    WorkModelCalculator::computeWorkUpdateSummary(config, info, cluster_one, {});
  assertFixtureIsDiscriminating(info.rank_breakdown, expected);

  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, {{this_rank, info}}, Statistics{}
  );

  StrictClusterTransfer<comm::CommMPI>::LockToken token{};
  transfer.sendBackClusterHandler(token, 1, cluster_one, {});

  EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(1), 1u);
  expectBreakdownEq(transfer.thisRankInfo().rank_breakdown, expected);
}

TEST_F(TestClusterBookkeeping, strict_finds_a_give_and_take_swap) {
  auto const this_rank = comm.getRank();
  auto const dst_rank = this_rank + 1;

  // Work 100 here vs 60 there, so the ideal net transfer is 20. No single
  // cluster moves 20, but giving 30 and taking 10 back does exactly that.
  auto const local = makeLoadOnlyRankInfo(100.0, {{1, 30.0}, {2, 5.0}});
  auto const remote = makeLoadOnlyRankInfo(60.0, {{3, 10.0}, {4, 1.0}});

  Configuration config;
  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000,
    {{this_rank, local}, {dst_rank, remote}}, Statistics{}
  );

  auto const best = transfer.findBestSwapCandidateForTarget(dst_rank, remote);

  EXPECT_EQ(best.dst_rank, dst_rank);
  EXPECT_EQ(best.give_cluster_gid, 1);
  EXPECT_EQ(best.recv_cluster_gid, 3);
  EXPECT_DOUBLE_EQ(best.this_work_after, 80.0);
  EXPECT_DOUBLE_EQ(best.dst_work_after, 80.0);
  // Best one-sided move is giving cluster 1 outright, worth only 10
  EXPECT_DOUBLE_EQ(best.improvement, 20.0);
}

TEST_F(TestClusterBookkeeping, strict_still_takes_a_one_sided_give_when_it_is_best) {
  auto const this_rank = comm.getRank();
  auto const dst_rank = this_rank + 1;

  // Here a single cluster moves the ideal 20 units, so no exchange improves on it
  auto const local = makeLoadOnlyRankInfo(100.0, {{1, 20.0}});
  auto const remote = makeLoadOnlyRankInfo(60.0, {{3, 50.0}});

  Configuration config;
  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000,
    {{this_rank, local}, {dst_rank, remote}}, Statistics{}
  );

  auto const best = transfer.findBestSwapCandidateForTarget(dst_rank, remote);

  EXPECT_EQ(best.give_cluster_gid, 1);
  EXPECT_EQ(best.recv_cluster_gid, -1);
  EXPECT_DOUBLE_EQ(best.improvement, 20.0);
}

TEST_F(TestClusterBookkeeping, strict_still_takes_a_one_sided_receive_when_it_is_best) {
  auto const this_rank = comm.getRank();
  auto const dst_rank = this_rank + 1;

  // Underloaded here, so the only improving move is to accept work
  auto const local = makeLoadOnlyRankInfo(60.0, {});
  auto const remote = makeLoadOnlyRankInfo(100.0, {{3, 20.0}});

  Configuration config;
  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000,
    {{this_rank, local}, {dst_rank, remote}}, Statistics{}
  );

  auto const best = transfer.findBestSwapCandidateForTarget(dst_rank, remote);

  EXPECT_EQ(best.give_cluster_gid, -1);
  EXPECT_EQ(best.recv_cluster_gid, 3);
  EXPECT_DOUBLE_EQ(best.improvement, 20.0);
}

TEST_F(TestClusterBookkeeping, strict_screen_stops_at_the_first_improving_move) {
  auto const this_rank = comm.getRank();
  auto const dst_rank = this_rank + 1;

  // One cluster each way, so enumeration order is deterministic: the give-only
  // move is tried first and already improves, so the screen returns it without
  // discovering that the exchange is worth twice as much.
  auto const local = makeLoadOnlyRankInfo(100.0, {{1, 30.0}});
  auto const remote = makeLoadOnlyRankInfo(60.0, {{3, 10.0}});

  Configuration config;
  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000,
    {{this_rank, local}, {dst_rank, remote}}, Statistics{}
  );

  auto const screened = transfer.screenSwapCandidateForTarget(dst_rank, remote);
  EXPECT_EQ(screened.give_cluster_gid, 1);
  EXPECT_EQ(screened.recv_cluster_gid, -1);
  EXPECT_DOUBLE_EQ(screened.improvement, 10.0);

  // The exhaustive pass, which runs once the destination is locked, does find it
  auto const best = transfer.findBestSwapCandidateForTarget(dst_rank, remote);
  EXPECT_EQ(best.give_cluster_gid, 1);
  EXPECT_EQ(best.recv_cluster_gid, 3);
  EXPECT_DOUBLE_EQ(best.improvement, 20.0);
}

TEST_F(TestClusterBookkeeping, strict_screen_finds_a_swap_when_giving_alone_does_not_help) {
  auto const this_rank = comm.getRank();
  auto const dst_rank = this_rank + 1;

  // Giving the 60-load cluster overshoots and makes things worse, so the screen
  // has to keep walking and reach the exchange before it finds anything
  auto const local = makeLoadOnlyRankInfo(100.0, {{1, 60.0}});
  auto const remote = makeLoadOnlyRankInfo(60.0, {{3, 40.0}});

  Configuration config;
  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000,
    {{this_rank, local}, {dst_rank, remote}}, Statistics{}
  );

  auto const screened = transfer.screenSwapCandidateForTarget(dst_rank, remote);
  EXPECT_EQ(screened.give_cluster_gid, 1);
  EXPECT_EQ(screened.recv_cluster_gid, 3);
  EXPECT_DOUBLE_EQ(screened.improvement, 20.0);
}

TEST_F(TestClusterBookkeeping, strict_reports_no_target_when_nothing_improves) {
  auto const this_rank = comm.getRank();
  auto const dst_rank = this_rank + 1;

  // Already balanced: every move is neutral or worse
  auto const local = makeLoadOnlyRankInfo(60.0, {{1, 10.0}});
  auto const remote = makeLoadOnlyRankInfo(60.0, {{3, 10.0}});

  Configuration config;
  PhaseData pd(this_rank);
  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000,
    {{this_rank, local}, {dst_rank, remote}}, Statistics{}
  );

  EXPECT_LE(transfer.screenSwapCandidateForTarget(dst_rank, remote).improvement, 0.0);
  // run() stops on a non-positive improvement
  EXPECT_LE(transfer.findSwapTarget().improvement, 0.0);
}

TEST_F(TestClusterBookkeeping, strict_run_completes_when_the_locked_rank_declines) {
  if (comm.numRanks() != 2) {
    GTEST_SKIP() << "fixture is written for exactly two ranks";
  }
  auto const this_rank = comm.getRank();

  Configuration config;
  PhaseData pd(this_rank);

  // Rank 0 acts on a stale, inviting view of rank 1 and asks for the lock.
  // Rank 1 is really the loaded one, so once its own numbers arrive the move
  // is plainly bad and the lock is released without anything moving.
  std::unordered_map<int, RankClusterInfo> info;
  info[0] = makeLoadOnlyRankInfo(100.0, {{1, 30.0}});
  info[1] = makeLoadOnlyRankInfo(this_rank == 0 ? 60.0 : 200.0, {});

  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, info, Statistics{}
  );

  transfer.run();

  if (this_rank == 0) {
    // The grant carried rank 1's real numbers, which must replace the stale ones
    EXPECT_DOUBLE_EQ(transfer.rankInfo(1).rank_breakdown.compute, 200.0);
    // ...and nothing was transferred
    EXPECT_EQ(transfer.thisRankInfo().cluster_summaries.count(1), 1u);
    EXPECT_DOUBLE_EQ(transfer.thisRankInfo().rank_breakdown.compute, 100.0);
  }
}

// Granting a lock and acting on one of our own at the same time lets two
// transactions mutate this rank concurrently, each validated against a state
// that ignored the other. The grant has to wait for the release.
TEST_F(TestClusterBookkeeping, strict_defers_a_grant_while_serving_another_rank) {
  if (comm.numRanks() != 2) {
    GTEST_SKIP() << "fixture is written for exactly two ranks";
  }
  auto const this_rank = comm.getRank();

  Configuration config;
  PhaseData pd(this_rank);

  // Rank 1 is the loaded one, so once rank 0 acts on the grant it declines and
  // clears the request. That makes "still outstanding" mean "still deferred".
  std::unordered_map<int, RankClusterInfo> info;
  info[0] = makeLoadOnlyRankInfo(20.0, {{1, 5.0}});
  info[1] = makeLoadOnlyRankInfo(200.0, {});

  StrictClusterTransfer<comm::CommMPI> transfer(
    comm, pd, config, nullptr, 1000, info, Statistics{}
  );

  using Strict = StrictClusterTransfer<comm::CommMPI>;

  if (this_rank == 0) {
    Strict::LockToken const peer_token{1, 99};

    transfer.requestRemoteLock(1, 40.0);
    ASSERT_TRUE(transfer.hasOutstandingLockRequest());

    // Rank 1 takes our lock before our own grant comes back
    transfer.requestLock(peer_token, 1.0);

    transfer.lockGranted(Strict::LockToken{0, 1}, 1, transfer.rankInfo(1));
    EXPECT_TRUE(transfer.hasOutstandingLockRequest())
      << "acted on our own grant while locked by another rank";

    // Releasing runs the deferred grant, which declines and clears the request
    transfer.releaseLock(peer_token);
    EXPECT_FALSE(transfer.hasOutstandingLockRequest())
      << "deferred grant was dropped rather than resumed";
  }

  while (comm.poll()) { }
}

TEST_F(TestClusterBookkeeping, strict_accepts_when_the_pair_maximum_falls) {
  using Strict = StrictClusterTransfer<comm::CommMPI>;

  // The receiver gets busier (20 -> 150) yet the pair's maximum drops from
  // 190 to 150, which is exactly the transfer that balances a hot rank
  EXPECT_TRUE(Strict::pairImproves(190.0, 70.0, 20.0, 150.0));
}

TEST_F(TestClusterBookkeeping, strict_accepts_a_sideways_move_that_lowers_the_total) {
  using Strict = StrictClusterTransfer<comm::CommMPI>;

  // Neither rank holds the maximum alone, so shedding an off-home block leaves
  // the pair maximum at 100 while the total falls from 180 to 170. Refusing
  // this is what used to strand a block away from home.
  EXPECT_TRUE(Strict::pairImproves(100.0, 100.0, 80.0, 70.0));
}

TEST_F(TestClusterBookkeeping, strict_rejects_a_sideways_move_that_changes_nothing) {
  using Strict = StrictClusterTransfer<comm::CommMPI>;

  // Same maximum and same total: run() would propose this forever
  EXPECT_FALSE(Strict::pairImproves(100.0, 80.0, 80.0, 100.0));
}

TEST_F(TestClusterBookkeeping, strict_rejects_when_the_receiver_becomes_the_bottleneck) {
  using Strict = StrictClusterTransfer<comm::CommMPI>;

  // Source improves a lot, but the destination overshoots past the old maximum
  EXPECT_FALSE(Strict::pairImproves(200.0, 50.0, 50.0, 210.0));
}

TEST_F(TestClusterBookkeeping, strict_rejects_when_the_pair_maximum_rises) {
  using Strict = StrictClusterTransfer<comm::CommMPI>;

  EXPECT_FALSE(Strict::pairImproves(100.0, 60.0, 90.0, 130.0));
}

} // end namespace vt_lb::tests::unit
