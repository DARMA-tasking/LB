/*
//@HEADER
// *****************************************************************************
//
//                            full_graph_visualizer.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_FULL_GRAPH_VISUALIZER_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_FULL_GRAPH_VISUALIZER_H

#include <vt-lb/config/cmake_config.h>

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Communication.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <unordered_set>
#include <fstream>
// Add headers used in DOT building
#include <array>
#include <sstream>
#include <iomanip>
#include <optional>

namespace vt_lb::algo::temperedlb {

template <typename CommT>
struct FullGraphVisualizer {
  using ThisType = FullGraphVisualizer<CommT>;
  using HandleType = typename CommT::template HandleType<ThisType>;

  FullGraphVisualizer(CommT& comm, model::PhaseData const& phase_data, Clusterer const* clusterer, int global_max_clusters, std::string const& filename)
    : comm_(comm.clone()),
      phase_data_(phase_data),
      clusterer_(clusterer),
      global_max_clusters_(global_max_clusters),
      filename_(filename)
    {
      handle_ = comm_.template registerInstanceCollective<ThisType>(this);
    }

  // Handler for receiving aggregated data from children
  void receiveAggregated(
    std::unordered_map<model::RankType, model::PhaseData> incoming_rank_phases,
    std::unordered_map<model::RankType, std::unordered_map<model::TaskType,int>> incoming_rank_clusters,
    model::RankType from_rank
  ) {
    // Merge per-rank PhaseData from child subtree
    for (auto& [r, pd] : incoming_rank_phases) {
      phases_by_rank_[r] = std::move(pd);
    }
    // Merge per-rank clusters from child subtree
    for (auto& [r, cmap] : incoming_rank_clusters) {
      clusters_by_rank_[r] = std::move(cmap);
    }
    ++received_children_;
    VT_LB_LOG(Visualizer, verbose, "[viz] receiveAggregated from child rank={} (received={}/{})\n",
      static_cast<int>(from_rank), received_children_, expected_children_);
  }

  void run() {
    const int my_rank = comm_.getRank();
    const int n_ranks = comm_.numRanks();

    // Initialize local cluster mapping from the provided clusterer, if any
    local_clusters_.clear();
    if (clusterer_) {
      for (auto const& [tid, local_cid] : clusterer_->taskToCluster()) {
        int global_cid = ClusterSummarizerUtil::localToGlobalClusterID(local_cid, my_rank, global_max_clusters_);
        local_clusters_[tid] = global_cid;
      }
      VT_LB_LOG(Visualizer, normal, "[viz] init local_clusters task-maps={} unique-clusters={} (globalized, gmax={})\n",
        local_clusters_.size(), countClusters(local_clusters_), global_max_clusters_);
    } else {
      VT_LB_LOG(Visualizer, normal, "[viz] clusterer_=nullptr\n");
    }

    // Binary tree topology
    const int parent = (my_rank == 0) ? -1 : (my_rank / 2);
    std::vector<int> children;
    for (int r = 0; r < n_ranks; ++r) {
      if (r != my_rank && (r / 2) == my_rank) {
        children.push_back(r);
      }
    }
    expected_children_ = static_cast<int>(children.size());
    received_children_ = 0;
    VT_LB_LOG(Visualizer, normal, "[viz] parent={} children=[{}] expected_children={}\n",
      parent,
      [&children](){
        std::string s;
        for (std::size_t i = 0; i < children.size(); ++i) {
          s += std::to_string(children[i]);
          if (i + 1 < children.size()) s += ",";
        }
        return s;
      }(),
      expected_children_);

    // Wait for children
    while (received_children_ < expected_children_) {
      if (!comm_.poll()) break;
    }
    VT_LB_LOG(Visualizer, normal, "[viz] proceed to merge (received={}/{})\n",
      received_children_, expected_children_);

    // Build per-rank accumulators (DO NOT flatten)
    std::unordered_map<model::RankType, model::PhaseData> merged_rank_phases = phases_by_rank_;
    std::unordered_map<model::RankType, std::unordered_map<model::TaskType,int>> merged_rank_clusters = clusters_by_rank_;

    // Insert this rank's local data
    merged_rank_phases[static_cast<model::RankType>(my_rank)] = phase_data_;
    merged_rank_clusters[static_cast<model::RankType>(my_rank)] = local_clusters_;

    // Debug: counts
    std::size_t total_tasks = 0, total_edges = 0, total_maps = 0;
    std::unordered_set<int> uniq_cids;
    for (auto const& [r, pd] : merged_rank_phases) {
      total_tasks += pd.getTasksMap().size();
      total_edges += pd.getCommunications().size();
    }
    for (auto const& [r, cmap] : merged_rank_clusters) {
      total_maps += cmap.size();
      for (auto const& [tid, cid] : cmap) uniq_cids.insert(cid);
      VT_LB_LOG(Visualizer, normal, "[viz] merged child rank={} task-maps={} unique-clusters={}\n",
        static_cast<int>(r), cmap.size(), countClusters(cmap));
    }
    VT_LB_LOG(Visualizer, normal, "[viz] merged tasks={} edges={} task-maps={} unique-clusters={}\n",
      total_tasks, total_edges, total_maps, uniq_cids.size());

    if (parent >= 0) {
      VT_LB_LOG(Visualizer, normal, "[viz] send to parent={}\n", parent);
      handle_[parent].template send<&ThisType::receiveAggregated>(
        merged_rank_phases, merged_rank_clusters, static_cast<model::RankType>(my_rank)
      );
    } else {
      // Root: store final per-rank data
      final_rank_phases_ = std::move(merged_rank_phases);
      final_rank_clusters_ = std::move(merged_rank_clusters);
      VT_LB_LOG(Visualizer, normal, "[viz] (root) final ranks={} total-cluster-maps={}\n",
        final_rank_phases_.size(), final_rank_clusters_.size());

      // Build and write DOT
      std::string dot = buildFullGraphDOT(final_rank_phases_, final_rank_clusters_);
      std::ofstream ofs(filename_ + ".dot");
      if (ofs) {
        ofs << dot;
        ofs.close();
        VT_LB_LOG(Visualizer, normal, "[viz] (root) wrote DOT to '{}.dot'\n", filename_);
      } else {
        VT_LB_LOG(Visualizer, normal, "[viz] (root) failed to open '{}.dot' for writing\n", filename_);
      }
    }
  }

private:
  // Merge a child's PhaseData into the accumulator
  static void mergePhaseData(model::PhaseData& acc, model::PhaseData const& child) {
    // Tasks: insert/overwrite
    for (auto const& [tid, t] : child.getTasksMap()) {
      // If duplicate task ids appear across ranks, last writer wins; adjust to desired policy.
      acc.addTask(t);
    }
    // Communications: append
    for (auto const& e : child.getCommunications()) {
      acc.addCommunication(e);
    }
    // Shared blocks: insert/overwrite
    for (auto const& [sbid, sb] : child.getSharedBlocksMap()) {
      acc.addSharedBlock(sb);
    }
    // Rank-level metadata: accumulate footprint and max available conservatively
    acc.setRankFootprintBytes(acc.getRankFootprintBytes() + child.getRankFootprintBytes());
    acc.setRankMaxMemoryAvailable(std::max(acc.getRankMaxMemoryAvailable(), child.getRankMaxMemoryAvailable()));
  }

  // Count unique cluster IDs in a task->cluster map
  static int countClusters(std::unordered_map<model::TaskType,int> const& task_to_cluster) {
    std::unordered_set<int> uniq;
    uniq.reserve(task_to_cluster.size());
    for (auto const& kv : task_to_cluster) {
      uniq.insert(kv.second);
    }
    return static_cast<int>(uniq.size());
  }

  // Build a multi-rank, multi-cluster DOT from per-rank PhaseData and per-rank clusters
  std::string buildFullGraphDOT(
    std::unordered_map<model::RankType, model::PhaseData> const& phases_by_rank_all,
    std::unordered_map<model::RankType, std::unordered_map<model::TaskType,int>> const& rank_task_to_global_cluster
  ) const {
    using TaskType = model::TaskType;
    using RankType = model::RankType;

    // Helper: find cluster id by (rank, task)
    auto findClusterId = [&rank_task_to_global_cluster](RankType r, TaskType tid) -> std::optional<int> {
      auto rc_it = rank_task_to_global_cluster.find(r);
      if (rc_it == rank_task_to_global_cluster.end()) return std::nullopt;
      auto const& cmap = rc_it->second;
      auto it = cmap.find(tid);
      if (it == cmap.end()) return std::nullopt;
      return it->second;
    };
    // Helper: globally unique DOT node id
    auto makeNodeId = [](TaskType tid, RankType rank) {
      return std::string("r") + std::to_string(static_cast<int>(rank)) + "_t" + std::to_string(static_cast<int>(tid));
    };

    static std::array<const char*, 12> palette = {
      "#1f77b4","#ff7f0e","#2ca02c","#d62728",
      "#9467bd","#8c564b","#e377c2","#7f7f7f",
      "#bcbd22","#17becf","#393b79","#637939"
    };

    // Build per-rank membership of tasks and a task->rank index
    std::unordered_map<RankType, std::vector<TaskType>> tasks_by_rank;
    std::unordered_map<TaskType, RankType> task_to_rank;
    for (auto const& [rank, pd] : phases_by_rank_all) {
      for (auto const& [tid, t] : pd.getTasksMap()) {
        tasks_by_rank[rank].push_back(tid);
        task_to_rank[tid] = rank;
      }
    }

    // Build per-rank, per-global-cluster membership
    std::unordered_map<RankType, std::unordered_map<int, std::vector<TaskType>>> rank_cluster_members;
    std::unordered_map<RankType, std::vector<TaskType>> rank_unclustered;
    for (auto const& [rank, tids] : tasks_by_rank) {
      for (auto tid : tids) {
        auto cid_opt = findClusterId(rank, tid);
        if (!cid_opt.has_value()) {
          rank_unclustered[rank].push_back(tid);
        } else {
          rank_cluster_members[rank][cid_opt.value()].push_back(tid);
        }
      }
    }

    // Compute cluster-level stats: total load, intra bytes, boundary bytes per rank+cluster
    struct ClusterBytes {
      unsigned long long intra = 0ULL;
      unsigned long long boundary = 0ULL;
      double load = 0.0;
    };
    std::unordered_map<RankType, std::unordered_map<int, ClusterBytes>> cluster_stats;

    // Precompute task load per rank
    std::unordered_map<RankType, std::unordered_map<TaskType, double>> task_load;
    for (auto const& [rank, pd] : phases_by_rank_all) {
      for (auto const& [tid, t] : pd.getTasksMap()) {
        task_load[rank][tid] = t.getLoad();
      }
    }
    // Accumulate cluster load per (rank, cluster)
    for (auto const& [rank, clmap] : rank_cluster_members) {
      for (auto const& [gcid, members] : clmap) {
        double sum_load = 0.0;
        for (auto tid : members) {
          auto itl = task_load[rank].find(tid);
          if (itl != task_load[rank].end()) sum_load += itl->second;
        }
        cluster_stats[rank][gcid].load = sum_load;
      }
    }
    // Accumulate intra/boundary bytes across all ranks' communications
    for (auto const& [rank_src, pd] : phases_by_rank_all) {
      for (auto const& e : pd.getCommunications()) {
        TaskType f = e.getFrom();
        TaskType t = e.getTo();
        auto rf_it = task_to_rank.find(f);
        auto rt_it = task_to_rank.find(t);
        if (rf_it == task_to_rank.end() || rt_it == task_to_rank.end()) continue;

        RankType rf = rf_it->second;
        RankType rt = rt_it->second;

        unsigned long long vol = static_cast<unsigned long long>(std::llround(e.getVolume()));
        auto cf_opt = findClusterId(rf, f);
        auto ct_opt = findClusterId(rt, t);
        bool cf_ok = cf_opt.has_value();
        bool ct_ok = ct_opt.has_value();

        if (cf_ok && ct_ok && cf_opt.value() == ct_opt.value() && rf == rt) {
          cluster_stats[rf][cf_opt.value()].intra += vol;
        } else {
          if (cf_ok) cluster_stats[rf][cf_opt.value()].boundary += vol;
          if (ct_ok) cluster_stats[rt][ct_opt.value()].boundary += vol;
        }
      }
    }

    // Begin DOT
    std::ostringstream out;
    out << "digraph FullTaskGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  compound=true;\n";
    out << "  node [shape=ellipse, fontname=\"Helvetica\"];\n";

    // Emit rank and cluster subgraphs and nodes
    for (auto const& [rank, tids] : tasks_by_rank) {
      out << "  subgraph cluster_rank_" << rank << " {\n";
      out << "    label=\"rank " << rank << "\";\n";
      out << "    color=\"black\";\n";

      auto rc_it = rank_cluster_members.find(rank);
      if (rc_it != rank_cluster_members.end()) {
        for (auto const& [gcid, members] : rc_it->second) {
          auto color = palette[static_cast<std::size_t>(gcid) % palette.size()];
          auto const& stats = cluster_stats[rank][gcid];
          out << "    subgraph cluster_rank_" << rank << "_cluster_" << gcid << " {\n";
          out << "      label=\"cluster " << gcid
              << "\\nload=" << std::fixed << std::setprecision(2) << stats.load
              << "\\nbytes_intra=" << stats.intra
              << "\\nbytes_boundary=" << stats.boundary << "\";\n";
          out << "      color=\"" << color << "\";\n";
          for (auto tid : members) {
            auto const& pd = phases_by_rank_all.at(rank);
            auto const* task = pd.getTask(tid);
            if (!task) continue;
            auto node_id = makeNodeId(tid, rank);
            out << "      \"" << node_id << "\" [style=filled, fillcolor=\"" << color << "\", label=\""
                << tid << "\\nL=" << std::fixed << std::setprecision(1) << task->getLoad()
                << "\"];\n";
          }
          out << "    }\n";
        }
      }

      auto ru_it = rank_unclustered.find(rank);
      if (ru_it != rank_unclustered.end()) {
        for (auto tid : ru_it->second) {
          auto const& pd = phases_by_rank_all.at(rank);
          auto const* task = pd.getTask(tid);
          if (!task) continue;
          auto node_id = makeNodeId(tid, rank);
          out << "    \"" << node_id << "\" [label=\"" << tid
              << "\\nL=" << std::fixed << std::setprecision(1) << task->getLoad()
              << "\"];\n";
        }
      }

      out << "  }\n";
    }

    // Draw edges for communications
    for (auto const& [rank_src, pd] : phases_by_rank_all) {
      for (auto const& e : pd.getCommunications()) {
        TaskType f = e.getFrom();
        TaskType t = e.getTo();
        auto rf_it = task_to_rank.find(f);
        auto rt_it = task_to_rank.find(t);
        if (rf_it == task_to_rank.end() || rt_it == task_to_rank.end()) continue;

        RankType rf = rf_it->second;
        RankType rt = rt_it->second;

        auto from_node = makeNodeId(f, rf);
        auto to_node   = makeNodeId(t, rt);

        auto cf_opt = findClusterId(rf, f);
        auto ct_opt = findClusterId(rt, t);
        int cf = cf_opt.value_or(-1);
        int ct = ct_opt.value_or(-1);

        out << "  \"" << from_node << "\" -> \"" << to_node << "\" [label=\"" << std::fixed << std::setprecision(0)
            << e.getVolume() << "\"";

        if (cf != -1 && ct != -1 && cf != ct) {
          out << ", penwidth=2";
        }
        if (rf != rt) {
          out << ", color=red";
        } else if (cf != -1 && ct != -1 && cf != ct) {
          out << ", color=orange";
        }
        out << "];\n";
      }
    }

    out << "}\n";
    return out.str();
  }

private:
  CommT comm_;
  model::PhaseData phase_data_;
  Clusterer const* clusterer_ = nullptr;
  int global_max_clusters_ = 0;
  std::string filename_;
  HandleType handle_;

  // Accumulators during reduction (per-rank)
  std::unordered_map<model::RankType, model::PhaseData> phases_by_rank_;
  std::unordered_map<model::RankType, std::unordered_map<model::TaskType,int>> clusters_by_rank_;
  int expected_children_ = 0;
  int received_children_ = 0;

  // Final merged output at root (per-rank)
  std::unordered_map<model::RankType, model::PhaseData> final_rank_phases_;
  std::unordered_map<model::RankType, std::unordered_map<model::TaskType,int>> final_rank_clusters_;

  // Local cluster mapping for this rank
  std::unordered_map<model::TaskType,int> local_clusters_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_FULL_GRAPH_VISUALIZER_H*/