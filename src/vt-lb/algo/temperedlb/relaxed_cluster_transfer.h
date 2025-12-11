/*
//@HEADER
// *****************************************************************************
//
//                         relaxed_cluster_transfer.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_RELAXED_CLUSTER_TRANSFER_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_RELAXED_CLUSTER_TRANSFER_H

#include <vt-lb/algo/temperedlb/transfer.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/statistics.h>
#include <vt-lb/comm/comm_traits.h>
#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>

#include <unordered_map>

namespace vt_lb::algo::temperedlb {

template <comm::Communicator CommT>
struct RelaxedClusterTransfer final : Transferer<CommT> {
  RelaxedClusterTransfer(
    CommT& comm,
    model::PhaseData& pd,
    std::unordered_map<int, RankClusterInfo> const& cluster_info,
    Statistics stats
  ) : Transferer<CommT>(comm, pd),
      cluster_info_(cluster_info),
      stats_(stats)
  {}

  void run(Configuration const& config) {
    int this_rank = this->comm_.getRank();
    std::vector<int> dest_ranks;
    dest_ranks.reserve(cluster_info_.size());
    for (auto const& [rank, _] : cluster_info_) {
      if (rank != this_rank) {
        dest_ranks.push_back(rank);
      }
    }

    RankClusterInfo const& this_rank_info = cluster_info_.at(this_rank);
    auto const& local_cluster_summaries = this_rank_info.cluster_summaries;

    // Precompute "before" work per known rank
    std::unordered_map<int, double> before_work;
    for (auto const& kv : cluster_info_) {
      before_work[kv.first] = WorkModelCalculator::computeWork(
        config.work_model_, kv.second.rank_breakdown
      );
    }

    struct Candidate {
      int dst_rank;
      int give_cluster_gid = -1;
      int recv_cluster_gid = -1;
      double this_work_after = 0.0;
      double dst_work_after = 0.0;
      double improvement = 0.0; // w_max_0 - w_max_new
    };

    std::vector<Candidate> candidates;

    auto eval_swap = [&](int dst_rank, int give_gid, int recv_gid) -> Candidate {
      Candidate c{dst_rank, give_gid, recv_gid, 0.0, 0.0, 0.0};

      TaskClusterSummaryInfo to_add_this{};
      TaskClusterSummaryInfo to_remove_this{};

      if (give_gid != -1) {
        to_remove_this = local_cluster_summaries.at(give_gid);
      }
      if (recv_gid != -1) {
        to_add_this = cluster_info_.at(dst_rank).cluster_summaries.at(recv_gid);
      }

      c.this_work_after = WorkModelCalculator::computeWorkUpdateSummary(
        config.work_model_, this_rank_info, to_add_this, to_remove_this
      );

      RankClusterInfo const& dst_info = cluster_info_.at(dst_rank);
      TaskClusterSummaryInfo to_add_dst{};
      TaskClusterSummaryInfo to_remove_dst{};

      if (give_gid != -1) {
        to_add_dst = local_cluster_summaries.at(give_gid);
      }
      if (recv_gid != -1) {
        to_remove_dst = dst_info.cluster_summaries.at(recv_gid);
      }

      c.dst_work_after = WorkModelCalculator::computeWorkUpdateSummary(
        config.work_model_, dst_info, to_add_dst, to_remove_dst
      );

      // Criterion: improvement in max work
      double before_w_src = before_work[this_rank];
      double before_w_dst = before_work[dst_rank];
      double w_max_0 = std::max(before_w_src, before_w_dst);
      double w_max_new = std::max(c.this_work_after, c.dst_work_after);
      c.improvement = w_max_0 - w_max_new;

      return c;
    };

    // Null swap candidates
    for (auto const& [give_gid, _] : local_cluster_summaries) {
      for (int dst : dest_ranks) {
        candidates.emplace_back(eval_swap(dst, give_gid, -1));
      }
    }

    // Receive-only candidates
    for (int dst : dest_ranks) {
      auto const& dst_summaries = cluster_info_.at(dst).cluster_summaries;
      for (auto const& [recv_gid, _] : dst_summaries) {
        candidates.emplace_back(eval_swap(dst, -1, recv_gid));
      }
    }

    if (candidates.empty()) {
      VT_LB_LOG(LoadBalancer, normal, "RelaxedClusterTransfer: no swap candidates\n");
      return;
    }

    // Sort by descending improvement; tie-breakers by this rank’s post-work then destination’s
    std::sort(
      candidates.begin(), candidates.end(),
      [](Candidate const& a, Candidate const& b) {
        if (a.improvement != b.improvement) return a.improvement > b.improvement;
        if (a.this_work_after != b.this_work_after) return a.this_work_after < b.this_work_after;
        return a.dst_work_after < b.dst_work_after;
      }
    );

    auto const& best = candidates.front();
    VT_LB_LOG(
      LoadBalancer, normal,
      "RelaxedClusterTransfer: best candidate dst_rank={} give_gid={} recv_gid={} "
      "this_work_after={:.2f} dst_work_after={:.2f} improvement={:.2f}\n",
      best.dst_rank, best.give_cluster_gid, best.recv_cluster_gid,
      best.this_work_after, best.dst_work_after, best.improvement
    );
  }

  /*virutal*/ bool acceptIncomingTask(model::Task const& task) override final {
    (void)task;
    return true;
  }

private:
  std::unordered_map<int, RankClusterInfo> cluster_info_;
  Statistics stats_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_RELAXED_CLUSTER_TRANSFER_H*/