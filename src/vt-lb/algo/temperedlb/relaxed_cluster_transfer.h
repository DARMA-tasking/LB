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
#include <vt-lb/util/assert.h>
#include <vt-lb/util/logging.h>

#include <unordered_map>
#include <limits>

#define PRINT_TOP_N_CANDIDATES 0

namespace vt_lb::algo::temperedlb {

template <comm::Communicator CommT>
struct RelaxedClusterTransfer {
  using ThisType = RelaxedClusterTransfer<CommT>;
  using HandleType = typename CommT::template HandleType<ThisType>;

  RelaxedClusterTransfer(
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
      // Remap all local cluster IDs to global IDs
      // This is required so that when we migrate clusters we don't have to remap the IDs
      clusterer_->remapClusterIDs(ClusterSummarizerUtil::buildLocalToGlobalClusterIDMap(
        comm_.getRank(),
        global_max_clusters_,
        clusterer_->clusters()
      ));
    }
  }

  struct Candidate {
    int dst_rank = -1;
    int give_cluster_gid = -1;
    int recv_cluster_gid = -1;
    double this_work_before = 0.0, this_work_after = 0.0;
    WorkBreakdown this_work_breakdown_after = {};
    double dst_work_before = 0.0, dst_work_after = 0.0;
    WorkBreakdown dst_work_breakdown_after = {};
    double improvement = 0.0; // w_max_0 - w_max_new
  };

