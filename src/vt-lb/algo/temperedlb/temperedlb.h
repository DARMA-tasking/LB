/*
//@HEADER
// *****************************************************************************
//
//                                 temperedlb.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H

#include <vt-lb/comm/comm_traits.h>
#include <vt-lb/algo/baselb/baselb.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/symmetrize_comm.h>
#include <vt-lb/algo/temperedlb/visualize.h>

#include <limits>
#include <random>
#include <ostream>
#include <fstream>
#include <cassert>

#include <mpi.h>

namespace vt_lb::algo::temperedlb {

struct WorkModel {
  /// @brief  Coefficient for load component (per rank)
  double rank_alpha = 1.0;
  /// @brief  Coefficient for inter-node communication component
  double beta = 0.0;
  /// @brief  Coefficient for intra-node communication component
  double gamma = 0.0;
  /// @brief  Coefficient for shared-memory communication component
  double delta = 0.0;

  /// @brief Whether memory information is available
  bool has_memory_info = true;
  /// @brief Has task serialized memory info
  bool has_task_serialized_memory_info = true;
  /// @brief Has task working memory info
  bool has_task_working_memory_info = true;
  /// @brief Has task footprint memory info
  bool has_task_footprint_memory_info = true;
  /// @brief Has shared block memory info
  bool has_shared_block_memory_info = true;

  double applyWorkFormula(
    double compute, double inter_comm_bytes, double intra_comm_bytes,
    double shared_comm_bytes
  ) const {
    return
      rank_alpha * compute +
      beta  * inter_comm_bytes +
      gamma * intra_comm_bytes +
      delta * shared_comm_bytes;
  }
};

struct Configuration {
  Configuration() = default;

  explicit Configuration(int num_ranks) {
    f_ = 2;
    k_max_ = std::ceil(std::sqrt(std::log(num_ranks)/std::log(2.0)));
  }

  bool hasMemoryInfo() const { return work_model_.has_memory_info; }
  bool hasTaskSerializedMemoryInfo() const {
    return hasMemoryInfo() && work_model_.has_task_serialized_memory_info;
  }
  bool hasTaskWorkingMemoryInfo() const {
    return hasMemoryInfo() && work_model_.has_task_working_memory_info;
  }
  bool hasTaskFootprintMemoryInfo() const {
    return hasMemoryInfo() && work_model_.has_task_footprint_memory_info;
  }
  bool hasSharedBlockMemoryInfo() const {
    return hasMemoryInfo() && work_model_.has_shared_block_memory_info;
  }

  /// @brief  Number of trials to perform
  int num_trials_ = 1;
  /// @brief  Number of iterations per trial
  int num_iters_ = 10;
  /// @brief  Fanout for information propagation
  int f_ = 2;
  /// @brief  Number of rounds of information propagation
  int k_max_ = 1;
  /// @brief Whether to use deterministic selection
  bool deterministic_ = true;
  /// @brief Seed for random number generation when deterministic_ is true
  int seed_ = 29;

  /// @brief  Work model parameters (rank-alpha, beta, gamma, delta)
  WorkModel work_model_;

  /// @brief Whether to cluster based on shared blocks
  bool cluster_based_on_shared_blocks_ = false;
  /// @brief Whether to cluster based on communication
  bool cluster_based_on_communication_ = false;
  /// @brief Whether to visualize the task graph
  bool visualize_task_graph_ = false;
  /// @brief Whether to visualize the clusters
  bool visualize_clusters_ = false;

  /// @brief Tolerance for convergence
  double converge_tolerance_ = 0.01;
};

template <typename CommT, typename DataT, typename JoinT>
struct InformationPropagation {
  using ThisType = InformationPropagation<CommT, DataT, JoinT>;
  using JoinedDataType = std::unordered_map<int, DataT>;
  using HandleType = typename CommT::template HandleType<ThisType>;

  /**
   * @brief Construct information propagation instance
   *
   * @param comm Communication interface -- n.b., we clone comm to create a new termination scope
   * @param f Fanout parameter
   * @param k_max Maximum number of rounds
   * @param deterministic Whether to use deterministic selection
   *
   */
  InformationPropagation(CommT& comm, int f, int k_max, bool deterministic, int seed)
    : comm_(comm.clone()), // collective operation
      f_(f),
      k_max_(k_max),
      deterministic_(deterministic)
  {
    handle_ = comm_.template registerInstanceCollective<ThisType>(this);

    if (deterministic_) {
      gen_select_.seed(seed + comm_.getRank());
    }
  }

  JoinedDataType run(DataT initial_data) {
    // Insert this rank to avoid self-selection
    already_selected_.insert(comm_.getRank());

    local_data_[comm_.getRank()] = initial_data;

    sendToFanout(1, local_data_);

    // Wait for termination to happen
    while (comm_.poll()) {
      // do nothing
    }

    printf("%d: done with poll: local_data size=%zu\n", comm_.getRank(), local_data_.size());

    return local_data_;
  }

  void sendToFanout(int round, JoinedDataType const& data) {
    int const rank = comm_.getRank();
    int const num_ranks = comm_.numRanks();

    sent_count_ = 0;
    recv_count_ = 0;

    for (int i = 1; i <= f_; ++i) {
      if (already_selected_.size() >= static_cast<size_t>(num_ranks)) {
        return;
      }

      std::uniform_int_distribution<int> dist(0, num_ranks - 1);
      int target = -1;
      do {
        target = dist(gen_select_);
      } while (already_selected_.find(target) != already_selected_.end());

      already_selected_.insert(target);

      //printf("rank %d sending to rank %d\n", comm_.getRank(), target);
      sent_count_++;
      handle_[target].template send<&ThisType::infoPropagateHandler>(rank, round, data);
    }

    if (deterministic_) {
      // In deterministic mode, we expect an ack from each sent message
      while (sent_count_ != recv_count_) {
        comm_.poll();
      }

      if (round < k_max_) {
        sendToFanout(round + 1, local_data_);
      }
    }
  }

  void infoAckHandler() {
    recv_count_++;
    //printf("rank %d received ack %d/%d\n", comm_.getRank(), recv_count_, sent_count_);
  }

  void infoPropagateHandler(int from_rank, int round, JoinedDataType incoming_data) {
    // Process incoming data and add to local data
    local_data_.insert(incoming_data.begin(), incoming_data.end());

    if (deterministic_) {
      // Acknowledge receipt of message to sender before we go to the next round
      handle_[from_rank].template send<&ThisType::infoAckHandler>();
    } else {
      if (round < k_max_) {
        sendToFanout(round + 1, local_data_);
      }
    }
  }

