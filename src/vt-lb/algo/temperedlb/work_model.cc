/*
//@HEADER
// *****************************************************************************
//
//                                work_model.cc
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

#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>

#include <cassert>
#include <unordered_set>
#include <cmath>

namespace vt_lb::algo::temperedlb {

/*static*/ WorkBreakdown WorkModelCalculator::computeWorkBreakdown(
  model::PhaseData const& phase_data
) {
  auto rank = phase_data.getRank();
  WorkBreakdown breakdown;
  std::unordered_set<model::SharedBlockType> shared_blocks_here;

  // Rank-alpha term
  for (auto const& [id, task] : phase_data.getTasksMap()) {
    breakdown.compute += task.getLoad();
    for (auto const& sb : task.getSharedBlocks()) {
      shared_blocks_here.insert(sb);
    }
  }

  // Communication terms
  for (auto const& e : phase_data.getCommunications()) {
    assert(
      (e.getFromRank() == rank || e.getToRank() == rank) &&
      "Edge does not belong to this rank"
    );
    if (e.getFromRank() != e.getToRank()) {
      if (e.getToRank() == rank) {
        breakdown.inter_node_recv_comm += e.getVolume();
      } else {
        breakdown.inter_node_send_comm += e.getVolume();
      }
    } else {
      // Intra-node: for this rank, edge is both sent and received locally
      if (e.getToRank() == rank) {
        breakdown.intra_node_recv_comm += e.getVolume();
        breakdown.intra_node_send_comm += e.getVolume();
      }
    }
  }

  // Shared-memory communication term
  for (auto const& sb : shared_blocks_here) {
    assert(phase_data.hasSharedBlock(sb) && "Shared block information missing");
    auto info = phase_data.getSharedBlock(sb);
    if (info->getHome() != rank) {
      breakdown.shared_mem_comm += info->getSize();
    }
  }

  return breakdown;
}

/*static*/ double WorkModelCalculator::computeWorkUpdate(
  model::PhaseData const& phase_data,
  WorkModel const& model,
  WorkBreakdown breakdown,
  std::vector<model::Task> const& to_add,
  std::vector<model::Edge> to_add_edges,
  std::vector<model::TaskType> const& to_remove
) {
  auto new_bd = breakdown;
  int const rank = phase_data.getRank();

  std::unordered_set<model::TaskType> remove_set(to_remove.begin(), to_remove.end());

  // Adjust compute for removed and added tasks
  if (!remove_set.empty()) {
    auto const& tasks_map = phase_data.getTasksMap();
    for (auto const& id : remove_set) {
      auto it = tasks_map.find(id);
      if (it != tasks_map.end()) {
        new_bd.compute -= it->second.getLoad();
      }
    }
  }
  for (auto const& t : to_add) {
    new_bd.compute += t.getLoad();
  }

  // Subtract inter/intra comm for edges incident to removed local tasks
  if (!remove_set.empty()) {
    for (auto const& e : phase_data.getCommunications()) {
      bool local_is_from = (e.getFromRank() == rank);
      bool local_is_to   = (e.getToRank()   == rank);
      if (!local_is_from && !local_is_to) continue;

      model::TaskType local_task = local_is_from ? e.getFrom() : e.getTo();
      if (remove_set.find(local_task) == remove_set.end()) {
        continue;
      }

      if (e.getFromRank() != e.getToRank()) {
        if (e.getToRank() == rank) {
          new_bd.inter_node_recv_comm -= e.getVolume();
        } else if (e.getFromRank() == rank) {
          new_bd.inter_node_send_comm -= e.getVolume();
        }
      } else {
        // Intra-node: subtract both send and recv for local removal
        if (local_is_to || local_is_from) {
          new_bd.intra_node_recv_comm -= e.getVolume();
          new_bd.intra_node_send_comm -= e.getVolume();
        }
      }
    }
  }

  // Add inter/intra comm for newly added edges
  for (auto const& e : to_add_edges) {
    if (e.getFromRank() != e.getToRank()) {
      if (e.getToRank() == rank) {
        new_bd.inter_node_recv_comm += e.getVolume();
      } else if (e.getFromRank() == rank) {
        new_bd.inter_node_send_comm += e.getVolume();
      }
    } else {
      // Intra-node: add both send and recv for local additions
      if (e.getToRank() == rank || e.getFromRank() == rank) {
        new_bd.intra_node_recv_comm += e.getVolume();
        new_bd.intra_node_send_comm += e.getVolume();
      }
    }
  }

  // Recompute shared-memory communication from final shared blocks
  {
    std::unordered_set<model::SharedBlockType> final_shared_blocks;

    // Existing tasks minus removed
    for (auto const& [id, task] : phase_data.getTasksMap()) {
      if (remove_set.find(id) != remove_set.end()) continue;
      for (auto const& sb : task.getSharedBlocks()) {
        final_shared_blocks.insert(sb);
      }
    }
    // Add new tasks
    for (auto const& t : to_add) {
      for (auto const& sb : t.getSharedBlocks()) {
        final_shared_blocks.insert(sb);
      }
    }

    double shared_comm_bytes = 0.0;
    for (auto const& sb : final_shared_blocks) {
      assert(phase_data.hasSharedBlock(sb) && "Shared block information missing");
      auto info = phase_data.getSharedBlock(sb);
      if (info->getHome() != rank) {
        shared_comm_bytes += info->getSize();
      }
    }
    new_bd.shared_mem_comm = shared_comm_bytes;
  }

  // Clamp negatives
  new_bd.compute                 = std::max(0.0, new_bd.compute);
  new_bd.inter_node_recv_comm    = std::max(0.0, new_bd.inter_node_recv_comm);
  new_bd.inter_node_send_comm    = std::max(0.0, new_bd.inter_node_send_comm);
  new_bd.intra_node_recv_comm    = std::max(0.0, new_bd.intra_node_recv_comm);
  new_bd.intra_node_send_comm    = std::max(0.0, new_bd.intra_node_send_comm);
  new_bd.shared_mem_comm         = std::max(0.0, new_bd.shared_mem_comm);

  // Compute work with updated breakdown; uses max(send, recv) for inter/intra
  return computeWork(model, new_bd);
}

