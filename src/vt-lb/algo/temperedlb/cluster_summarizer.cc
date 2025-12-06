/*
//@HEADER
// *****************************************************************************
//
//                             cluster_summarizer.cc
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

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Communication.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/util/logging.h>

#define VT_LB_LOG(mode, ...) ::vt_lb::util::log(::vt_lb::util::Component::LoadBalancer, ::vt_lb::util::Verbosity::mode, __VA_ARGS__)

namespace vt_lb::algo::temperedlb {

/*static*/ std::unordered_map<int, TaskClusterSummaryInfo>
ClusterSummarizer::buildClusterSummaries(
  model::PhaseData const& pd,
  Configuration const& config,
  Clusterer const* clusterer_,
  int global_max_clusters
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
    info.cluster_id = localToGlobalClusterID(cl.id, rank, global_max_clusters);
    info.num_tasks_ = static_cast<int>(cl.members.size());
    info.cluster_load = cl.load;
    summary_by_local.emplace(cl.id, std::move(info));
  }

  // Walk communications: accumulate intra send/recv; collect broadened inter edges starting in a cluster
  for (auto const& e : pd.getCommunications()) {
    // Only consider edges that involve this rank
    if (e.getFromRank() != rank && e.getToRank() != rank) continue;

    auto u = e.getFrom();
    auto v = e.getTo();
    if (!pd.hasTask(u) || !pd.hasTask(v)) continue;

    auto itu = t2c.find(u);
    auto itv = t2c.find(v);
    int cu = (itu != t2c.end()) ? itu->second : -1; // local cluster id or -1 if unclustered
    int cv = (itv != t2c.end()) ? itv->second : -1;
    auto vol = e.getVolume();

    // Intra-cluster: both endpoints mapped and equal -> accumulate send/recv
    if (cu != -1 && cv != -1 && cu == cv) {
      auto& sum = summary_by_local.at(cu);
      if (e.getFromRank() == rank) {
        sum.cluster_intra_send_bytes += vol;
      }
      if (e.getToRank() == rank) {
        sum.cluster_intra_recv_bytes += vol;
      }
      continue;
    }

    // Broadened "inter" edges: if the edge starts in a cluster on this rank, add to that cluster
    // Conditions:
    //  - source endpoint (u) is clustered locally (cu != -1)
    //  - source is on this rank (e.getFromRank() == rank)
    //  - destination is:
    //      * in a different local cluster (cv == -1 or cv != cu), or
    //      * on a different rank (e.getToRank() != rank)
    if (e.getFromRank() == rank && cu != -1) {
      bool dest_is_external =
        (cv == -1) || (cv != cu) || (e.getToRank() != rank);
      if (dest_is_external) {
        summary_by_local.at(cu).inter_edges_.push_back(e);
      }
    }

    // Enable vice-versa logic to capture edges entering a cluster
    // Conditions:
    //  - destination endpoint (v) is clustered locally (cv != -1)
    //  - destination is on this rank (e.getToRank() == rank)
    //  - source is:
    //      * in a different local cluster (cu == -1 or cu != cv), or
    //      * on a different rank (e.getFromRank() != rank)
    if (e.getToRank() == rank && cv != -1) {
      bool src_is_external = (cu == -1) || (cu != cv) || (e.getFromRank() != rank);
      if (src_is_external) {
        summary_by_local.at(cv).inter_edges_.push_back(e);
      }
    }
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
      normal,
      "buildClusterSummaries cluster {} size={} load={:.2f} intra_send={:.2f} intra_recv={:.2f} "
      "inter_edges={} footprint={:.0f} max_work_in={:.0f} max_work_out={:.0f} "
      "max_ser_in={:.0f} max_ser_out={:.0f} shared_block_count={}\n",
      localToGlobalClusterID(cl.id, rank, global_max_clusters),
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

#undef VT_LB_LOG