  Candidate findBestSwapCandidate() {
    int this_rank = this->comm_.getRank();
    RankClusterInfo const& this_rank_info = cluster_info_.at(this_rank);
    auto const& local_cluster_summaries = this_rank_info.cluster_summaries;

    std::vector<int> dest_ranks;
    dest_ranks.reserve(cluster_info_.size());
    for (auto const& [rank, _] : cluster_info_) {
      if (rank != this_rank) {
        dest_ranks.push_back(rank);
      }
    }

    // Precompute "before" work per known rank
    std::unordered_map<int, double> before_work;
    for (auto const& [rank, info] : cluster_info_) {
      before_work[rank] = WorkModelCalculator::computeWork(
        config_.work_model_, info.rank_breakdown
      );
    }

    std::vector<Candidate> candidates;

    auto eval_swap = [&](int dst_rank, int give_gid, int recv_gid) -> Candidate {
      Candidate c{dst_rank, give_gid, recv_gid, 0.0, 0.0, 0.0};

      RankClusterInfo const& dst_info = cluster_info_.at(dst_rank);
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

      // Check memory fit on this rank
      if (config_.hasMemoryInfo()) {
        bool fits = WorkModelCalculator::checkMemoryFitUpdate(
          config_, this_rank_info, to_add_this, to_remove_this,
          this->pd_.getRankMaxMemoryAvailable() // assume all ranks have equal memory available
        );
        if (!fits) {
          c.improvement = -std::numeric_limits<double>::infinity();
          return c;
        }
      }

      // Check memory fit on destination rank
      if (config_.hasMemoryInfo()) {
        bool fits = WorkModelCalculator::checkMemoryFitUpdate(
          config_, dst_info, to_add_dst, to_remove_dst,
          this->pd_.getRankMaxMemoryAvailable() // assume all ranks have equal memory available
        );
        if (!fits) {
          c.improvement = -std::numeric_limits<double>::infinity();
          return c;
        }
      }

      // Compute post-swap work on this rank
      c.this_work_breakdown_after = WorkModelCalculator::computeWorkUpdateSummary(
        this_rank_info, to_add_this, to_remove_this
      );
      c.this_work_after = WorkModelCalculator::computeWork(
        config_.work_model_, c.this_work_breakdown_after
      );

      // Compute post-swap work on destination rank
      c.dst_work_breakdown_after = WorkModelCalculator::computeWorkUpdateSummary(
        dst_info, to_add_dst, to_remove_dst
      );
      c.dst_work_after = WorkModelCalculator::computeWork(
        config_.work_model_, c.dst_work_breakdown_after
      );

      // Criterion: improvement in max work
      double before_w_src = before_work[this_rank];
      double before_w_dst = before_work[dst_rank];
      double w_max_0 = std::max(before_w_src, before_w_dst);
      double w_max_new = std::max(c.this_work_after, c.dst_work_after);
      c.improvement = w_max_0 - w_max_new;
      c.this_work_before = before_w_src;
      c.dst_work_before = before_w_dst;

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
      return Candidate{};
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

#if PRINT_TOP_N_CANDIDATES
    // Print top N candidates
    int n_print = std::min(5, static_cast<int>(candidates.size()));
    for (int i = 0; i < n_print; ++i) {
      const auto& c = candidates[i];
      VT_LB_LOG(
        LoadBalancer, normal,
        "RelaxedClusterTransfer: candidate[{}] dst_rank={} give_gid={} recv_gid={} "
        "this_work_after={:.2f} dst_work_after={:.2f} improvement={:.2f}\n",
        i, c.dst_rank, c.give_cluster_gid, c.recv_cluster_gid,
        c.this_work_after, c.dst_work_after, c.improvement
      );
    }
#endif

    auto const& best = candidates.front();
    VT_LB_LOG(
      LoadBalancer, normal,
      "RelaxedClusterTransfer: best candidate dst_rank={} give_gid={} recv_gid={} "
      "this_work_before={:.2f} this_work_after={:.2f} dst_work_before={:.2f} dst_work_after={:.2f} improvement={:.2f}\n",
      best.dst_rank, best.give_cluster_gid, best.recv_cluster_gid,
      best.this_work_before, best.this_work_after, best.dst_work_before, best.dst_work_after, best.improvement
    );

    return best;
  }

  void run() {
    int this_rank = this->comm_.getRank();
    RankClusterInfo const& this_rank_info = cluster_info_.at(this_rank);

    if (WorkModelCalculator::computeWork(config_.work_model_, this_rank_info.rank_breakdown) <= stats_.avg) {
      // do nothing
    } else {
      bool found_good_swap = true;

      while (found_good_swap && transaction_status_ != TransactionStatus::Rejected) {
        auto best = findBestSwapCandidate();
        if (best.improvement > 0.0) {
          VT_LB_LOG(
            LoadBalancer, normal,
            "RelaxedClusterTransfer: executing swap dst_rank={} give_gid={} recv_gid={} "
            "this_work_after={:.2f} dst_work_after={:.2f} improvement={:.2f}\n",
            best.dst_rank, best.give_cluster_gid, best.recv_cluster_gid,
            best.this_work_after, best.dst_work_after, best.improvement
          );
          TaskClusterSummaryInfo give_cluster_summary{};
          if (best.give_cluster_gid != -1) {
            give_cluster_summary = this_rank_info.cluster_summaries.at(best.give_cluster_gid);
          }
          this->migrateCluster(
            best.dst_rank, best.give_cluster_gid, give_cluster_summary, best.recv_cluster_gid,
            false, best.dst_work_before
          );
        } else {
          found_good_swap = false;
          continue;
        }

        transaction_status_ = TransactionStatus::Pending;
        while (transaction_status_ == TransactionStatus::Pending && this->comm_.poll()) {
          // do nothing
        }

        if (transaction_status_ == TransactionStatus::Accepted) {
          VT_LB_LOG(
            LoadBalancer, normal,
            "RelaxedClusterTransfer: swap accepted dst_rank={} give_gid={} recv_gid={}\n",
            best.dst_rank, best.give_cluster_gid, best.recv_cluster_gid
          );

          auto& ci_r = cluster_info_[this_rank];
          auto& ci_d = cluster_info_[best.dst_rank];

          ci_r.rank_breakdown = best.this_work_breakdown_after;
          ci_d.rank_breakdown = best.dst_work_breakdown_after;

          if (best.give_cluster_gid != -1) {
            if (auto it = ci_r.cluster_summaries.find(best.give_cluster_gid); it != ci_r.cluster_summaries.end()) {
              ci_d.cluster_summaries[best.give_cluster_gid] = it->second;
              ci_r.cluster_summaries.erase(it);
            }
          }
          if (best.recv_cluster_gid != -1) {
            if (auto it = ci_d.cluster_summaries.find(best.recv_cluster_gid); it != ci_d.cluster_summaries.end()) {
              ci_r.cluster_summaries[best.recv_cluster_gid] = it->second;
              ci_d.cluster_summaries.erase(it);
            }
          }

          auto new_work = WorkModelCalculator::computeWork(
            config_.work_model_, ci_r.rank_breakdown
          );
          VT_LB_LOG(
            LoadBalancer, normal,
            "RelaxedClusterTransfer: post-swap this_rank={} new_work={:.2f}\n",
            this_rank, new_work
          );
        }
      }
    }

    // Just wait for termination and process requests
    while (this->comm_.poll()) {
      // do nothing
    }
  }

  void migrateCluster(
    int const rank,
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    int request_cluster_gid,
    bool sending_requested_cluster = false,
    double dst_work_before = 0.0
  ) {
    // Assume all clusters have been converted to global IDs already
    vt_lb_assert(clusterer_ != nullptr, "Clusterer must be initialized to migrate clusters");

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
              VT_LB_LOG(
                LoadBalancer, normal,
                "Transferer::migrateCluster: migrating edge from task {} rank {} to task {} rank {} volume {}\n",
                edge.getFrom(), edge.getFromRank(), edge.getTo(), edge.getToRank(), edge.getVolume()
              );
              edges_to_migrate.push_back(edge);
            }
          }