private:
  CommT comm_;
  int f_ = 2;
  int k_max_ = 2;
  bool deterministic_ = false;
  int sent_count_ = 0;
  int recv_count_ = 0;
  std::unordered_set<int> already_selected_;
  std::unordered_map<int, DataT> local_data_;
  std::mt19937 gen_select_{std::random_device{}()};
  HandleType handle_;
};

struct TaskClusterSummaryInfo {
  int cluster_id = -1;
  int num_tasks_ = 0;
  double cluster_load = 0.0;
  double cluster_intra_send_bytes = 0.0;
  double cluster_intra_recv_bytes = 0.0;
  std::unordered_set<model::Edge> inter_edges_;

  // Memory info
  std::unordered_map<model::SharedBlockType, model::BytesType> shared_block_bytes_;
  model::BytesType max_object_working_bytes = 0;
  model::BytesType max_object_working_bytes_outside = 0;
  model::BytesType max_object_serialized_bytes = 0;
  model::BytesType max_object_serialized_bytes_outside = 0;
  model::BytesType cluster_footprint = 0;

  template <typename SerializerT>
  void serializer(SerializerT& s) {
    s | cluster_id;
    s | num_tasks_;
    s | cluster_load;
    s | cluster_intra_send_bytes;
    s | cluster_intra_recv_bytes;
    s | inter_edges_;
    s | shared_block_bytes_;
    s | max_object_working_bytes;
    s | max_object_working_bytes_outside;
    s | max_object_serialized_bytes;
    s | max_object_serialized_bytes_outside;
    s | cluster_footprint;
  }
};

struct WorkBreakdown {
  double compute = 0.0;
  double inter_node_recv_comm = 0.0;
  double inter_node_send_comm = 0.0;
  double intra_node_recv_comm = 0.0;
  double intra_node_send_comm = 0.0;
  double shared_mem_comm = 0.0;
};

template <typename CommT>
struct TemperedLB : baselb::BaseLB {
  using HandleType = typename CommT::template HandleType<TemperedLB<CommT>>;

  // Assert that CommT conforms to the communication interface we expect
  static_assert(comm::is_comm_conformant<CommT>::value, "CommT must be comm conformant");

