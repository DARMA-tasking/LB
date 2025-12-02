#pragma once

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Communication.h>
#include "clustering.h"

#include <sstream>
#include <iomanip>
#include <array>
#include <cmath> // llround

namespace vt_lb::algo::temperedlb {

/**
 * Build a GraphViz DOT string representing the task communication graph.
 * If clusterer != nullptr and show_clusters == true, tasks are grouped
 * into subgraphs per cluster with colors.
 */
inline std::string buildTaskGraphDOT(
  vt_lb::model::PhaseData const& pd,
  Clusterer const* clusterer,
  bool show_clusters,
  bool show_loads
) {
  using TaskType = vt_lb::model::TaskType;
  std::ostringstream out;
  out << "digraph TaskGraph {\n";
  out << "  rankdir=LR;\n";
  out << "  node [shape=ellipse, fontname=\"Helvetica\"];\n";

  std::unordered_map<TaskType,int> cluster_map;
  std::vector<Cluster> clusters;
  if (clusterer && show_clusters) {
    cluster_map = clusterer->taskToCluster();
    clusters = clusterer->clusters();
  }

  // Compute per-cluster boundary bytes (sum of edges crossing cluster boundary)
  // Use integer accumulation to avoid FP off-by-one when printing.
  std::unordered_map<int, unsigned long long> cluster_boundary_bytes;
  // New: intra-cluster total bytes (sum of edges where both endpoints in same cluster)
  std::unordered_map<int, unsigned long long> cluster_intra_bytes;
  if (show_clusters && !clusters.empty()) {
    // Build fast membership sets per cluster
    std::unordered_map<int, std::unordered_set<TaskType>> cluster_members;
    for (auto const& cl : clusters) {
      cluster_members[cl.id] = std::unordered_set<TaskType>(cl.members.begin(), cl.members.end());
      cluster_boundary_bytes[cl.id] = 0ULL;
      cluster_intra_bytes[cl.id] = 0ULL; // initialize intra
    }
    // Iterate communications and add volume to intra or boundary
    for (auto const& e : pd.getCommunications()) {
      auto f = e.getFrom();
      auto t = e.getTo();
      // Skip edges without tasks
      if (!pd.hasTask(f) || !pd.hasTask(t)) continue;

      // Round each edge volume to integer bytes before summing
      unsigned long long vol = static_cast<unsigned long long>(std::llround(e.getVolume()));

      auto cf_it = cluster_map.find(f);
      auto ct_it = cluster_map.find(t);
      if (cf_it != cluster_map.end() || ct_it != cluster_map.end()) {
        if (cf_it != cluster_map.end() && ct_it != cluster_map.end()) {
          int cf = cf_it->second;
          int ct = ct_it->second;
          if (cf == ct) {
            // Intra-cluster communication
            cluster_intra_bytes[cf] += vol;
          } else {
            // Crossing cluster boundary
            cluster_boundary_bytes[cf] += vol;
            cluster_boundary_bytes[ct] += vol;
          }
        } else {
          // One endpoint is in some cluster, the other is unclustered
          if (cf_it != cluster_map.end()) {
            cluster_boundary_bytes[cf_it->second] += vol;
          }
          if (ct_it != cluster_map.end()) {
            cluster_boundary_bytes[ct_it->second] += vol;
          }
        }
      }
    }
  }

  // Color palette
  std::array<const char*, 12> palette = {
    "#1f77b4","#ff7f0e","#2ca02c","#d62728",
    "#9467bd","#8c564b","#e377c2","#7f7f7f",
    "#bcbd22","#17becf","#393b79","#637939"
  };

  // Emit clusters or standalone nodes
  if (show_clusters && !clusters.empty()) {
    for (auto const& cl : clusters) {
      auto color = palette[cl.id % palette.size()];
      unsigned long long bytes_boundary = cluster_boundary_bytes.count(cl.id) ? cluster_boundary_bytes[cl.id] : 0ULL;
      unsigned long long bytes_intra    = cluster_intra_bytes.count(cl.id)    ? cluster_intra_bytes[cl.id]    : 0ULL;
      out << "  subgraph cluster_" << cl.id << " {\n";
      out << "    label=\"cluster " << cl.id
          << "\\nload=" << std::fixed << std::setprecision(2) << cl.load
          << "\\nbytes_intra=" << bytes_intra
          << "\\nbytes_boundary=" << bytes_boundary << "\";\n";
      out << "    color=\"" << color << "\";\n";
      for (auto t : cl.members) {
        auto const* task = pd.getTask(t);
        if (!task) continue;
        out << "    \"" << t << "\" [style=filled, fillcolor=\"" << color << "\", label=\""
            << t;
        if (show_loads) {
          out << "\\nL=" << std::fixed << std::setprecision(1) << task->getLoad();
        }
        out << "\"];\n";
      }
      out << "  }\n";
    }
  } else {
    // Just tasks
    for (auto const& kv : pd.getTasksMap()) {
      auto const& task = kv.second;
      out << "  \"" << task.getId() << "\" [label=\"" << task.getId();
      if (show_loads) {
        out << "\\nL=" << std::fixed << std::setprecision(1) << task.getLoad();
      }
      out << "\"];\n";
    }
  }

  // Edges
  for (auto const& e : pd.getCommunications()) {
    auto f = e.getFrom();
    auto t = e.getTo();
    if (!pd.hasTask(f) || !pd.hasTask(t)) continue;
    out << "  \"" << f << "\" -> \"" << t << "\" [label=\"" << std::fixed << std::setprecision(0)
        << e.getVolume() << "\"";
    // If clusters shown, style inter-cluster edges bold
    if (show_clusters && !cluster_map.empty()) {
      auto cf_it = cluster_map.find(f);
      auto ct_it = cluster_map.find(t);
      if (cf_it != cluster_map.end() && ct_it != cluster_map.end() && cf_it->second != ct_it->second) {
        out << ", penwidth=2, color=red";
      }
    }
    out << "];\n";
  }

  out << "}\n";
  return out.str();
}

} // namespace vt_lb::algo::temperedlb