          for (auto const& sb_id : task->getSharedBlocks()) {
            if (shared_blocks_id_set.find(sb_id) == shared_blocks_id_set.end()) {
              shared_blocks_id_set.insert(sb_id);
              shared_blocks_to_migrate.push_back(*pd_.getSharedBlock(sb_id));
            }
          }

          // Erase the tasks.. they are gone. If it is rejected, then we add them back
          pd_.eraseTask(task->getId());
        }
      }

      // Tell the transfer scheme that the cluster should be removed
      outgoingCluster(cluster_gid, cluster_gid_summary);
    }

    VT_LB_LOG(
      LoadBalancer, normal,
      "Transferer::migrateCluster: migrating cluster_gid={} with {} tasks {} edges to rank {}\n",
      cluster_gid, tasks_to_migrate.size(), edges_to_migrate.size(), rank
    );

    handle_[rank].template send<&ThisType::migrationClusterHandler>(
      comm_.getRank(), cluster_gid, cluster_gid_summary,
      tasks_to_migrate, edges_to_migrate, shared_blocks_to_migrate,
      request_cluster_gid, sending_requested_cluster, dst_work_before
    );
  }

  void migrationClusterHandler(
    int from_rank,
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    std::vector<model::Task> const& tasks,
    std::vector<model::Edge> const& edges,
    std::vector<model::SharedBlock> const& shared_blocks,
    int request_cluster_gid,
    bool sending_requested_cluster,
    double dst_work_before
  ) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "Transferer::migrationClusterHandler: received cluster_gid={} with {} tasks {} edges from rank {}\n",
      cluster_gid, tasks.size(), edges.size(), from_rank
    );

    // If we are sending back a requested cluster, always accept it
    if (sending_requested_cluster || acceptIncomingClusterSwap(from_rank, cluster_gid, request_cluster_gid, dst_work_before)) {
      std::vector<model::TaskType> task_ids;
      // Add all received tasks to local PhaseData
      for (auto const& task : tasks) {
        VT_LB_LOG(
          LoadBalancer, normal,
          "Transferer::migrationClusterHandler: adding task {} from cluster_gid={} received from rank {}\n",
          task.getId(), cluster_gid, from_rank
        );
        pd_.addTask(task);
        task_ids.push_back(task.getId());
      }
      // Add all received edges
      for (auto& edge : edges) {
        model::Edge e = edge;
        if (pd_.getTask(e.getFrom()) != nullptr) {
          e.setFromRank(comm_.getRank());
        }
         if (pd_.getTask(e.getTo()) != nullptr) {
          e.setToRank(comm_.getRank());
        }
        pd_.addCommunication(e);
      }
      // Add all received shared blocks
      for (auto const& sb : shared_blocks) {
        if (!pd_.hasSharedBlock(sb.getId())) {
          pd_.addSharedBlock(sb);
        }
      }

      if (cluster_gid != -1) {
        // Add new cluster of tasks to the clusterer, used to extract tasks for future migrations
        clusterer_->addCluster(task_ids, cluster_gid);

        // Add the cluster to the bookkeeping
        incomingCluster(cluster_gid, cluster_gid_summary);
      }

      if (request_cluster_gid != -1) {
        auto iter = cluster_info_.at(this->comm_.getRank()).cluster_summaries.find(request_cluster_gid);
        vt_lb_assert(
          iter != cluster_info_.at(this->comm_.getRank()).cluster_summaries.end(),
          "request_cluster_gid not found in local summaries"
        );
        // Send back the requested cluster
        migrateCluster(from_rank, request_cluster_gid, iter->second, -1, true);
      } else {
        if (sending_requested_cluster) {
          // The cluster sent back from the swap is complete; notify that the transaction is complete
          transactionComplete(TransactionStatus::Accepted);
        } else {
          // This is a null swap, so notify of completion
          handle_[from_rank].template send<&ThisType::clusterAccepted>(
            cluster_gid
          );
        }
      }
    } else {
      VT_LB_LOG(
        LoadBalancer, normal,
        "Transferer::migrationClusterHandler: rejecting incoming cluster_gid={} from rank {}\n",
        cluster_gid, from_rank
      );
      // Send back all tasks
      handle_[from_rank].template send<&ThisType::sendBackClusterHandler>(
        cluster_gid, cluster_gid_summary, tasks
      );
    }
  }

  void clusterAccepted(int cluster_gid) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "Transferer::clusterAccepted: cluster_gid={} accepted by remote rank\n",
      cluster_gid
    );
    // Cluster is accepted; notify that the transaction is complete
    transactionComplete(TransactionStatus::Accepted);
  }

  void sendBackClusterHandler(
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    std::vector<model::Task> const& tasks
  ) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "Transferer::sendBackClusterHandler: cluster sent back cluster_gid={} with {} tasks\n",
      cluster_gid, tasks.size()
    );

    for (auto const& task : tasks) {
      pd_.addTask(task);

      // Reset the edge endpoints
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

     // Add the cluster to the bookkeeping
    incomingCluster(cluster_gid, cluster_gid_summary);

    // Cluster is sent back; notify that the transaction is complete
    transactionComplete(TransactionStatus::Rejected);
  }

  void transactionComplete(TransactionStatus status) {
    transaction_status_ = status;
  }

  void outgoingCluster(
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary
  ) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "RelaxedClusterTransfer::outgoingCluster removing cluster_gid={}\n",
      cluster_gid
    );
    auto iter = cluster_info_[this->comm_.getRank()].cluster_summaries.find(cluster_gid);
    vt_lb_assert(
      iter != cluster_info_[this->comm_.getRank()].cluster_summaries.end(),
      "RelaxedClusterTransfer::outgoingCluster: cluster_gid not found in local summaries"
    );
    cluster_info_[this->comm_.getRank()].cluster_summaries.erase(iter);
    cluster_info_[this->comm_.getRank()].rank_breakdown = WorkModelCalculator::computeWorkUpdateSummary(
      cluster_info_[this->comm_.getRank()], {}, cluster_gid_summary
    );
  }

  void incomingCluster(
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary
  ) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "RelaxedClusterTransfer::incomingCluster adding cluster_gid={}\n",
      cluster_gid
    );
    cluster_info_[this->comm_.getRank()].cluster_summaries[cluster_gid] = cluster_gid_summary;
    cluster_info_[this->comm_.getRank()].rank_breakdown = WorkModelCalculator::computeWorkUpdateSummary(
      cluster_info_[this->comm_.getRank()], cluster_gid_summary, {}
    );
  }

  bool acceptIncomingClusterSwap(
    [[maybe_unused]] int from_rank,
    [[maybe_unused]] int give_cluster_gid,
    int recv_cluster_gid,
    double dst_work_before
  ) {
    bool has_cluster =
      recv_cluster_gid == -1 ||
      cluster_info_.at(this->comm_.getRank()).cluster_summaries.contains(recv_cluster_gid);

    auto current_work = WorkModelCalculator::computeWork(
      config_.work_model_, cluster_info_[this->comm_.getRank()].rank_breakdown
    );

    VT_LB_LOG(
      LoadBalancer, normal,
      "RelaxedClusterTransfer::acceptIncomingClusterSwap cluster_gid={}, has_cluster={}, current_work={}, dst_work_before={}\n",
      recv_cluster_gid, has_cluster, current_work, dst_work_before
    );

    // For relaxed approach, we accept if we still have the recv_cluster_gid
    if (has_cluster && current_work <= dst_work_before) {
      return true;
    }
    return false;
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
  Configuration const& config_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_RELAXED_CLUSTER_TRANSFER_H*/