  /**
   * @brief Construct a new TemperedLB object
   *
   * @param comm Communication interface
   * @param config Configuration parameters
   */
  TemperedLB(CommT& comm, Configuration config = Configuration())
      : comm_(comm),
        config_(config),
        handle_(comm_.template registerInstanceCollective<TemperedLB<CommT>>(this))
  { }

  void clusterBasedOnCommunication() {
    auto& pd = this->getPhaseData();
    clusterer_ = std::make_unique<LeidenCPMStandaloneClusterer>(pd);
    clusterer_->compute();
  }

  void clusterBasedOnSharedBlocks() {
    auto& pd = this->getPhaseData();
    clusterer_ = std::make_unique<SharedBlockClusterer>(pd);
    clusterer_->compute();
  }

  void makeCommunicationsSymmetric() {
    CommunicationsSymmetrizer<CommT> symm(comm_, this->getPhaseData());
    symm.run();
  }

  void visualizeGraph(const std::string& prefix) const {
    if (!config_.visualize_task_graph_ && !config_.visualize_clusters_) {
      return;
    }
    std::string base = prefix + "_rank" + std::to_string(comm_.getRank());
    auto const& pd = this->getPhaseData();
    const Clusterer* cl = getClusterer();
    std::string dot = vt_lb::algo::temperedlb::buildTaskGraphDOT(
      pd,
      cl,
      config_.visualize_clusters_,
      /*show_loads*/true
    );
    std::ofstream ofs(base + ".dot");
    if (ofs.good()) {
      ofs << dot;
    }
  }

  void run() {
    auto total_load = computeLoad();
    printf("%d: initial total load: %f, num tasks: %zu\n", comm_.getRank(), total_load, numTasks());

    // Make communications symmetric before distributed decisions
    makeCommunicationsSymmetric();

    if (config_.cluster_based_on_communication_ || config_.cluster_based_on_shared_blocks_) {
      if (config_.cluster_based_on_communication_) {
        clusterBasedOnCommunication();
      } else if (config_.cluster_based_on_shared_blocks_) {
        clusterBasedOnSharedBlocks();
      }
    }

    // Generate visualization after symmetrization/clustering
    visualizeGraph("temperedlb2");

    auto& wm = config_.work_model_;
    if (wm.beta == 0.0 && wm.gamma == 0.0 && wm.delta == 0.0) {
      using LoadType = double;
      auto ip = InformationPropagation<CommT, LoadType, TemperedLB<CommT>>(
        comm_,
        config_.f_,
        config_.k_max_,
        config_.deterministic_,
        config_.seed_
      );
      auto info = ip.run(total_load);
      //printf("%d: gathered load info from %zu ranks\n", comm_.getRank(), info.size());
    } else {
#if 0
      computeGlobalMaxClusters();
#else
      // Just assume max of 1000 clusters per rank for now, until we have bcast
#endif
    }
  }

  Clusterer const* getClusterer() const { return clusterer_.get(); }

private:
  WorkBreakdown computeWorkBreakdown() const {
    WorkBreakdown breakdown;
    std::unordered_set<model::SharedBlockType> shared_blocks_here;

    // Rank-alpha term
    for (auto const& [id, task] : this->getPhaseData().getTasksMap()) {
      breakdown.compute += task.getLoad();
      for (auto const& sb : task.getSharedBlocks()) {
        shared_blocks_here.insert(sb);
      }
    }

    // Communication terms
    for (auto const& e : this->getPhaseData().getCommunications()) {
      assert(
        (e.getFromRank() == comm_.getRank() || e.getToRank() == comm_.getRank()) &&
        "Edge does not belong to this rank"
      );
      if (e.getFromRank() != e.getToRank()) {
        if (e.getToRank() == comm_.getRank()) {
          breakdown.inter_node_recv_comm += e.getVolume();
        } else {
          breakdown.inter_node_send_comm += e.getVolume();
        }
      } else {
        // Intra-node: for this rank, edge is both sent and received locally
        if (e.getToRank() == comm_.getRank()) {
          breakdown.intra_node_recv_comm += e.getVolume();
          breakdown.intra_node_send_comm += e.getVolume();
        }
      }
    }

    // Shared-memory communication term
    for (auto const& sb : shared_blocks_here) {
      assert(getPhaseData().hasSharedBlock(sb) && "Shared block information missing");
      auto info = getPhaseData().getSharedBlock(sb);
      if (info->getHome() != comm_.getRank()) {
        breakdown.shared_mem_comm += info->getSize();
      }
    }

    return breakdown;
  }

