/*
//@HEADER
// *****************************************************************************
//
//                          strict_cluster_transfer.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_STRICT_CLUSTER_TRANSFER_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_STRICT_CLUSTER_TRANSFER_H

#include <vt-lb/algo/temperedlb/transfer.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/statistics.h>
#include <vt-lb/comm/comm_traits.h>
#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>
#include <vt-lb/util/assert.h>
#include <vt-lb/util/logging.h>

#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>
#include <limits>

namespace vt_lb::algo::temperedlb {

template <comm::Communicator CommT>
struct StrictClusterTransfer {
  using ThisType = StrictClusterTransfer<CommT>;
  using HandleType = typename CommT::template HandleType<ThisType>;

  StrictClusterTransfer(
    CommT& comm,
    model::PhaseData& pd,
    Configuration const& config,
    Clusterer* clusterer,
    int global_max_clusters,
    std::unordered_map<int, RankClusterInfo> const& cluster_info,
    Statistics stats
  ) : comm_(comm.clone()),
      handle_(comm_.template registerInstanceCollective<ThisType>(this)),
      pd_(pd),
      clusterer_(clusterer),
      global_max_clusters_(global_max_clusters),
      cluster_info_(cluster_info),
      stats_(stats),
      config_(config)
  {
    if (clusterer_) {
      clusterer_->remapClusterIDs(
        ClusterSummarizerUtil::buildLocalToGlobalClusterIDMap(
          comm_.getRank(), global_max_clusters_, clusterer_->clusters()
        )
      );
    }
  }

  struct Candidate {
    int dst_rank = -1;
    int give_cluster_gid = -1;
    int recv_cluster_gid = -1;
    double this_work_before = 0.0;
    double this_work_after = 0.0;
    WorkBreakdown this_work_breakdown_after = {};
    double dst_work_before = 0.0;
    double dst_work_after = 0.0;
    WorkBreakdown dst_work_breakdown_after = {};
    double improvement = 0.0;
  };

  struct LockToken {
    int requester_rank = -1;
    std::uint64_t sequence = 0;

    template <typename SerializerT>
    void serialize(SerializerT& s) {
      s | requester_rank;
      s | sequence;
    }

    bool operator==(LockToken const& other) const {
      return requester_rank == other.requester_rank and sequence == other.sequence;
    }

    bool valid() const {
      return requester_rank >= 0;
    }
  };

  struct PendingLockRequest {
    LockToken token = {};
    double priority = 0.0;

    bool operator<(PendingLockRequest const& other) const {
      if (priority != other.priority) {
        return priority > other.priority;
      }
      if (token.requester_rank != other.token.requester_rank) {
        return token.requester_rank < other.token.requester_rank;
      }
      return token.sequence < other.token.sequence;
    }
  };

  struct ActiveLockRequest {
    LockToken token = {};
    int target_rank = -1;
    bool waiting_for_grant = false;
    bool waiting_for_transaction = false;
  };

  Candidate evaluateSwapCandidate(
    int this_rank,
    RankClusterInfo const& this_rank_info,
    int dst_rank,
    RankClusterInfo const& dst_info,
    std::unordered_map<int, double> const& before_work,
    int give_gid,
    int recv_gid
  ) const {
    Candidate c{};
    c.dst_rank = dst_rank;
    c.give_cluster_gid = give_gid;
    c.recv_cluster_gid = recv_gid;

    auto const& local_cluster_summaries = this_rank_info.cluster_summaries;

    TaskClusterSummaryInfo to_add_this{};
    TaskClusterSummaryInfo to_remove_this{};
    TaskClusterSummaryInfo to_add_dst{};
    TaskClusterSummaryInfo to_remove_dst{};

    if (give_gid != -1) {
      to_remove_this = to_add_dst = local_cluster_summaries.at(give_gid);
    }
    if (recv_gid != -1) {
      to_add_this = to_remove_dst = dst_info.cluster_summaries.at(recv_gid);
    }

    if (
      config_.hasMemoryInfo() and
      not WorkModelCalculator::checkMemoryFitUpdate(
        config_, this_rank_info, to_add_this, to_remove_this
      )
    ) {
      c.improvement = -std::numeric_limits<double>::infinity();
      return c;
    }

    if (
      config_.hasMemoryInfo() and
      not WorkModelCalculator::checkMemoryFitUpdate(
        config_, dst_info, to_add_dst, to_remove_dst
      )
    ) {
      c.improvement = -std::numeric_limits<double>::infinity();
      return c;
    }

    c.this_work_breakdown_after = WorkModelCalculator::computeWorkUpdateSummary(
      this_rank_info, to_add_this, to_remove_this
    );
    c.this_work_after = WorkModelCalculator::computeWork(
      config_.work_model_, c.this_work_breakdown_after
    );

    c.dst_work_breakdown_after = WorkModelCalculator::computeWorkUpdateSummary(
      dst_info, to_add_dst, to_remove_dst
    );
    c.dst_work_after = WorkModelCalculator::computeWork(
      config_.work_model_, c.dst_work_breakdown_after
    );

    c.this_work_before = before_work.at(this_rank);
    c.dst_work_before = before_work.at(dst_rank);
    double w_max_0 = std::max(c.this_work_before, c.dst_work_before);
    double w_max_new = std::max(c.this_work_after, c.dst_work_after);
    c.improvement = w_max_0 - w_max_new;

    return c;
  }

  Candidate findBestSwapCandidateForTarget(
    int dst_rank,
    RankClusterInfo const& dst_info
  ) const {
    Candidate best{};
    best.dst_rank = dst_rank;
    best.improvement = -std::numeric_limits<double>::infinity();

    int const this_rank = comm_.getRank();
    if (dst_rank <= this_rank) {
      return best;
    }

    RankClusterInfo const& this_rank_info = cluster_info_.at(this_rank);
    auto const& local_cluster_summaries = this_rank_info.cluster_summaries;

    std::unordered_map<int, double> before_work;
    before_work[this_rank] = WorkModelCalculator::computeWork(
      config_.work_model_, this_rank_info.rank_breakdown
    );
    before_work[dst_rank] = WorkModelCalculator::computeWork(
      config_.work_model_, dst_info.rank_breakdown
    );

    for (auto const& [give_gid, _] : local_cluster_summaries) {
      auto candidate = evaluateSwapCandidate(
        this_rank, this_rank_info, dst_rank, dst_info, before_work, give_gid, -1
      );
      if (candidate.improvement > best.improvement) {
        best = std::move(candidate);
      }
    }

    for (auto const& [recv_gid, _] : dst_info.cluster_summaries) {
      auto candidate = evaluateSwapCandidate(
        this_rank, this_rank_info, dst_rank, dst_info, before_work, -1, recv_gid
      );
      if (candidate.improvement > best.improvement) {
        best = std::move(candidate);
      }
    }

    return best;
  }

  Candidate findBestSwapCandidate() {
    int this_rank = this->comm_.getRank();

    std::vector<int> dest_ranks;
    dest_ranks.reserve(cluster_info_.size());
    for (auto const& [rank, _] : cluster_info_) {
      if (rank > this_rank) {
        dest_ranks.push_back(rank);
      }
    }
    Candidate best{};
    best.improvement = -std::numeric_limits<double>::infinity();
    for (int dst_rank : dest_ranks) {
      auto candidate = findBestSwapCandidateForTarget(
        dst_rank, cluster_info_.at(dst_rank)
      );
      if (candidate.improvement > best.improvement) {
        best = std::move(candidate);
      }
    }

    if (best.improvement == -std::numeric_limits<double>::infinity()) {
      VT_LB_LOG(LoadBalancer, normal, "StrictClusterTransfer: no swap candidates\n");
      return Candidate{};
    }

    VT_LB_LOG(
      LoadBalancer, normal,
      "StrictClusterTransfer: best candidate dst_rank={} give_gid={} recv_gid={} "
      "this_work_before={:.2f} this_work_after={:.2f} dst_work_before={:.2f} "
      "dst_work_after={:.2f} improvement={:.2f}\n",
      best.dst_rank, best.give_cluster_gid, best.recv_cluster_gid,
      best.this_work_before, best.this_work_after, best.dst_work_before,
      best.dst_work_after, best.improvement
    );

    return best;
  }

  void run() {
    while (comm_.poll()) {
      tryGrantNextLock();
    }

    while (true) {
      auto best = findBestSwapCandidate();
      if (best.improvement <= 0.0) {
        break;
      }

      VT_LB_LOG(
        LoadBalancer, normal,
        "StrictClusterTransfer: requesting lock for dst_rank={} "
        "anticipated improvement={:.2f}\n",
        best.dst_rank, best.improvement
      );

      requestRemoteLock(best.dst_rank, best.improvement);
      while (active_lock_request_.has_value() and comm_.poll()) {
        tryGrantNextLock();
      }
    }

    while (this->comm_.poll()) {
      tryGrantNextLock();
    }
  }

  void requestRemoteLock(int dst_rank, double priority) {
    vt_lb_assert(
      not active_lock_request_.has_value(),
      "Only one active strict lock request is allowed per rank"
    );

    LockToken token{comm_.getRank(), next_lock_sequence_++};
    active_lock_request_ = ActiveLockRequest{token, dst_rank, true, false};
    handle_[dst_rank].template send<&ThisType::requestLock>(token, priority);
  }

  void requestLock(LockToken token, double priority) {
    pending_lock_requests_.insert(PendingLockRequest{token, priority});
    tryGrantNextLock();
  }

  void lockGranted(
    LockToken token,
    int locked_rank,
    RankClusterInfo locked_rank_info
  ) {
    if (
      not active_lock_request_.has_value() or
      token != active_lock_request_->token or
      locked_rank != active_lock_request_->target_rank
    ) {
      handle_[locked_rank].template send<&ThisType::releaseLock>(token);
      return;
    }

    auto best = findBestSwapCandidateForTarget(locked_rank, locked_rank_info);
    if (best.improvement <= 0.0) {
      handle_[locked_rank].template send<&ThisType::releaseLock>(token);
      active_lock_request_.reset();
      pending_candidate_.reset();
      return;
    }

    TaskClusterSummaryInfo give_cluster_summary{};
    if (best.give_cluster_gid != -1) {
      give_cluster_summary =
        cluster_info_.at(comm_.getRank()).cluster_summaries.at(best.give_cluster_gid);
    }

    pending_candidate_ = best;
    active_lock_request_->waiting_for_grant = false;
    active_lock_request_->waiting_for_transaction = true;
    transaction_status_ = TransactionStatus::Pending;

    migrateCluster(
      locked_rank,
      token,
      best.give_cluster_gid,
      give_cluster_summary,
      best.recv_cluster_gid,
      false,
      best.dst_work_before
    );
  }

  void releaseLock(LockToken token) {
    if (not is_locked_ or token != current_lock_token_) {
      return;
    }

    is_locked_ = false;
    current_lock_token_ = {};
    tryGrantNextLock();
  }

  void tryGrantNextLock() {
    if (is_locked_ or hasTentativeLocalTransaction() or pending_lock_requests_.empty()) {
      return;
    }

    auto iter = pending_lock_requests_.begin();
    auto request = *iter;
    pending_lock_requests_.erase(iter);

    auto const this_rank = comm_.getRank();
    current_lock_token_ = request.token;
    is_locked_ = true;

    handle_[request.token.requester_rank].template send<&ThisType::lockGranted>(
      request.token,
      this_rank,
      cluster_info_.at(this_rank)
    );
  }

  /// This rank's incrementally-maintained cluster info
  RankClusterInfo const& thisRankInfo() const {
    return cluster_info_.at(comm_.getRank());
  }

  void migrateCluster(
    int const rank,
    LockToken token,
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    int request_cluster_gid,
    bool sending_requested_cluster = false,
    double dst_work_before = 0.0
  ) {
    vt_lb_assert(
      clusterer_ != nullptr,
      "Clusterer must be initialized to migrate clusters"
    );

    std::vector<model::Task> tasks_to_migrate;
    std::vector<model::Edge> edges_to_migrate;
    std::set<model::SharedBlockType> shared_blocks_id_set;
    std::vector<model::SharedBlock> shared_blocks_to_migrate;

    if (cluster_gid != -1) {
      for (auto const& [task_id, task_cluster_id] : clusterer_->taskToCluster()) {
        if (task_cluster_id == cluster_gid) {
          auto const* task = pd_.getTask(task_id);
          vt_lb_assert(task != nullptr, "Task must exist locally to migrate");
          tasks_to_migrate.push_back(*task);

          for (auto& edge : pd_.getCommunicationsRef()) {
            if (edge.getFrom() == task->getId() || edge.getTo() == task->getId()) {
              if (edge.getFrom() == task->getId()) {
                edge.setFromRank(rank);
              }
              if (edge.getTo() == task->getId()) {
                edge.setToRank(rank);
              }
              edges_to_migrate.push_back(edge);
            }
          }

          for (auto const& sb_id : task->getSharedBlocks()) {
            if (shared_blocks_id_set.find(sb_id) == shared_blocks_id_set.end()) {
              shared_blocks_id_set.insert(sb_id);
              shared_blocks_to_migrate.push_back(*pd_.getSharedBlock(sb_id));
            }
          }

          pd_.eraseTask(task->getId());
        }
      }

      outgoingCluster(cluster_gid, cluster_gid_summary);
    }

    handle_[rank].template send<&ThisType::migrationClusterHandler>(
      comm_.getRank(), token, cluster_gid, cluster_gid_summary,
      tasks_to_migrate, edges_to_migrate, shared_blocks_to_migrate,
      request_cluster_gid, sending_requested_cluster, dst_work_before
    );
  }

  void migrationClusterHandler(
    int from_rank,
    LockToken token,
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    std::vector<model::Task> const& tasks,
    std::vector<model::Edge> const& edges,
    std::vector<model::SharedBlock> const& shared_blocks,
    int request_cluster_gid,
    bool sending_requested_cluster,
    double dst_work_before
  ) {
    bool accept =
      sending_requested_cluster ||
      acceptIncomingClusterSwap(
        from_rank, token, cluster_gid, cluster_gid_summary, request_cluster_gid,
        dst_work_before
      );

    if (accept) {
      std::vector<model::TaskType> task_ids;
      for (auto const& task : tasks) {
        pd_.addTask(task);
        task_ids.push_back(task.getId());
      }

      for (auto const& edge : edges) {
        model::Edge e = edge;
        if (pd_.getTask(e.getFrom()) != nullptr) {
          e.setFromRank(comm_.getRank());
        }
        if (pd_.getTask(e.getTo()) != nullptr) {
          e.setToRank(comm_.getRank());
        }
        pd_.addCommunication(e);
      }

      for (auto const& sb : shared_blocks) {
        if (!pd_.hasSharedBlock(sb.getId())) {
          pd_.addSharedBlock(sb);
        }
      }

      if (cluster_gid != -1) {
        clusterer_->addCluster(task_ids, cluster_gid);
        incomingCluster(cluster_gid, cluster_gid_summary);
      }

      if (request_cluster_gid != -1) {
        auto iter =
          cluster_info_.at(this->comm_.getRank()).cluster_summaries.find(
            request_cluster_gid
          );
        vt_lb_assert(
          iter != cluster_info_.at(this->comm_.getRank()).cluster_summaries.end(),
          "request_cluster_gid not found in local summaries"
        );
        migrateCluster(from_rank, token, request_cluster_gid, iter->second, -1, true);
      } else if (sending_requested_cluster) {
        transactionComplete(token, TransactionStatus::Accepted);
      } else {
        handle_[from_rank].template send<&ThisType::clusterAccepted>(token, cluster_gid);
      }
    } else {
      handle_[from_rank].template send<&ThisType::sendBackClusterHandler>(
        token, cluster_gid, cluster_gid_summary, tasks
      );
    }
  }

  void clusterAccepted(LockToken token, [[maybe_unused]] int cluster_gid) {
    transactionComplete(token, TransactionStatus::Accepted);
  }

  void sendBackClusterHandler(
    LockToken token,
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    std::vector<model::Task> const& tasks
  ) {
    for (auto const& task : tasks) {
      pd_.addTask(task);
      for (auto& edge : pd_.getCommunicationsRef()) {
        if (edge.getFrom() == task.getId() || edge.getTo() == task.getId()) {
          if (edge.getFrom() == task.getId()) {
            edge.setFromRank(comm_.getRank());
          }
          if (edge.getTo() == task.getId()) {
            edge.setToRank(comm_.getRank());
          }
        }
      }
    }

    // A receive-only swap sends nothing, so there is nothing to restore
    if (cluster_gid != -1) {
      incomingCluster(cluster_gid, cluster_gid_summary);
    }
    transactionComplete(token, TransactionStatus::Rejected);
  }

  void transactionComplete(LockToken token, TransactionStatus status) {
    if (
      not active_lock_request_.has_value() or
      token != active_lock_request_->token or
      not active_lock_request_->waiting_for_transaction
    ) {
      return;
    }

    transaction_status_ = status;

    if (status == TransactionStatus::Accepted and pending_candidate_.has_value()) {
      auto const this_rank = comm_.getRank();
      auto const& best = *pending_candidate_;
      auto& ci_r = cluster_info_[this_rank];
      auto& ci_d = cluster_info_[best.dst_rank];

      ci_r.rank_breakdown = best.this_work_breakdown_after;
      ci_d.rank_breakdown = best.dst_work_breakdown_after;

      if (best.give_cluster_gid != -1) {
        if (
          auto it = ci_r.cluster_summaries.find(best.give_cluster_gid);
          it != ci_r.cluster_summaries.end()
        ) {
          ci_d.cluster_summaries[best.give_cluster_gid] = it->second;
          ci_r.cluster_summaries.erase(it);
        }
      }

      if (best.recv_cluster_gid != -1) {
        if (
          auto it = ci_d.cluster_summaries.find(best.recv_cluster_gid);
          it != ci_d.cluster_summaries.end()
        ) {
          ci_r.cluster_summaries[best.recv_cluster_gid] = it->second;
          ci_d.cluster_summaries.erase(it);
        }
      }
    }

    handle_[active_lock_request_->target_rank].template send<&ThisType::releaseLock>(
      token
    );
    active_lock_request_.reset();
    pending_candidate_.reset();
    tryGrantNextLock();
  }

  void outgoingCluster(
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary
  ) {
    auto& info = cluster_info_[this->comm_.getRank()];
    auto iter = info.cluster_summaries.find(cluster_gid);
    vt_lb_assert(
      iter != info.cluster_summaries.end(),
      "StrictClusterTransfer::outgoingCluster: cluster_gid not found in local summaries"
    );
    // Must be computed against the pre-swap summaries: the calculator reclassifies
    // edges and shared blocks by comparing local membership before and after
    auto const new_breakdown = WorkModelCalculator::computeWorkUpdateSummary(
      info, {}, cluster_gid_summary
    );
    info.cluster_summaries.erase(iter);
    info.rank_breakdown = new_breakdown;
  }

  void incomingCluster(
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary
  ) {
    vt_lb_assert(
      cluster_gid != -1,
      "StrictClusterTransfer::incomingCluster: cluster_gid must be a real cluster"
    );
    auto& info = cluster_info_[this->comm_.getRank()];
    // Must be computed against the pre-swap summaries; see outgoingCluster
    auto const new_breakdown = WorkModelCalculator::computeWorkUpdateSummary(
      info, cluster_gid_summary, {}
    );
    info.cluster_summaries[cluster_gid] = cluster_gid_summary;
    info.rank_breakdown = new_breakdown;
  }

  bool acceptIncomingClusterSwap(
    int from_rank,
    LockToken token,
    [[maybe_unused]] int give_cluster_gid,
    TaskClusterSummaryInfo const& give_cluster_gid_summary,
    int recv_cluster_gid,
    double dst_work_before
  ) {
    if (
      not is_locked_ or
      token != current_lock_token_ or
      from_rank != current_lock_token_.requester_rank
    ) {
      return false;
    }

    auto const this_rank = this->comm_.getRank();
    auto const& this_rank_info = cluster_info_.at(this_rank);

    bool contains_cluster =
      this_rank_info.cluster_summaries.contains(recv_cluster_gid);
    bool has_cluster_or_null = recv_cluster_gid == -1 || contains_cluster;
    if (!has_cluster_or_null) {
      return false;
    }

    TaskClusterSummaryInfo recv_cluster_summary{};
    if (recv_cluster_gid != -1) {
      recv_cluster_summary = this_rank_info.cluster_summaries.at(recv_cluster_gid);
    }

    if (
      config_.hasMemoryInfo() &&
      !WorkModelCalculator::checkMemoryFitUpdate(
        config_, this_rank_info, give_cluster_gid_summary, recv_cluster_summary
      )
    ) {
      return false;
    }

    auto new_bd = WorkModelCalculator::computeWorkUpdateSummary(
      this_rank_info, give_cluster_gid_summary, recv_cluster_summary
    );
    auto new_work = WorkModelCalculator::computeWork(config_.work_model_, new_bd);

    VT_LB_LOG(
      LoadBalancer, normal,
      "StrictClusterTransfer::acceptIncomingClusterSwap cluster_gid={}, has_cluster_or_null={}, "
      "new_work={}, dst_work_before={}\n",
      recv_cluster_gid, has_cluster_or_null, new_work, dst_work_before
    );

    return new_work <= dst_work_before;
  }

  bool hasTentativeLocalTransaction() const {
    return active_lock_request_.has_value() and
      active_lock_request_->waiting_for_transaction;
  }

private:
  CommT comm_;
  HandleType handle_;
  model::PhaseData& pd_;
  Clusterer* clusterer_ = nullptr;
  int global_max_clusters_ = 0;
  std::unordered_map<int, RankClusterInfo> cluster_info_;
  Statistics stats_;
  TransactionStatus transaction_status_ = TransactionStatus::Pending;
  bool is_locked_ = false;
  LockToken current_lock_token_ = {};
  std::set<PendingLockRequest> pending_lock_requests_;
  std::optional<ActiveLockRequest> active_lock_request_;
  std::optional<Candidate> pending_candidate_;
  std::uint64_t next_lock_sequence_ = 1;
  Configuration const& config_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_STRICT_CLUSTER_TRANSFER_H*/
