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
#include <vt-lb/util/assert.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>
#include <vt-lb/algo/temperedlb/transfer_util.h>

#include <cassert>
#include <unordered_set>
#include <cmath>

namespace vt_lb::algo::temperedlb {

/*static*/ WorkBreakdown WorkModelCalculator::computeWorkBreakdown(
  model::PhaseData const& phase_data,
  Configuration const& config
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
    if (e.getFromRank() == rank || e.getToRank() == rank) {
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
  }

  // Shared-memory communication term
  for (auto const& sb : shared_blocks_here) {
    vt_lb_assert(phase_data.hasSharedBlock(sb), "Shared block information missing");
    auto info = phase_data.getSharedBlock(sb);
    if (info->getHome() != rank) {
      breakdown.shared_mem_comm += info->getSize();
    }
  }

  if (config.hasMemoryInfo()) {
    breakdown.memory_breakdown = computeMemoryUsage(config, phase_data);
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
      vt_lb_assert(phase_data.hasSharedBlock(sb), "Shared block information missing");
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

/*static*/ WorkBreakdown WorkModelCalculator::computeWorkUpdateSummary(
  RankClusterInfo rank_cluster_info,
  TaskClusterSummaryInfo to_add,
  TaskClusterSummaryInfo to_remove
) {
  WorkBreakdown new_bd = rank_cluster_info.rank_breakdown;

  // Build sets of local clusters BEFORE and AFTER (global IDs)
  std::unordered_set<int> local_before;
  for (const auto& kv : rank_cluster_info.cluster_summaries) {
    local_before.insert(kv.second.cluster_id);
  }
  auto local_after = local_before;
  if (to_add.cluster_id != -1) {
    local_after.insert(to_add.cluster_id);
  }
  if (to_remove.cluster_id != -1) {
    local_after.erase(to_remove.cluster_id);
  }

  // Adjust compute and intra bytes for add/remove
  if (to_remove.cluster_id != -1) {
    new_bd.compute -= to_remove.cluster_load;
    new_bd.intra_node_send_comm -= to_remove.cluster_intra_send_bytes;
    new_bd.intra_node_recv_comm -= to_remove.cluster_intra_recv_bytes;
  }
  if (to_add.cluster_id != -1) {
    new_bd.compute += to_add.cluster_load;
    new_bd.intra_node_send_comm += to_add.cluster_intra_send_bytes;
    new_bd.intra_node_recv_comm += to_add.cluster_intra_recv_bytes;
  }

  // Deduplicate inter-edges by unordered pair of global cluster IDs
  auto encode_pair = [](int a, int b) -> long long {
    if (a > b) std::swap(a, b);
    return (static_cast<long long>(a) << 32) | static_cast<unsigned long long>(b);
  };
  std::unordered_set<long long> seen;

  auto process_edges_reclass = [&](std::vector<model::ClusterEdge> const& edges) {
    for (auto const& e : edges) {
      int g_from = e.getFromCluster();
      int g_to   = e.getToCluster();
      double vol = e.getVolume();

      long long key = encode_pair(g_from, g_to);
      if (!seen.insert(key).second) continue;

      int init_locals =
        (local_before.count(g_from) ? 1 : 0) +
        (local_before.count(g_to)   ? 1 : 0);
      int final_locals =
        (local_after.count(g_from) ? 1 : 0) +
        (local_after.count(g_to)   ? 1 : 0);

      // intra: 2 local; inter: 1 local; none: 0 local
      if (init_locals == 2 && final_locals < 2) {
        // intra -> inter or none
        new_bd.intra_node_send_comm -= vol;
        new_bd.intra_node_recv_comm -= vol;
        if (final_locals == 1) {
          // treat inter as undirected, add to both send/recv
          new_bd.inter_node_send_comm += vol;
          new_bd.inter_node_recv_comm += vol;
        }
      } else if (init_locals == 1 && final_locals == 2) {
        // inter -> intra
        new_bd.inter_node_send_comm -= vol;
        new_bd.inter_node_recv_comm -= vol;
        new_bd.intra_node_send_comm += vol;
        new_bd.intra_node_recv_comm += vol;
      } else if (init_locals == 0 && final_locals == 1) {
        // none -> inter (newly relevant to this rank)
        new_bd.inter_node_send_comm += vol;
        new_bd.inter_node_recv_comm += vol;
      } else if (init_locals == 1 && final_locals == 0) {
        // inter -> none (no longer relevant)
        new_bd.inter_node_send_comm -= vol;
        new_bd.inter_node_recv_comm -= vol;
      }
      // 2->2, 0->0, 1->1: no change
    }
  };

  if (to_add.cluster_id != -1) {
    process_edges_reclass(to_add.inter_edges_);
  }
  if (to_remove.cluster_id != -1) {
    process_edges_reclass(to_remove.inter_edges_);
  }

  auto const& homed_blocks = rank_cluster_info.shared_blocks_homed;

  // Gather present-before and present-after maps
  std::unordered_map<model::SharedBlockType, model::BytesType> present_before;
  for (const auto& kv : rank_cluster_info.cluster_summaries) {
    for (const auto& sb_kv : kv.second.shared_block_bytes_) {
      present_before.insert(sb_kv);
    }
  }
  std::unordered_map<model::SharedBlockType, model::BytesType> present_after = present_before;
  if (to_remove.cluster_id != -1) {
    for (const auto& sb_kv : to_remove.shared_block_bytes_) {
      present_after.erase(sb_kv.first);
    }
  }
  if (to_add.cluster_id != -1) {
    for (const auto& sb_kv : to_add.shared_block_bytes_) {
      present_after.insert(sb_kv);
    }
  }

  // Union of all blocks to find size
  std::unordered_map<model::SharedBlockType, model::BytesType> all_sbs = present_before;
  all_sbs.insert(to_add.shared_block_bytes_.begin(), to_add.shared_block_bytes_.end());
  all_sbs.insert(to_remove.shared_block_bytes_.begin(), to_remove.shared_block_bytes_.end());

  auto size_of = [&](model::SharedBlockType sb) -> double {
    vt_lb_assert(all_sbs.find(sb) != all_sbs.end(), "Shared block size missing");
    return all_sbs.find(sb)->second;
  };

  for (auto const& sb : all_sbs) {
    bool before = present_before.contains(sb.first);
    bool removed_here = (to_remove.cluster_id != -1) &&
                        (to_remove.shared_block_bytes_.contains(sb.first));
    bool added_here = (to_add.cluster_id != -1) &&
                      (to_add.shared_block_bytes_.contains(sb.first));
    bool after = (before && !removed_here) || added_here;

    bool is_homed_here = homed_blocks.contains(sb.first);
    if (is_homed_here) {
      continue;
    }

    if (before && !after) {
      new_bd.shared_mem_comm -= size_of(sb.first);
    } else if (!before && after) {
      new_bd.shared_mem_comm += size_of(sb.first);
    }
  }

  new_bd.compute              = std::max(0.0, new_bd.compute);
  new_bd.inter_node_recv_comm = std::max(0.0, new_bd.inter_node_recv_comm);
  new_bd.inter_node_send_comm = std::max(0.0, new_bd.inter_node_send_comm);
  new_bd.intra_node_recv_comm = std::max(0.0, new_bd.intra_node_recv_comm);
  new_bd.intra_node_send_comm = std::max(0.0, new_bd.intra_node_send_comm);
  new_bd.shared_mem_comm      = std::max(0.0, new_bd.shared_mem_comm);

  return new_bd;
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

/*static*/ MemoryBreakdown WorkModelCalculator::computeMemoryUsage(
  Configuration const& config,
  model::PhaseData const& phase_data
) {
  if (!config.hasMemoryInfo()) {
    return MemoryBreakdown{0.0, 0.0, 0.0};
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
    vt_lb_assert(phase_data.hasSharedBlock(sb), "Shared block information missing");
    auto info = phase_data.getSharedBlock(sb);
    shared_blocks_bytes_ += info->getSize();
  }

  double const total_usage =
    phase_data.getRankFootprintBytes() +
    task_footprint_bytes_ +
    task_max_working_bytes_ +
    task_max_serialized_bytes_ +
    shared_blocks_bytes_;

  return MemoryBreakdown{
    total_usage,
    task_max_working_bytes_,
    task_max_serialized_bytes_
  };
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
  RankClusterInfo rank_cluster_info,
  TaskClusterSummaryInfo to_add,
  TaskClusterSummaryInfo to_remove,
  double rank_available_memory
) {
  if (!config.hasMemoryInfo()) {
    return true;
  }

  MemoryBreakdown const& cur = rank_cluster_info.rank_breakdown.memory_breakdown;
  double updated_usage = cur.current_memory_usage;

  double new_max_working = cur.current_max_task_working_bytes;
  double new_max_serialized = cur.current_max_task_serialized_bytes;

  if (config.hasTaskWorkingMemoryInfo()) {
    new_max_working = std::max(
      (double)to_remove.max_object_working_bytes_outside,
      (double)to_add.max_object_working_bytes
    );
    updated_usage += (new_max_working - cur.current_max_task_working_bytes);
  }
  if (config.hasTaskSerializedMemoryInfo()) {
    new_max_serialized = std::max(
      (double)to_remove.max_object_serialized_bytes_outside,
      (double)to_add.max_object_serialized_bytes
    );
    updated_usage += (new_max_serialized - cur.current_max_task_serialized_bytes);
  }

  if (config.hasTaskFootprintMemoryInfo()) {
    updated_usage += (double)to_add.cluster_footprint - (double)to_remove.cluster_footprint;
  }

  // Shared-block delta
  if (config.hasSharedBlockMemoryInfo()) {
    // Track current presence
    std::unordered_map<model::SharedBlockType, model::BytesType> present_before;
    for (auto const& kv : rank_cluster_info.cluster_summaries) {
      TaskClusterSummaryInfo const& sum = kv.second;
      for (auto const& sb_kv : sum.shared_block_bytes_) {
        present_before.insert(sb_kv);
      }
    }

    // Union of candidates to check
    std::unordered_map<model::SharedBlockType, model::BytesType> all_sbs = present_before;
    all_sbs.insert(to_add.shared_block_bytes_.begin(), to_add.shared_block_bytes_.end());
    all_sbs.insert(to_remove.shared_block_bytes_.begin(), to_remove.shared_block_bytes_.end());

    auto size_of = [&](model::SharedBlockType sb) -> double {
      vt_lb_assert(all_sbs.find(sb) != all_sbs.end(), "Shared block size missing");
      return all_sbs.find(sb)->second;
    };

    for (auto const& sb : all_sbs) {
      bool before = present_before.contains(sb.first);
      bool removed_here = (to_remove.cluster_id != -1) &&
                          (to_remove.shared_block_bytes_.contains(sb.first));
      bool added_here = (to_add.cluster_id != -1) &&
                        (to_add.shared_block_bytes_.contains(sb.first));
      bool after = (before && !removed_here) || added_here;

      if (before && !after) {
        updated_usage -= size_of(sb.first);
      } else if (!before && after) {
        updated_usage += size_of(sb.first);
      }
    }
  }

  return updated_usage <= rank_available_memory;
}

} /* end namespace vt_lb::algo::temperedlb */