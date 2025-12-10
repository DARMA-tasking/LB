/*
//@HEADER
// *****************************************************************************
//
//                            cluster_summarizer.impl.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_IMPL_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_IMPL_H

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Communication.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/util/logging.h>

#include <cassert>

namespace vt_lb::algo::temperedlb {

template <typename CommT>
void ClusterSummarizer<CommT>::resolveClusterIDForTask(
  model::TaskType task_id,
  model::TaskType source_task_id,
  int source_global_cluster_id
) {
  auto iter = clusterer_->taskToCluster().find(task_id);
  assert(iter != clusterer_->taskToCluster().end() && "Task must be clustered here");
  int local_cluster_id = iter->second;
  int global_cluster_id = localToGlobalClusterID(
    local_cluster_id,
    comm_.getRank(),
    global_max_clusters_
  );
  handle_.template send<&ClusterSummarizer::recvClusterIDForTask>(
    task_id, global_cluster_id
  );
  recvClusterIDForTask(source_task_id, source_global_cluster_id);
}

template <typename CommT>
void ClusterSummarizer<CommT>::recvClusterIDForTask(
  model::TaskType task_id,
  int global_cluster_id
) {
  task_to_global_cluster_id_[task_id] = global_cluster_id;
}

template <typename CommT>
std::unordered_map<int, TaskClusterSummaryInfo>
ClusterSummarizer<CommT>::buildClusterSummaries(
  model::PhaseData const& pd,
  Configuration const& config
) {
  assert(clusterer_ != nullptr && "Clusterer must be initialized to build summaries");
  int const rank = pd.getRank();

  // Validate assumption: every task must be assigned to a cluster
  assert(allTasksClustered(*clusterer_, pd) && "All tasks must exist in at least one cluster");

  // Task -> local cluster id
  auto const& t2c = clusterer_->taskToCluster();

  // Prepare summary per local cluster id
  std::unordered_map<int, TaskClusterSummaryInfo> summary_by_local;
  for (auto const& cl : clusterer_->clusters()) {
    TaskClusterSummaryInfo info;
    info.cluster_id = localToGlobalClusterID(cl.id, rank, global_max_clusters_);
    info.num_tasks_ = static_cast<int>(cl.members.size());
    info.cluster_load = cl.load;
    summary_by_local.emplace(cl.id, std::move(info));
  }

  std::vector<model::Edge> to_resolve_later;

  // Walk communications: accumulate intra send/recv; collect broadened inter edges starting in a cluster
  for (auto const& e : pd.getCommunications()) {
    // Only consider edges that involve this rank
    if (e.getFromRank() != rank && e.getToRank() != rank) continue;

    auto u = e.getFrom();
    auto v = e.getTo();

    auto itu = t2c.find(u);
    auto itv = t2c.find(v);
    int cu = (itu != t2c.end()) ? itu->second : -1; // local cluster id or -1 if cluster is elsewhere
    int cv = (itv != t2c.end()) ? itv->second : -1;
    auto vol = e.getVolume();

    // Intra-cluster: both endpoints mapped and equal -> accumulate send/recv
    if (cu != -1 && cv != -1 && cu == cv) {
      assert(
        e.getFromRank() == e.getToRank() && e.getFromRank() == rank &&
        "Intra-cluster edge must be intra-rank"
      );
      auto& sum = summary_by_local.at(cu);
      sum.cluster_intra_send_bytes += vol;
      sum.cluster_intra_recv_bytes += vol;
      continue;
    }

    if (cu != -1 && cv != -1 && cu != cv) {
      // Inter-cluster on same rank: do not count as intra, but add to inter edges for both clusters
      model::ClusterEdge cluster_edge(
        localToGlobalClusterID(cu, rank, global_max_clusters_), // from cu
        localToGlobalClusterID(cv, rank, global_max_clusters_), // to cv
        vol
      );
      summary_by_local.at(cu).inter_edges_.push_back(cluster_edge);
      summary_by_local.at(cv).inter_edges_.push_back(cluster_edge);
      continue;
    }

    // Now, discover the target clusters that are not local to this rank
    if (cu != -1) {
      auto global_cu = localToGlobalClusterID(cu, rank, global_max_clusters_);
      // Source cluster is local; destination is remote
      auto to_rank = e.getToRank();
      // Request cluster ID for destination task, send cluster ID for source task
      handle_[to_rank].template send<&ClusterSummarizer::resolveClusterIDForTask>(
        e.getTo(), e.getFrom(), global_cu, vol
      );
      to_resolve_later.push_back(e);
    } else if (cv != -1) {
      to_resolve_later.push_back(e);
    }
  }

  // Wait for termination
  while (comm_.poll()) {
    // Process incoming requests for cluster IDs
  }

  // After all cluster IDs are resolved, process deferred edges
  for (auto const& e : to_resolve_later) {
    auto u = e.getFrom();
    auto v = e.getTo();
    auto itu = t2c.find(u);
    auto itv = t2c.find(v);
    int cu = (itu != t2c.end()) ? itu->second : -1;
    int cv = (itv != t2c.end()) ? itv->second : -1;
    auto vol = e.getVolume();

    // Only one endpoint is local, the other is remote
    int local_cluster = (cu != -1) ? cu : cv;
    int remote_task = (cu != -1) ? v : u;

    // Find the global cluster ID for the remote task
    auto it_remote_gid = task_to_global_cluster_id_.find(remote_task);
    assert(
      it_remote_gid != task_to_global_cluster_id_.end() &&
      "Should not happen if all resolutions are complete"
    );

    int remote_global_cluster_id = it_remote_gid->second;
    int local_global_cluster_id = localToGlobalClusterID(local_cluster, rank, global_max_clusters_);

    // Add the inter-cluster edge to the local cluster summary
    model::ClusterEdge cluster_edge(
      local_global_cluster_id,
      remote_global_cluster_id,
      vol
    );

    summary_by_local.at(local_cluster).inter_edges_.push_back(cluster_edge);
  }

  // Memory summaries per cluster (only if enabled)
  if (config.hasMemoryInfo()) {
    // Precompute a full set of task IDs for outside-cluster checks
    std::unordered_set<model::TaskType> all_tasks;
    for (auto const& kv : pd.getTasksMap()) {
      all_tasks.insert(kv.first);
    }

    for (auto const& cl : clusterer_->clusters()) {
      auto& sum = summary_by_local.at(cl.id);

      // Build set of tasks in this cluster
      std::unordered_set<model::TaskType> cluster_tasks(cl.members.begin(), cl.members.end());

      // Initialize maxima and footprint
      model::BytesType max_working_inside = 0;
      model::BytesType max_serialized_inside = 0;
      model::BytesType max_working_outside = 0;
      model::BytesType max_serialized_outside = 0;
      model::BytesType total_footprint = 0;

      // Collect shared blocks used by cluster tasks
      std::unordered_set<model::SharedBlockType> shared_blocks_in_cluster;

      // Iterate tasks to compute inside values and shared blocks
      for (auto t : cl.members) {
        auto it = pd.getTasksMap().find(t);
        if (it == pd.getTasksMap().end()) continue;
        auto const& task = it->second;

        // Footprint sum
        if (config.hasTaskFootprintMemoryInfo()) {
          total_footprint += task.getMemory().getFootprint();
        }

        // Max inside working/serialized
        if (config.hasTaskWorkingMemoryInfo()) {
          max_working_inside = std::max(max_working_inside, task.getMemory().getWorking());
        }
        if (config.hasTaskSerializedMemoryInfo()) {
          max_serialized_inside = std::max(max_serialized_inside, task.getMemory().getSerialized());
        }

        // Shared blocks union
        if (config.hasSharedBlockMemoryInfo()) {
          for (auto const& sb : task.getSharedBlocks()) {
            shared_blocks_in_cluster.insert(sb);
          }
        }
      }

      // Compute max outside values by scanning tasks not in this cluster
      if (config.hasTaskWorkingMemoryInfo() || config.hasTaskSerializedMemoryInfo()) {
        for (auto const& kv : pd.getTasksMap()) {
          auto const& task = kv.second;
          if (cluster_tasks.find(task.getId()) != cluster_tasks.end()) continue; // skip inside
          if (config.hasTaskWorkingMemoryInfo()) {
            max_working_outside = std::max(max_working_outside, task.getMemory().getWorking());
          }
          if (config.hasTaskSerializedMemoryInfo()) {
            max_serialized_outside = std::max(max_serialized_outside, task.getMemory().getSerialized());
          }
        }
      }

      // Fill shared_block_bytes_ with sizes
      if (config.hasSharedBlockMemoryInfo()) {
        for (auto const& sb : shared_blocks_in_cluster) {
          if (!pd.hasSharedBlock(sb)) continue;
          auto info = pd.getSharedBlock(sb);
          sum.shared_block_bytes_[sb] = info->getSize();
        }
      }

      // Store computed values on summary
      sum.max_object_working_bytes = max_working_inside;
      sum.max_object_serialized_bytes = max_serialized_inside;
      sum.max_object_working_bytes_outside = max_working_outside;
      sum.max_object_serialized_bytes_outside = max_serialized_outside;
      sum.cluster_footprint = total_footprint;
    }
  }

  // Emit summaries
  for (auto const& cl : clusterer_->clusters()) {
    auto const& sum = summary_by_local.at(cl.id);
    VT_LB_LOG(
      LoadBalancer,
      normal,
      "buildClusterSummaries cluster {} size={} load={:.2f} intra_send={:.2f} intra_recv={:.2f} "
      "inter_edges={} footprint={:.0f} max_work_in={:.0f} max_work_out={:.0f} "
      "max_ser_in={:.0f} max_ser_out={:.0f} shared_block_count={}\n",
      localToGlobalClusterID(cl.id, rank, global_max_clusters_),
      cl.members.size(),
      cl.load,
      sum.cluster_intra_send_bytes,
      sum.cluster_intra_recv_bytes,
      sum.inter_edges_.size(),
      sum.cluster_footprint,
      sum.max_object_working_bytes,
      sum.max_object_working_bytes_outside,
      sum.max_object_serialized_bytes,
      sum.max_object_serialized_bytes_outside,
      sum.shared_block_bytes_.size()
    );
  }

  return summary_by_local;
}

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_IMPL_H*/
