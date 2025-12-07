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
    model::PhaseData incoming_pd,
    std::unordered_map<model::TaskType,int> incoming_clusters,
    model::RankType from_rank
  ) {
    // Accumulate per-rank PhaseData and cluster map
    phases_by_rank_[from_rank] = std::move(incoming_pd);
    clusters_by_rank_[from_rank] = std::move(incoming_clusters);
    ++received_children_;
    // Debug: child arrival
    VT_LB_LOG(Visualizer, verbose, "[viz] receiveAggregated from child rank={} (received={}/{})\n",
    static_cast<int>(from_rank), received_children_, expected_children_);
  }

  void run() {
    const int my_rank = comm_.getRank();
    const int n_ranks = comm_.numRanks();

    // Initialize local cluster mapping from the provided clusterer, if any
    local_clusters_.clear();
    if (clusterer_) {
      // Convert local cluster IDs to global using global_max_clusters_
      for (auto const& [tid, local_cid] : clusterer_->taskToCluster()) {
        int global_cid = ClusterSummarizer::localToGlobalClusterID(local_cid, my_rank, global_max_clusters_);
        local_clusters_[tid] = global_cid;
      }
      // Debug: local cluster count and conversion info
      VT_LB_LOG(
        Visualizer, normal, "[viz] init local_clusters task-maps={} unique-clusters={} (globalized, gmax={})\n",
        local_clusters_.size(), countClusters(local_clusters_), global_max_clusters_
      );
    } else {
      VT_LB_LOG(Visualizer, normal, "[viz] clusterer_=nullptr\n");
    }

    // Compute parent and children for a simple binary tree: parent = r/2
    const int parent = (my_rank == 0) ? -1 : (my_rank / 2);
    std::vector<int> children;
    for (int r = 0; r < n_ranks; ++r) {
      if (r != my_rank && (r / 2) == my_rank) {
        children.push_back(r);
      }
    }
    expected_children_ = static_cast<int>(children.size());
    received_children_ = 0;
    // Debug: topology
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

    // Wait for all children to send their data up the tree
    while (received_children_ < expected_children_) {
      if (!comm_.poll()) break; // progress MPI and handlers
    }
    VT_LB_LOG(Visualizer, normal, "[viz] proceed to merge (received={}/{})\n",
              received_children_, expected_children_);

    // Merge children data into a local accumulator
    model::PhaseData merged = phase_data_; // start with local rank data
    auto merged_clusters = local_clusters_; // local per-task cluster map (empty if none)

    // Debug: local counts pre-merge
    VT_LB_LOG(Visualizer, verbose, "[viz] local tasks={} edges={} shared_blocks={}\n",
              merged.getTasksMap().size(),
              merged.getCommunications().size(),
              merged.getSharedBlocksMap().size());

    for (auto const& [rank, pd] : phases_by_rank_) {
      // Debug: child payload sizes
      VT_LB_LOG(Visualizer, verbose, "[viz] merge child rank={} tasks={} edges={} shared_blocks={}\n",
                static_cast<int>(rank),
                pd.getTasksMap().size(),
                pd.getCommunications().size(),
                pd.getSharedBlocksMap().size());
      mergePhaseData(merged, pd);
    }
    for (auto const& [rank, cmap] : clusters_by_rank_) {
      // Assume children already sent globalized IDs
      for (auto const& [task, cid] : cmap) {
        merged_clusters[task] = cid;
      }
    }

    // Debug: post-merge counts
    VT_LB_LOG(Visualizer, normal, "[viz] merged tasks={} edges={} shared_blocks={} task-maps={} unique-clusters={}\n",
              merged.getTasksMap().size(),
              merged.getCommunications().size(),
              merged.getSharedBlocksMap().size(),
              merged_clusters.size(),
              countClusters(merged_clusters));

    if (parent >= 0) {
      // Forward merged accumulation to parent
      VT_LB_LOG(Visualizer, normal, "[viz] send to parent={}\n", parent);
      handle_[parent].template send<&ThisType::receiveAggregated>(
        merged, merged_clusters, static_cast<model::RankType>(my_rank)
      );
    } else {
      // Root: we now have the complete graph and cluster map across all ranks
      final_merged_phase_ = std::move(merged);
      final_merged_clusters_ = std::move(merged_clusters);
      VT_LB_LOG(Visualizer, normal, "[viz] (root) final merged tasks={} edges={} shared_blocks={} task-maps={} unique-clusters={}\n",
                final_merged_phase_.getTasksMap().size(),
                final_merged_phase_.getCommunications().size(),
                final_merged_phase_.getSharedBlocksMap().size(),
                final_merged_clusters_.size(),
                countClusters(final_merged_clusters_));
      // Build and write the full DOT visualization across ranks/clusters
      std::string dot = buildFullGraphDOT(final_merged_phase_, final_merged_clusters_);
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

  // Build a multi-rank, multi-cluster DOT:
  // - Top-level graph contains subgraph per rank
  // - Each rank subgraph contains nested subgraphs per global cluster belonging to that rank
  // - Tasks that are unclustered for that rank appear directly under the rank subgraph
  // - All communications are drawn across tasks; inter-cluster edges are bold; inter-rank edges are colored
  std::string buildFullGraphDOT(
    model::PhaseData const& pd,
    std::unordered_map<model::TaskType,int> const& task_to_global_cluster
  ) const {
    using TaskType = model::TaskType;
    using RankType = model::RankType;

    // Palette for cluster colors (cycled)
    static std::array<const char*, 12> palette = {
      "#1f77b4","#ff7f0e","#2ca02c","#d62728",
      "#9467bd","#8c564b","#e377c2","#7f7f7f",
      "#bcbd22","#17becf","#393b79","#637939"
    };

    // Build per-rank membership of tasks
    std::unordered_map<RankType, std::vector<TaskType>> tasks_by_rank;
    for (auto const& [tid, t] : pd.getTasksMap()) {
      tasks_by_rank[t.getCurrent()].push_back(tid);
    }

    // Build per-rank, per-global-cluster membership
    std::unordered_map<RankType, std::unordered_map<int, std::vector<TaskType>>> rank_cluster_members;
    std::unordered_map<RankType, std::vector<TaskType>> rank_unclustered;
    for (auto const& [tid, t] : pd.getTasksMap()) {
      auto r = t.getCurrent();
      auto it = task_to_global_cluster.find(tid);
      if (it == task_to_global_cluster.end()) {
        rank_unclustered[r].push_back(tid);
      } else {
        int gcid = it->second;
        // Ensure cluster is attributed to the cluster's owning rank; tasks might be on different ranks
        // For visualization, nest under task's current rank to show locality, but keep GCID labeling.
        rank_cluster_members[r][gcid].push_back(tid);
      }
    }

    // Compute cluster-level stats: total load, intra bytes, boundary bytes per rank+cluster
    // Build quick membership lookup for intra/boundary computation
    std::unordered_map<TaskType,int> task_gcid = task_to_global_cluster; // copy for lookup
    // Sum per (rank, global cluster id)
    struct ClusterBytes {
      unsigned long long intra = 0ULL;
      unsigned long long boundary = 0ULL;
      double load = 0.0;
    };
    std::unordered_map<RankType, std::unordered_map<int, ClusterBytes>> cluster_stats;

    // Precompute task load
    std::unordered_map<TaskType, double> task_load;
    for (auto const& [tid, t] : pd.getTasksMap()) {
      task_load[tid] = t.getLoad();
    }
    // Accumulate cluster load per rank cluster
    for (auto const& [rank, clmap] : rank_cluster_members) {
      for (auto const& [gcid, members] : clmap) {
        double sum_load = 0.0;
        for (auto tid : members) {
          auto itl = task_load.find(tid);
          if (itl != task_load.end()) sum_load += itl->second;
        }
        cluster_stats[rank][gcid].load = sum_load;
      }
    }
    // Accumulate intra/boundary bytes
    for (auto const& e : pd.getCommunications()) {
      TaskType f = e.getFrom();
      TaskType t = e.getTo();
      if (!pd.hasTask(f) || !pd.hasTask(t)) continue;
      unsigned long long vol = static_cast<unsigned long long>(std::llround(e.getVolume()));

      auto cf_it = task_gcid.find(f);
      auto ct_it = task_gcid.find(t);
      bool cf_ok = cf_it != task_gcid.end();
      bool ct_ok = ct_it != task_gcid.end();

      RankType rf = pd.getTask(f)->getCurrent();
      RankType rt = pd.getTask(t)->getCurrent();

      if (cf_ok && ct_ok && cf_it->second == ct_it->second && rf == rt) {
        // Intra-cluster intra-rank
        cluster_stats[rf][cf_it->second].intra += vol;
      } else {
        // Boundary/Inter-rank: attribute volume to clusters if known
        if (cf_ok) cluster_stats[rf][cf_it->second].boundary += vol;
        if (ct_ok) cluster_stats[rt][ct_it->second].boundary += vol;
      }
    }

    // Begin DOT
    std::ostringstream out;
    out << "digraph FullTaskGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  compound=true;\n";
    out << "  node [shape=ellipse, fontname=\"Helvetica\"];\n";

    // Emit rank subgraphs
    for (auto const& [rank, rank_tasks] : tasks_by_rank) {
      out << "  subgraph cluster_rank_" << rank << " {\n";
      out << "    label=\"rank " << rank << "\";\n";
      out << "    color=\"black\";\n";

      // Emit cluster subgraphs under this rank
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
            auto const* task = pd.getTask(tid);
            if (!task) continue;
            out << "      \"" << tid << "\" [style=filled, fillcolor=\"" << color << "\", label=\""
                << tid << "\\nL=" << std::fixed << std::setprecision(1) << task->getLoad()
                << "\"];\n";
          }
          out << "    }\n";
        }
      }

      // Emit unclustered tasks under rank
      auto ru_it = rank_unclustered.find(rank);
      if (ru_it != rank_unclustered.end()) {
        for (auto tid : ru_it->second) {
          auto const* task = pd.getTask(tid);
          if (!task) continue;
          out << "    \"" << tid << "\" [label=\"" << tid
              << "\\nL=" << std::fixed << std::setprecision(1) << task->getLoad()
              << "\"];\n";
        }
      }

      out << "  }\n";
    }

    // Draw edges for communications
    for (auto const& e : pd.getCommunications()) {
      TaskType f = e.getFrom();
      TaskType t = e.getTo();
      if (!pd.hasTask(f) || !pd.hasTask(t)) continue;

      RankType rf = pd.getTask(f)->getCurrent();
      RankType rt = pd.getTask(t)->getCurrent();

      int cf = -1, ct = -1;
      auto cf_it = task_to_global_cluster.find(f);
      auto ct_it = task_to_global_cluster.find(t);
      if (cf_it != task_to_global_cluster.end()) cf = cf_it->second;
      if (ct_it != task_to_global_cluster.end()) ct = ct_it->second;

      // Base label is bytes
      out << "  \"" << f << "\" -> \"" << t << "\" [label=\"" << std::fixed << std::setprecision(0)
          << e.getVolume() << "\"";

      // Inter-cluster edges are bold
      if (cf != -1 && ct != -1 && cf != ct) {
        out << ", penwidth=2";
      }

      // Inter-rank edges colored
      if (rf != rt) {
        out << ", color=red";
      } else if (cf != -1 && ct != -1 && cf != ct) {
        out << ", color=orange";
      }

      out << "];\n";
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

  // Accumulators during reduction
  std::unordered_map<model::RankType, model::PhaseData> phases_by_rank_;
  std::unordered_map<model::RankType, std::unordered_map<model::TaskType,int>> clusters_by_rank_;
  int expected_children_ = 0;
  int received_children_ = 0;

  // Final merged output at root
  model::PhaseData final_merged_phase_;
  std::unordered_map<model::TaskType,int> final_merged_clusters_;

  // Local cluster mapping for this rank
  std::unordered_map<model::TaskType,int> local_clusters_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_FULL_GRAPH_VISUALIZER_H*/