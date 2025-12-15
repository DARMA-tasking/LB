/*
//@HEADER
// *****************************************************************************
//
//                                 transfer.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_H

#include <vt-lb/model/types.h>
#include <vt-lb/comm/comm_traits.h>
#include <vt-lb/util/logging.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/statistics.h>
#include <vt-lb/algo/temperedlb/transfer_util.h>

#include <unordered_map>
#include <cassert>

namespace vt_lb::algo::temperedlb {

template <comm::Communicator CommT>
struct Transferer {
  using HandleType = typename CommT::template HandleType<Transferer<CommT>>;

  /**
   * @brief Construct a new Transferer object
   *
   * @note If one is not transferring clusters, clusterer can be nullptr
   * and global_max_clusters can be 0
   *
   * @param comm The communicator to use for transfers
   * @param pd The phase data containing tasks and communication information
   * @param clusterer (optional) ptr to a Clusterer object for cluster-based transfers
   * @param global_max_clusters The maximum number of clusters on any rank
   */
  Transferer(CommT& comm, model::PhaseData& pd, Clusterer* clusterer = nullptr, int global_max_clusters = 0)
    : comm_(comm.clone()),
      handle_(comm_.template registerInstanceCollective<Transferer<CommT>>(this)),
      pd_(pd),
      clusterer_(clusterer),
      global_max_clusters_(global_max_clusters)
  {
    // Remap all local cluster IDs to global IDs
    // This is required so that when we migrate clusters we don't have to remap the IDs
    clusterer_->remapClusterIDs(ClusterSummarizerUtil::buildLocalToGlobalClusterIDMap(
      comm_.getRank(),
      global_max_clusters_,
      clusterer_->clusters()
    ));
  }

  virtual ~Transferer() = default;

  struct TransferTask {
    model::Task task;
    std::vector<model::Edge> edges;
    std::vector<model::SharedBlock> shared_blocks;

    template <typename SerializerT>
    void serialize(SerializerT& s) {
      s | task;
      s | edges;
      s | shared_blocks;
    }
  };

  void migrateCluster(
    int const rank,
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    int request_cluster_gid
  ) {
    // Assume all clusters have been converted to global IDs already
    assert(clusterer_ != nullptr && "Clusterer must be initialized to migrate clusters");

    std::vector<model::Task> tasks_to_migrate;
    std::vector<model::Edge> edges_to_migrate;
    std::set<model::SharedBlockType> shared_blocks_id_set;
    std::vector<model::SharedBlock> shared_blocks_to_migrate;

    for (auto const& [task_id, task_cluster_id] : clusterer_->taskToCluster()) {
      if (task_cluster_id == cluster_gid) {
        auto const* task = pd_.getTask(task_id);
        assert(task != nullptr && "Task must exist locally to migrate");
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

        // Erase the tasks.. they are gone. If it is rejected, then we add them back
        pd_.eraseTask(task->getId());
      }
    }

    // Tell the transfer scheme that the cluster should be removed
    outgoingCluster(cluster_gid, cluster_gid_summary);

    VT_LB_LOG(
      LoadBalancer, normal,
      "Transferer::migrateCluster: migrating cluster_gid={} with {} tasks {} edges to rank {}\n",
      cluster_gid, tasks_to_migrate.size(), edges_to_migrate.size(), rank
    );

    handle_[rank].template send<&Transferer::migrationClusterHandler>(
      comm_.getRank(), cluster_gid, cluster_gid_summary,
      tasks_to_migrate, edges_to_migrate, shared_blocks_to_migrate,
      request_cluster_gid
    );
  }

  void migrateTask(int const rank, model::Task const& task, bool include_comm = false) {
    std::vector<model::Edge> edges;
    std::vector<model::SharedBlock> shared_blocks;

    if (include_comm) {
      for (auto const& sb_id : task.getSharedBlocks()) {
        auto const* sb = pd_.getSharedBlock(sb_id);
        if (sb != nullptr) {
          shared_blocks.push_back(*sb);
        }
      }
      for (auto const& edge : pd_.getCommunications()) {
        if (edge.getFrom() == task.getId() || edge.getTo() == task.getId()) {
          edges.push_back(edge);
        }
      }
    }

    migrate_tasks_[rank].push_back(TransferTask{task, edges, shared_blocks});
    pd_.eraseTask(task.getId());

    // We can't clean up everything here, because the task might come back.
    // Do this later once all migrations are done.
  }

  void doMigrations(bool do_cleanup = true) {
    for (auto&& [rank, tasks] : migrate_tasks_) {
      handle_[rank].template send<&Transferer::migrationHandler>(comm_.getRank(), tasks);
    }

    // Wait for termination on the migrations
    while (comm_.poll()) {
      // do nothing
    }

    migrate_tasks_.clear();

    if (do_cleanup) {
      cleanup();
    }
  }

