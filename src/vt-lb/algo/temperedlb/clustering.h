/*
//@HEADER
// *****************************************************************************
//
//                                 clustering.h
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
#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTERING_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTERING_H

#include <vt-lb/config/cmake_config.h>

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/util/logging.h>

#include <algorithm>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <deque>

namespace vt_lb::algo::temperedlb {

// Common clustering artifacts
struct Cluster {
  int id = -1;
  std::vector<vt_lb::model::TaskType> members;
  vt_lb::model::LoadType load = 0.0;
};

// Assumption: all tasks must exist to at least one cluster
struct Clusterer {
  using TaskType = vt_lb::model::TaskType;
  using BytesType = vt_lb::model::BytesType;
  virtual ~Clusterer() = default;
  virtual void compute() = 0;
  virtual std::unordered_map<TaskType,int> const& taskToCluster() const = 0;
  virtual std::vector<Cluster> const& clusters() const = 0;
};

// Communication-based clustering, strawman method
struct CommunicationClusterer : Clusterer {
  using TaskType = vt_lb::model::TaskType;
  using BytesType = vt_lb::model::BytesType;
  using LoadType = vt_lb::model::LoadType;
  using PhaseData = vt_lb::model::PhaseData;

  explicit CommunicationClusterer(PhaseData const& pd) : pd_(pd) {}

  void compute() override {
    clear();
    buildAggregatedEdges();
    collectTasks();
    if (agg_edges_.empty()) {
      int cid = 0;
      for (auto const& t : tasks_) task_to_cluster_[t] = cid++;
      materializeClusters();
      return;
    }
    std::sort(
      agg_edges_.begin(), agg_edges_.end(),
      [](auto const& a, auto const& b){ return std::get<2>(a) > std::get<2>(b); }
    );
    std::unordered_set<TaskType> matched;
    int next_cid = 0;
    for (auto const& e : agg_edges_) {
      auto u = std::get<0>(e);
      auto v = std::get<1>(e);
      if (!matched.count(u) && !matched.count(v)) {
        task_to_cluster_[u] = next_cid;
        task_to_cluster_[v] = next_cid;
        matched.insert(u); matched.insert(v);
        ++next_cid;
      }
    }
    for (auto const& t : tasks_)
      if (!task_to_cluster_.count(t))
        task_to_cluster_[t] = next_cid++;
    materializeClusters();
  }

  std::unordered_map<TaskType,int> const& taskToCluster() const override { return task_to_cluster_; }
  std::vector<Cluster> const& clusters() const override { return clusters_; }

private:
  void clear() {
    tasks_.clear();
    agg_edges_.clear();
    task_to_cluster_.clear();
    clusters_.clear();
  }

  void collectTasks() {
    for (auto const& kv : pd_.getTasksMap()) {
      tasks_.push_back(kv.first);
    }
  }

  void buildAggregatedEdges() {
    struct PairHash {
      size_t operator()(std::pair<TaskType,TaskType> const& p) const noexcept {
        auto a = p.first < p.second ? p.first : p.second;
        auto b = p.first < p.second ? p.second : p.first;
        return (static_cast<size_t>(a) << 32) ^ static_cast<size_t>(b);
      }
    };
    std::unordered_map<std::pair<TaskType,TaskType>, BytesType, PairHash> agg;
    for (auto const& e : pd_.getCommunications()) {
      auto u = e.getFrom(); auto v = e.getTo();
      if (u == v || !pd_.hasTask(u) || !pd_.hasTask(v)) continue;
      auto key = (u < v) ? std::make_pair(u,v) : std::make_pair(v,u);
      agg[key] += e.getVolume();
    }
    agg_edges_.reserve(agg.size());
    for (auto const& kv : agg)
      agg_edges_.emplace_back(kv.first.first, kv.first.second, kv.second);
  }

  void materializeClusters() {
    std::unordered_map<int, Cluster> tmp;
    for (auto const& [t,cid] : task_to_cluster_) {
      auto& cl = tmp[cid]; cl.id = cid; cl.members.push_back(t);
    }
    for (auto& kv : tmp) {
      auto& cl = kv.second;
      for (auto const& t : cl.members)
        cl.load += pd_.getTasksMap().at(t).getLoad();
      clusters_.push_back(std::move(cl));
    }
    std::sort(clusters_.begin(), clusters_.end(), [](auto const& a, auto const& b){ return a.id < b.id; });
  }

private:
  PhaseData const& pd_;
  std::vector<TaskType> tasks_;
  std::vector<std::tuple<TaskType,TaskType,BytesType>> agg_edges_;
  std::unordered_map<TaskType,int> task_to_cluster_;
  std::vector<Cluster> clusters_;
};

// Shared-block-based clustering
struct SharedBlockClusterer : Clusterer {
  using TaskType = vt_lb::model::TaskType;
  using BytesType = vt_lb::model::BytesType;
  using LoadType = vt_lb::model::LoadType;
  using PhaseData = vt_lb::model::PhaseData;

  explicit SharedBlockClusterer(PhaseData const& pd) : pd_(pd) {}

  void compute() override {
    clear();
    buildAggregatedEdgesFromSharedBlocks(); // only shared-block-derived edges
    collectTasks();
    if (sb_edges_.empty()) {
      // Each task its own cluster
      int cid = 0;
      for (auto const& t : tasks_) {
        task_to_cluster_[t] = cid++;
      }
      materializeClusters();
      return;
    }
    std::sort(
      sb_edges_.begin(), sb_edges_.end(),
      [](auto const& a, auto const& b){ return std::get<2>(a) > std::get<2>(b); }
    );
    std::unordered_set<TaskType> matched;
    int next_cid = 0;
    for (auto const& e : sb_edges_) {
      auto u = std::get<0>(e);
      auto v = std::get<1>(e);
      if (!matched.count(u) && !matched.count(v)) {
        task_to_cluster_[u] = next_cid;
        task_to_cluster_[v] = next_cid;
        matched.insert(u);
        matched.insert(v);
        ++next_cid;
      }
    }
    for (auto const& t : tasks_) {
      if (!task_to_cluster_.count(t)) {
        task_to_cluster_[t] = next_cid++;
      }
    }
    materializeClusters();
  }

  std::unordered_map<TaskType,int> const& taskToCluster() const override { return task_to_cluster_; }
  std::vector<Cluster> const& clusters() const override { return clusters_; }

private:
  void clear() {
    tasks_.clear();
    sb_edges_.clear();
    task_to_cluster_.clear();
    clusters_.clear();
  }

  void collectTasks() {
    for (auto const& kv : pd_.getTasksMap()) {
      tasks_.push_back(kv.first);
    }
  }

  void buildAggregatedEdgesFromSharedBlocks() {
    // Build edges exclusively from shared block co-access (communication edges are ignored)
    // Map shared block -> tasks
    std::unordered_map<vt_lb::model::SharedBlockType, std::vector<TaskType>> blk_to_tasks;
    for (auto const& kv : pd_.getTasksMap()) {
      auto const& task = kv.second;
      for (auto const& sb : task.getSharedBlocks()) {
        blk_to_tasks[sb].push_back(task.getId());
      }
    }
    // Aggregate edges: weight is sum of shared block sizes across pairs
    struct PairHash {
      size_t operator()(std::pair<TaskType,TaskType> const& p) const noexcept {
        auto a = p.first < p.second ? p.first : p.second;
        auto b = p.first < p.second ? p.second : p.first;
        return (static_cast<size_t>(a) << 32) ^ static_cast<size_t>(b);
      }
    };
    std::unordered_map<std::pair<TaskType,TaskType>, BytesType, PairHash> agg;
    auto const& sb_map = pd_.getSharedBlocksMap();
    for (auto const& kv : blk_to_tasks) {
      auto blk_id = kv.first;
      auto it_blk = sb_map.find(blk_id);
      if (it_blk == sb_map.end()) continue;
      BytesType size = it_blk->second.getSize();
      auto const& vec = kv.second;
      for (size_t i=0;i<vec.size();++i) {
        for (size_t j=i+1;j<vec.size();++j) {
          auto u = vec[i]; auto v = vec[j];
          auto key = (u < v) ? std::make_pair(u,v) : std::make_pair(v,u);
          agg[key] += size;
        }
      }
    }
    sb_edges_.reserve(agg.size());
    for (auto const& kv : agg) {
      sb_edges_.emplace_back(kv.first.first, kv.first.second, kv.second);
    }
  }

  void materializeClusters() {
    std::unordered_map<int, Cluster> tmp;
    for (auto const& [t,cid] : task_to_cluster_) {
      auto& cl = tmp[cid];
      cl.id = cid;
      cl.members.push_back(t);
    }
    for (auto& kv : tmp) {
      auto& cl = kv.second;
      for (auto const& t : cl.members) {
        cl.load += pd_.getTasksMap().at(t).getLoad();
      }
      clusters_.push_back(std::move(cl));
    }
    std::sort(clusters_.begin(), clusters_.end(), [](auto const& a, auto const& b){ return a.id < b.id; });
  }

private:
  PhaseData const& pd_;
  std::vector<TaskType> tasks_;
  // aggregated undirected shared-block edges: (u,v,weight)
  std::vector<std::tuple<TaskType,TaskType,BytesType>> sb_edges_;
  std::unordered_map<TaskType,int> task_to_cluster_;
  std::vector<Cluster> clusters_;
};

// Standalone Leiden-style clustering using CPM (Constant Potts Model) objective.
// Objective per community c: Q_c = W_in(c) - gamma * C(S_c, 2)
// Move delta (v: node, A->B): dQ = w_{v,B} - w_{v,A} - gamma * (S_B - (S_A - 1))
// Multi-level: local moving -> refinement (split disconnected communities) -> coarsen (communities as super-nodes) -> repeat.
struct LeidenCPMStandaloneClusterer : Clusterer {
  using TaskType  = vt_lb::model::TaskType;
  using BytesType = vt_lb::model::BytesType;
  using PhaseData = vt_lb::model::PhaseData;

  LeidenCPMStandaloneClusterer(
    PhaseData const& pd,
    double resolution = 50.0,
    int max_passes = 10,
    int max_levels = 4
  ) : pd_(pd),
      gamma_(resolution),
      max_passes_(max_passes),
      max_levels_(max_levels)
  {}

  void compute() override {
    clear();
    buildInitialGraph();
    if (node_to_tasks_.empty()) return;

    if (rank0()) {
      VT_LB_LOG(
        Clusterer, normal, "LeidenCPMStandalone: start nodes={} edges={} gamma={:.4f}\n",
        node_to_tasks_.size(), edges_.size(), gamma_
      );
    }

    int level = 0;
    while (level < max_levels_) {
      if (rank0()) {
        VT_LB_LOG(Clusterer, normal, "LeidenCPMStandalone: level {}\n", level);
      }
      bool moved_any = localMovingPhase();
      refinementPhase();

      bool coarsened = coarsenGraph();
      if (rank0()) {
        VT_LB_LOG(
          Clusterer, normal, "  after level {}: moved={} coarsened={} nodes={} edges={}\n",
          level,
          moved_any ? "yes" : "no",
          coarsened ? "yes" : "no",
          node_to_tasks_.size(), edges_.size()
        );
      }
      ++level;
      if (!coarsened) break;
    }

    materializeClusters();

    if (rank0()) {
      VT_LB_LOG(Clusterer, normal, "LeidenCPMStandalone: final communities={}\n", clusters_.size());
      for (auto const& c : clusters_) {
        VT_LB_LOG(Clusterer, normal, "  community {} size={} load={:.2f}\n", c.id, c.members.size(), c.load);
      }
    }
  }

  std::unordered_map<TaskType,int> const& taskToCluster() const override { return task_to_cluster_; }
  std::vector<Cluster> const& clusters() const override { return clusters_; }

private:
  // ---------- Graph state for current level ----------
  // nodes are indexed [0..N-1], each super-node maps to a vector of original tasks
  std::vector<std::vector<TaskType>> node_to_tasks_;
  // adjacency list: for each node, vector of (neighbor_node, weight)
  std::vector<std::vector<std::pair<int,double>>> adj_;
  // unique undirected edge list as (u,v,w) with u<v
  std::vector<std::tuple<int,int,double>> edges_;

  // community membership for current level: node -> community id
  std::vector<int> membership_;
  // size of each community in number of nodes (not original tasks)
  std::unordered_map<int,int> comm_size_;

  // ---------- Inputs/outputs ----------
  PhaseData const& pd_;
  double gamma_ = 1.0;
  int max_passes_ = 10;
  int max_levels_ = 4;

  std::unordered_map<TaskType,int> task_to_cluster_;
  std::vector<Cluster> clusters_;

  // ---------- Utils ----------
  bool rank0() const { return pd_.getRank() == 0; }

  void clear() {
    node_to_tasks_.clear();
    adj_.clear();
    edges_.clear();
    membership_.clear();
    comm_size_.clear();
    task_to_cluster_.clear();
    clusters_.clear();
  }

  static unsigned long long key(TaskType a, TaskType b) {
    auto x = std::min(a,b);
    auto y = std::max(a,b);
    return (unsigned long long)x << 32 | (unsigned long long)y;
  }

  void buildInitialGraph() {
    // Aggregate undirected weights from communications
    std::unordered_map<unsigned long long, double> agg;
    std::vector<TaskType> tasks;
    tasks.reserve(pd_.getTasksMap().size());
    for (auto const& kv : pd_.getTasksMap()) tasks.push_back(kv.first);

    for (auto const& e : pd_.getCommunications()) {
      auto u = e.getFrom(), v = e.getTo();
      if (u == v || !pd_.hasTask(u) || !pd_.hasTask(v)) continue;
      agg[key(u,v)] += e.getVolume();
    }

    // Node 0..N-1 map 1-1 to tasks initially
    size_t N = tasks.size();
    node_to_tasks_.resize(N);
    std::unordered_map<TaskType,int> t2i;
    t2i.reserve(N);
    for (size_t i=0;i<N;++i) {
      node_to_tasks_[i].push_back(tasks[i]);
      t2i[tasks[i]] = (int)i;
    }

    // Build edges_ and adj_
    adj_.assign(N, {});
    edges_.clear(); edges_.reserve(agg.size());
    for (auto const& kv : agg) {
      TaskType a = (TaskType)(kv.first >> 32);
      TaskType b = (TaskType)(kv.first & 0xffffffffULL);
      int u = t2i[a], v = t2i[b];
      double w = kv.second;
      if (u == v) continue;
      int x = std::min(u,v), y = std::max(u,v);
      edges_.emplace_back(x,y,w);
      adj_[u].push_back({v,w});
      adj_[v].push_back({u,w});
    }

    // Initialize membership: each node its own community
    membership_.resize(N);
    comm_size_.clear();
    for (int i=0;i<(int)N;++i) {
      membership_[i] = i;
      comm_size_[i] = 1;
    }
  }

  // Compute w_{v,c} sums for neighboring communities of v
  void neighborCommWeights(int v, std::unordered_map<int,double>& sums) const {
    sums.clear();
    for (auto const& pr : adj_[v]) {
      int u = pr.first;
      double w = pr.second;
      int c = membership_[u];
      sums[c] += w;
    }
  }

  // Local moving phase (greedy CPM improvement)
  bool localMovingPhase() {
    bool moved_any = false;
    std::mt19937 gen( (unsigned)pd_.getRank() + 7777 );
    std::vector<int> order(membership_.size());
    std::iota(order.begin(), order.end(), 0);

    for (int pass=0; pass<max_passes_; ++pass) {
      std::shuffle(order.begin(), order.end(), gen);
      bool moved = false;

      for (int v : order) {
        int cA = membership_[v];
        int sizeA = comm_size_[cA];

        // Compute w_{v,c} for neighbor communities
        std::unordered_map<int,double> w_by_comm;
        neighborCommWeights(v, w_by_comm);
        double w_vA = w_by_comm.count(cA) ? w_by_comm[cA] : 0.0;

        // Evaluate moves to neighbor communities
        double best_delta = 0.0;
        int best_comm = cA;

        for (auto const& kv : w_by_comm) {
          int cB = kv.first;
          if (cB == cA) continue;
          int sizeB = comm_size_[cB];
          double w_vB = kv.second;
          double delta = w_vB - w_vA - gamma_ * (sizeB - (sizeA - 1));
          if (delta > best_delta + 1e-14) {
            best_delta = delta;
            best_comm = cB;
          }
        }

        // Consider move to an empty community (new singleton)
        {
          // sizeB = 0, w_vB = 0
          double delta_empty = - w_vA - gamma_ * (0 - (sizeA - 1));
          if (delta_empty > best_delta + 1e-14) {
            best_delta = delta_empty;
            best_comm = makeNewCommunityId();
          }
        }

        if (best_comm != cA) {
          // Apply move v: cA -> best_comm
          decrementCommSize(cA);
          membership_[v] = best_comm;
          incrementCommSize(best_comm);
          moved = true;
          moved_any = true;
        }
      }

      if (rank0()) {
        VT_LB_LOG(Clusterer, normal, "  local pass {} moved={}\n", pass, moved ? "yes" : "no");
      }
      if (!moved) break;
    }
    return moved_any;
  }

  // Split any community that is not connected into connected components
  void refinementPhase() {
    // Group nodes by community
    std::unordered_map<int,std::vector<int>> nodes_by_comm;
    for (int v=0; v<(int)membership_.size(); ++v)
      nodes_by_comm[membership_[v]].push_back(v);

    int splits = 0;
    for (auto& kv : nodes_by_comm) {
      auto const& nodes = kv.second;
      if (nodes.size() <= 1) continue;

      // Build set for fast membership filtering
      std::unordered_set<int> node_set(nodes.begin(), nodes.end());

      // Find connected components inside this community
      std::unordered_map<int,int> comp_id; // node->component
      int next_comp = 0;
      for (int s : nodes) {
        if (comp_id.count(s)) continue;
        // BFS/DFS within node_set
        std::deque<int> dq;
        dq.push_back(s);
        comp_id[s] = next_comp;
        while (!dq.empty()) {
          int x = dq.front(); dq.pop_front();
          for (auto const& pr : adj_[x]) {
            int y = pr.first;
            if (!node_set.count(y)) continue;
            if (!comp_id.count(y)) {
              comp_id[y] = next_comp;
              dq.push_back(y);
            }
          }
        }
        ++next_comp;
      }
      if (next_comp <= 1) continue; // already connected

      // Split: assign each component to a new community id
      std::unordered_map<int,int> comp_to_comm;
      for (auto const& p : comp_id) {
        int v = p.first;
        int c = p.second;
        int target_comm;
        auto it = comp_to_comm.find(c);
        if (it == comp_to_comm.end()) {
          target_comm = makeNewCommunityId();
          comp_to_comm[c] = target_comm;
        } else {
          target_comm = it->second;
        }
        decrementCommSize(membership_[v]);
        membership_[v] = target_comm;
        incrementCommSize(target_comm);
      }
      ++splits;
    }
    if (rank0()) {
      VT_LB_LOG(Clusterer, normal, "  refinement: splits={}\n", splits);
    }
  }

  // Build coarse graph of communities; return false if no aggregation possible
  bool coarsenGraph() {
    // Map current community ids to compact ids [0..C-1]
    std::unordered_map<int,int> remap;
    int next = 0;
    for (int cid : membership_) {
      if (!remap.count(cid)) remap[cid] = next++;
    }
    int C = next;
    if (C == (int)node_to_tasks_.size()) return false;

    // Build new nodes (super-nodes) membership: node index -> compact comm
    std::vector<int> node_comm_compact(membership_.size());
    for (int i=0;i<(int)membership_.size();++i) node_comm_compact[i] = remap[membership_[i]];

    // Aggregate members: combine original tasks per super-node
    std::vector<std::vector<TaskType>> new_node_to_tasks(C);
    for (int i=0;i<(int)node_to_tasks_.size(); ++i) {
      int c = node_comm_compact[i];
      auto& dst = new_node_to_tasks[c];
      auto& src = node_to_tasks_[i];
      dst.insert(dst.end(), src.begin(), src.end());
    }

    // Aggregate edges between communities
    std::unordered_map<unsigned long long, double> agg_edges;
    for (auto const& e : edges_) {
      int u = std::get<0>(e);
      int v = std::get<1>(e);
      double w = std::get<2>(e);
      int cu = node_comm_compact[u];
      int cv = node_comm_compact[v];
      if (cu == cv) continue;
      int a = std::min(cu,cv), b = std::max(cu,cv);
      unsigned long long k = ((unsigned long long)a << 32) | (unsigned long long)b;
      agg_edges[k] += w;
    }

    // Rebuild current graph state from aggregates
    node_to_tasks_.swap(new_node_to_tasks);

    adj_.assign(C, {});
    edges_.clear(); edges_.reserve(agg_edges.size());
    for (auto const& kv : agg_edges) {
      int a = (int)(kv.first >> 32);
      int b = (int)(kv.first & 0xffffffffULL);
      double w = kv.second;
      edges_.emplace_back(a,b,w);
      adj_[a].push_back({b,w});
      adj_[b].push_back({a,w});
    }

    // Reset membership: each super-node starts in its own community
    membership_.assign(C, 0);
    comm_size_.clear();
    for (int i=0;i<C;++i) {
      membership_[i] = i;
      comm_size_[i] = 1;
    }
    return true;
  }

  // Helpers to maintain comm_size_
  int makeNewCommunityId() {
    // pick the next integer not in comm_size_
    int id = (int)comm_size_.size() ? maxCommId()+1 : 0;
    // Ensure uniqueness
    while (comm_size_.count(id)) ++id;
    comm_size_[id] = 0;
    return id;
  }

  int maxCommId() const {
    int mx = -1;
    for (auto const& kv : comm_size_) mx = std::max(mx, kv.first);
    return mx;
  }

  void incrementCommSize(int cid) { ++comm_size_[cid]; }

  void decrementCommSize(int cid) {
    auto it = comm_size_.find(cid);
    if (it != comm_size_.end()) {
      --(it->second);
      if (it->second == 0) {
        // keep empty comm id so future renumbering can compress
        // do not erase to avoid reusing id within phase
      }
    }
  }

  void materializeClusters() {
    // Remap final communities to 0..K-1
    std::unordered_map<int,int> remap;
    int next = 0;
    for (int cid : membership_) if (!remap.count(cid)) remap[cid] = next++;
    int K = next;

    // Accumulate original tasks by final community
    std::vector<std::vector<TaskType>> cl_tasks(K);
    for (int n=0; n<(int)node_to_tasks_.size(); ++n) {
      int c = remap[membership_[n]];
      cl_tasks[c].insert(cl_tasks[c].end(),
                         node_to_tasks_[n].begin(),
                         node_to_tasks_[n].end());
    }

    // Build clusters_ and task_to_cluster_
    std::unordered_map<int, Cluster> tmp;
    for (int cid=0; cid<K; ++cid) {
      auto& cl = tmp[cid];
      cl.id = cid;
      cl.members = std::move(cl_tasks[cid]);
      double load_sum = 0.0;
      for (auto t : cl.members) {
        task_to_cluster_[t] = cid;
        if (pd_.hasTask(t)) load_sum += pd_.getTasksMap().at(t).getLoad();
      }
      cl.load = load_sum;
    }
    for (auto& kv : tmp) clusters_.push_back(std::move(kv.second));
    std::sort(clusters_.begin(), clusters_.end(),
              [](auto const& a, auto const& b){ return a.id < b.id; });
  }

};

// Utility: verify that all tasks in pd are present in the cluster mapping
inline bool allTasksClustered(Clusterer const& clusterer, vt_lb::model::PhaseData const& pd) {
  auto const& t2c = clusterer.taskToCluster();
  for (auto const& kv : pd.getTasksMap()) {
    auto const task_id = kv.first;
    if (t2c.find(task_id) == t2c.end()) {
      return false;
    }
  }
  return true;
}

} // namespace vt_lb::algo::temperedlb

#endif // INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTERING_H