/*static*/ double WorkModelCalculator::computeWork(
  WorkModel const& model, WorkBreakdown const& breakdown
) {
  return model.applyWorkFormula(
    breakdown.compute,
    std::max(breakdown.inter_node_recv_comm, breakdown.inter_node_send_comm),
    std::max(breakdown.intra_node_recv_comm, breakdown.intra_node_send_comm),
    breakdown.shared_mem_comm
  );
}

/*static*/ double WorkModelCalculator::computeMemoryUsage(
  Configuration const& config,
  model::PhaseData const& phase_data
) {
  if (!config.hasMemoryInfo()) {
    return 0.0;
  }

  double task_footprint_bytes_ = 0.0;
  double task_max_working_bytes_ = 0.0;
  double task_max_serialized_bytes_ = 0.0;
  double shared_blocks_bytes_ = 0.0;
  std::unordered_set<model::SharedBlockType> shared_blocks_here;
  for (auto const& [id, task] : phase_data.getTasksMap()) {
    if (config.hasTaskFootprintMemoryInfo()) {
      task_footprint_bytes_ += task.getMemory().getFootprint();
    }
    if (config.hasTaskWorkingMemoryInfo()) {
      task_max_working_bytes_ = std::max(
        task_max_working_bytes_, task.getMemory().getWorking()
      );
    }
    if (config.hasTaskSerializedMemoryInfo()) {
      task_max_serialized_bytes_ = std::max(
        task_max_serialized_bytes_, task.getMemory().getSerialized()
      );
    }
    if (config.hasSharedBlockMemoryInfo()) {
      for (auto const& sb : task.getSharedBlocks()) {
        shared_blocks_here.insert(sb);
      }
    }
  }
  for (auto const& sb : shared_blocks_here) {
    assert(phase_data.hasSharedBlock(sb) && "Shared block information missing");
    auto info = phase_data.getSharedBlock(sb);
    shared_blocks_bytes_ += info->getSize();
  }
  return phase_data.getRankFootprintBytes() +
    task_footprint_bytes_ +
    task_max_working_bytes_ +
    task_max_serialized_bytes_ +
    shared_blocks_bytes_;
}

/*static*/ bool WorkModelCalculator::checkMemoryFit(
  Configuration const& config,
  model::PhaseData const& phase_data,
  double total_memory_usage
) {
  if (!config.hasMemoryInfo()) {
    return true;
  }
  double max_memory_available = phase_data.getRankMaxMemoryAvailable();
  return total_memory_usage <= max_memory_available;
}