  double computeNewWork(
    WorkBreakdown breakdown,
    std::vector<model::Task> const& to_add,
    std::vector<model::Edge> to_add_edges,
    std::vector<model::TaskType> const& to_remove
  ) const {
    auto new_bd = breakdown;
    int const rank = comm_.getRank();

    std::unordered_set<model::TaskType> remove_set(to_remove.begin(), to_remove.end());

    // Adjust compute for removed and added tasks
    if (!remove_set.empty()) {
      auto const& tasks_map = this->getPhaseData().getTasksMap();
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
      for (auto const& e : this->getPhaseData().getCommunications()) {
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
      for (auto const& [id, task] : this->getPhaseData().getTasksMap()) {
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
        assert(getPhaseData().hasSharedBlock(sb) && "Shared block information missing");
        auto info = getPhaseData().getSharedBlock(sb);
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
    return computeWork(new_bd);
  }

  double computeWork(WorkBreakdown breakdown) const {
    return config_.work_model_.applyWorkFormula(
      breakdown.compute,
      std::max(breakdown.inter_node_recv_comm, breakdown.inter_node_send_comm),
      std::max(breakdown.intra_node_recv_comm, breakdown.intra_node_send_comm),
      breakdown.shared_mem_comm
    );
  }

  double computeMemoryUsage() const {
    if (!config_.hasMemoryInfo()) {
      return 0.0;
    }

    double task_footprint_bytes_ = 0.0;
    double task_max_working_bytes_ = 0.0;
    double task_max_serialized_bytes_ = 0.0;
    double shared_blocks_bytes_ = 0.0;
    std::unordered_set<model::SharedBlockType> shared_blocks_here;
    for (auto const& [id, task] : this->getPhaseData().getTasksMap()) {
      if (config_.hasTaskFootprintMemoryInfo()) {
        task_footprint_bytes_ += task.getMemory().footprint_bytes;
      }
      if (config_.hasTaskWorkingMemoryInfo()) {
        task_max_working_bytes_ = std::max(
          task_max_working_bytes_, task.getMemory().working_bytes
        );
      }
      if (config_.hasTaskSerializedMemoryInfo()) {
        task_max_serialized_bytes_ = std::max(
          task_max_serialized_bytes_, task.getMemory().serialized_bytes
        );
      }
      if (config_.hasSharedBlockMemoryInfo()) {
        for (auto const& sb : task.getSharedBlocks()) {
          shared_blocks_here.insert(sb);
        }
      }
    }
    for (auto const& sb : shared_blocks_here) {
      assert(getPhaseData().hasSharedBlock(sb) && "Shared block information missing");
      auto info = getPhaseData().getSharedBlock(sb);
      shared_blocks_bytes_ += info->getSize();
    }
    return this->getPhaseData().getRankFootprintBytes() +
      task_footprint_bytes_ +
      task_max_working_bytes_ +
      task_max_serialized_bytes_ +
      shared_blocks_bytes_;
  }

private:
  void computeGlobalMaxClusters() {
    // compute max number of clusters on any rank
    int local_clusters = 0;
    if (clusterer_) {
      // assume Clusterer provides numClusters(); if not, set appropriately
      local_clusters = static_cast<int>(clusterer_->clusters().size());
    }

    int const root = 0;
    comm_.reduce(root, MPI_INT, MPI_MAX, &local_clusters, &global_max_clusters_, 1);

    if (comm_.getRank() == root) {
      printf("%d: global max clusters across ranks: %d\n", root, global_max_clusters_);
    }
    // @todo: once we have a bcast, broadcast global_max_clusters_ to all ranks
  }

  int localToGlobalClusterID(int cluster_id) const {
    // Map local cluster IDs to global cluster IDs based on global_max_clusters_
    // Implementation depends on how clusters are represented and communicated
    return cluster_id + comm_.getRank() * global_max_clusters_;
  }

  int globalToLocalClusterID(int global_cluster_id) const {
    return global_cluster_id % global_max_clusters_;
  }

  int globalClusterToRank(int global_cluster_id) const {
    return global_cluster_id / global_max_clusters_;
  }

private:
  /// @brief Communication interface
  CommT& comm_;
  /// @brief Configuration parameters
  Configuration config_;
  /// @brief Handle to this load balancer instance
  HandleType handle_;
  /// @brief Computed communication-based clusters for current phase
  std::unique_ptr<Clusterer> clusterer_;
  /// @brief Global maximum number of clusters across all ranks
  int global_max_clusters_ = 1000;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H*/