private:
  void sendBackHandler(model::Task const& task) {
    // Add the task back
    pd_.addTask(task);
  }

  void migrationHandler(int from_rank, std::vector<TransferTask> const& tasks) {
    for (auto& [task, edges, shared_blocks] : tasks) {
      if (!acceptIncomingTask(task)) {
        VT_LB_LOG(
          LoadBalancer, normal,
          "Transferer::migrationHandler: rejecting incoming task {}, load {} due to load constraints\n",
          task.getId(),
          task.getLoad()
        );
        handle_[from_rank].template send<&Transferer::sendBackHandler>(task);
      } else {
        pd_.addTask(task);
        for (auto const& edge : edges) {
          pd_.addCommunication(edge);
        }
        for (auto const& sb : shared_blocks) {
          if (!pd_.hasSharedBlock(sb.getId())) {
            pd_.addSharedBlock(sb);
          }
        }
      }
    }
  }

  void migrationClusterHandler(
    int from_rank,
    int cluster_gid,
    TaskClusterSummaryInfo cluster_gid_summary,
    std::vector<model::Task> const& tasks,
    std::vector<model::Edge> const& edges,
    std::vector<model::SharedBlock> const& shared_blocks,
    int request_cluster_gid
  ) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "Transferer::migrationClusterHandler: received cluster_gid={} with {} tasks {} edges from rank {}\n",
      cluster_gid, tasks.size(), edges.size(), from_rank
    );

    if (acceptIncomingClusterSwap(from_rank, cluster_gid, request_cluster_gid)) {
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

      // Add new cluster of tasks to the clusterer, used to extract tasks for future migrations
      clusterer_->addCluster(task_ids, cluster_gid);

      // Add the cluster to the bookkeeping
      incomingCluster(cluster_gid, cluster_gid_summary);

      if (request_cluster_gid != -1) {
        // Send back the requested cluster
        migrateCluster(from_rank, request_cluster_gid, getClusterSummary(request_cluster_gid), -1);
      }
    } else {
      VT_LB_LOG(
        LoadBalancer, normal,
        "Transferer::migrationClusterHandler: rejecting incoming cluster_gid={} from rank {}\n",
        cluster_gid, from_rank
      );
      // Send back all tasks
      handle_[from_rank].template send<&Transferer::sendBackClusterHandler>(
        cluster_gid, cluster_gid_summary, tasks
      );
    }
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
    }

     // Add the cluster to the bookkeeping
    incomingCluster(cluster_gid, cluster_gid_summary);
  }

  void cleanup() {
    // 1) Remove communications where neither endpoint exists locally
    pd_.purgeDanglingCommunications();

    // 2) Compute shared-block references from current tasks
    std::unordered_map<model::SharedBlockType, int> refcounts;
    for (auto const& [id, task] : pd_.getTasksMap()) {
      for (auto const& sb : task.getSharedBlocks()) {
        ++refcounts[sb];
      }
    }

    // 3) Remove shared blocks with zero references on this rank
    for (auto const& [sb_id, sb] : pd_.getSharedBlocksMap()) {
      if (refcounts.find(sb_id) == refcounts.end()) {
        pd_.eraseSharedBlock(sb_id);
      }
    }
  }

protected:
  /**
   * \brief The transfer scheme that inherits from this class can choose
   * whether to not to accept an incoming task by overriding this method
   *
   * \param[in] task the incoming task
   *
   * \return true to accept the task, false to reject it
   */
  virtual bool acceptIncomingTask(
    [[maybe_unused]] model::Task const& task
  ) { return true; }

  /**
   * \brief The transfer scheme that inherits from this class can choose
   * whether to not to accept an incoming cluster swap by overriding this method
   *
   * \param[in] from_rank the rank sending the cluster
   * \param[in] give_cluster_gid the cluster GID being given away
   * \param[in] recv_cluster_gid the cluster GID being received
   *
   * \return true to accept the cluster swap, false to reject it
   */
  virtual bool acceptIncomingClusterSwap(
    [[maybe_unused]] int from_rank,
    [[maybe_unused]] int give_cluster_gid,
    [[maybe_unused]] int recv_cluster_gid
  ) { return true; }

  /**
   * \brief The transfer scheme that inherits from this class can handle
   * incoming cluster information by overriding this method
   *
   * \param[in] cluster_gid the cluster GID being received
   * \param[in] cluster_gid_summary the summary info for the cluster being received
   */
  virtual void incomingCluster(
    [[maybe_unused]] int cluster_gid,
    [[maybe_unused]] TaskClusterSummaryInfo cluster_gid_summary
  ) {}

  /**
   * \brief The transfer scheme that inherits from this class can handle
   * outgoing cluster information by overriding this method
   *
   * \param[in] cluster_gid the cluster GID being removed
   * \param[in] cluster_gid_summary the summary info for the cluster being removed
   */
  virtual void outgoingCluster(
    [[maybe_unused]] int cluster_gid,
    [[maybe_unused]] TaskClusterSummaryInfo cluster_gid_summary
  ) {}

  /**
   * \brief The transfer scheme that inherits from this class can return
   * the summary info for a cluster by overriding this method
   *
   * \param[in] cluster_gid the cluster GID to get the summary for
   *
   * \return the summary info for the cluster
   */
  virtual TaskClusterSummaryInfo getClusterSummary(
    [[maybe_unused]] int cluster_gid
  ) { return TaskClusterSummaryInfo{}; }

protected:
  CommT comm_;
  HandleType handle_;
  model::PhaseData& pd_;
  std::unordered_map<int, std::vector<TransferTask>> migrate_tasks_;
  Clusterer* clusterer_ = nullptr;
  int global_max_clusters_ = 0;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_H*/