/*static*/ bool WorkModelCalculator::checkMemoryFitUpdate(
  Configuration const& config,
  model::PhaseData const& phase_data,
  Clusterer const& clusterer,
  int global_max_clusters,
  double current_memory_usage,
  double current_max_working,
  double current_max_serialized,
  TaskClusterSummaryInfo to_add,
  TaskClusterSummaryInfo to_remove
) {
  if (!config.hasMemoryInfo()) {
    return true;
  }

  // New maxima from add/remove summaries
  double new_max_working    = current_max_working;
  double new_max_serialized = current_max_serialized;

  if (config.hasTaskWorkingMemoryInfo()) {
    new_max_working = std::max(
      (double)to_remove.max_object_working_bytes_outside,
      (double)to_add.max_object_working_bytes
    );
  }
  if (config.hasTaskSerializedMemoryInfo()) {
    new_max_serialized = std::max(
      (double)to_remove.max_object_serialized_bytes_outside,
      (double)to_add.max_object_serialized_bytes
    );
  }

  // Footprint delta
  double delta_footprint = 0.0;
  if (config.hasTaskFootprintMemoryInfo()) {
    delta_footprint = (double)to_add.cluster_footprint - (double)to_remove.cluster_footprint;
  }

  // Shared-block delta with remote add and local remove
  double delta_shared_blocks = 0.0;
  if (config.hasSharedBlockMemoryInfo()) {
    // Current refcounts from all tasks on this rank
    std::unordered_map<model::SharedBlockType, int> refcount_current;
    for (auto const& [id, task] : phase_data.getTasksMap()) {
      for (auto const& sb : task.getSharedBlocks()) {
        ++refcount_current[sb];
      }
    }

    // Determine tasks belonging to the local cluster being removed
    std::unordered_set<model::TaskType> remove_tasks;
    int local_cid = ClusterSummarizer::globalToLocalClusterID(to_remove.cluster_id, global_max_clusters);
    for (auto const& kv : clusterer.taskToCluster()) {
      if (kv.second == local_cid) {
        remove_tasks.insert(kv.first);
      }
    }

    // Count how many references the remove cluster makes to each shared block
    std::unordered_map<model::SharedBlockType, int> refcount_remove;
    for (auto tid : remove_tasks) {
      auto it = phase_data.getTasksMap().find(tid);
      if (it == phase_data.getTasksMap().end()) continue;
      for (auto const& sb : it->second.getSharedBlocks()) {
        ++refcount_remove[sb];
      }
    }

    // Helper to get size: prefer phase_data block info; fallback to to_add summary map
    auto get_shared_size_fn = [&](model::SharedBlockType sb) -> double {
      if (phase_data.hasSharedBlock(sb)) {
        return (double)phase_data.getSharedBlock(sb)->getSize();
      }
      auto it = to_add.shared_block_bytes_.find(sb);
      if (it != to_add.shared_block_bytes_.end()) {
        return (double)it->second;
      }
      return 0.0;
    };

    // Build union of shared blocks to inspect
    std::unordered_set<model::SharedBlockType> all_sbs;
    for (auto const& kv : refcount_current) all_sbs.insert(kv.first);
    for (auto const& kv : to_add.shared_block_bytes_) all_sbs.insert(kv.first);
    for (auto const& kv : refcount_remove) all_sbs.insert(kv.first);

    for (auto const& sb : all_sbs) {
      int cur = refcount_current.count(sb) ? refcount_current[sb] : 0;
      int rem = refcount_remove.count(sb) ? refcount_remove[sb] : 0;
      bool cur_present = (cur > 0);
      bool remove_would_zero = (cur > 0) && (cur - rem <= 0);

      bool add_will_use = (to_add.shared_block_bytes_.find(sb) != to_add.shared_block_bytes_.end());

      bool final_present = (cur_present && !remove_would_zero) || add_will_use;

      if (cur_present && !final_present) {
        // Block was present but will not be after update
        delta_shared_blocks -= get_shared_size_fn(sb);
      } else if (!cur_present && final_present) {
        // Block was not present but will be after update
        delta_shared_blocks += get_shared_size_fn(sb);
      }
      // Otherwise, no delta.
    }
  }

  // Update memory usage
  double updated_usage = current_memory_usage;
  if (config.hasTaskWorkingMemoryInfo()) {
    updated_usage += (new_max_working - current_max_working);
  }
  if (config.hasTaskSerializedMemoryInfo()) {
    updated_usage += (new_max_serialized - current_max_serialized);
  }
  updated_usage += delta_footprint;
  updated_usage += delta_shared_blocks;

  double max_memory_available = phase_data.getRankMaxMemoryAvailable();
  return updated_usage <= max_memory_available;
}

} /* end namespace vt_lb::algo::temperedlb */