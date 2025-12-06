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
#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/symmetrize_comm.h>
#include <vt-lb/algo/temperedlb/visualize.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>
#include <vt-lb/algo/temperedlb/full_graph_visualizer.h>
#include <vt-lb/util/logging.h>

#include <limits>
#include <random>
#include <ostream>
#include <fstream>
#include <cassert>

#include <mpi.h>

#define VT_LB_LOG(mode, ...) ::vt_lb::util::log(::vt_lb::util::Component::LoadBalancer, ::vt_lb::util::Verbosity::mode, __VA_ARGS__)

namespace vt_lb::algo::temperedlb {

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
  InformationPropagation(CommT& comm, Configuration const& config)
    : comm_(comm.clone()), // collective operation
      f_(config.f_),
      k_max_(config.k_max_),
      deterministic_(config.deterministic_)
  {
    handle_ = comm_.template registerInstanceCollective<ThisType>(this);

    if (deterministic_) {
      gen_select_.seed(config.seed_ + comm_.getRank());
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

    VT_LB_LOG(normal, "done with poll: local_data size={}\n", local_data_.size());

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

template <typename CommT>
struct TemperedLB final : baselb::BaseLB {
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
    clusterer_ = std::make_unique<LeidenCPMStandaloneClusterer>(pd, 80.0);
    clusterer_->compute();
  }

  void clusterBasedOnSharedBlocks() {
    auto& pd = this->getPhaseData();
    clusterer_ = std::make_unique<SharedBlockClusterer>(pd);
    clusterer_->compute();
  }

  void doClustering() {
    if (config_.cluster_based_on_communication_ || config_.cluster_based_on_shared_blocks_) {
      if (config_.cluster_based_on_communication_) {
        clusterBasedOnCommunication();
      } else if (config_.cluster_based_on_shared_blocks_) {
        clusterBasedOnSharedBlocks();
      }
    }
  }

  std::unordered_map<int, TaskClusterSummaryInfo> buildClusterSummaries() {
    return ClusterSummarizer::buildClusterSummaries(
      this->getPhaseData(), config_, getClusterer(), global_max_clusters_
    );
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

  void visualizeFullGraphIfNeeded(
    CommT& comm,
    model::PhaseData const& pd,
    Clusterer const* clusterer,
    int global_max_clusters,
    Configuration const& config,
    const std::string& prefix
  ) const {
    if (!config.visualize_full_graph_) {
      return;
    }
    FullGraphVisualizer<CommT> visualizer(comm, pd, clusterer, global_max_clusters, prefix);
    visualizer.run();
  }

  template <typename T>
  std::unordered_map<int, T> runInformationPropagation(T& initial_data) {
    InformationPropagation<CommT, T, TemperedLB<CommT>> ip(comm_, config_);
    auto gathered_info = ip.run(initial_data);
    VT_LB_LOG(normal, "gathered load info size={}\n", gathered_info.size());
    return gathered_info;
  }

  void run() {
    // Make communications symmetric before running trials so we only have to do it once
    makeCommunicationsSymmetric();

    for (int trial = 0; trial < config_.num_trials_; ++trial) {
      VT_LB_LOG(normal, "Starting trial {}/{}\n", trial + 1, config_.num_trials_);
      runTrial(trial);
      VT_LB_LOG(normal, "Finished trial {}/{}\n", trial + 1, config_.num_trials_);
    }
  }

  void runTrial(int trial) {
    // Save a clone of the phase data before load balancing
    savePhaseData();

    double total_load = computeLoad();
    computeStatistics(total_load, "Compute Load");

    double const total_work = WorkModelCalculator::computeWork(
      config_.work_model_,
      WorkModelCalculator::computeWorkBreakdown(this->getPhaseData(), config_)
    );
    computeStatistics(total_work, "Work");

    if (config_.hasMemoryInfo()) {
      double const total_memory_usage = WorkModelCalculator::computeMemoryUsage(
        config_,
        this->getPhaseData()
      ).current_memory_usage;
      computeStatistics(total_memory_usage, "Memory Usage");
    }

    // Run the clustering algorithm if appropriate for the configuration
    doClustering();

    // Generate visualization after clustering
    visualizeGraph("temperedlb_rank" + std::to_string(comm_.getRank()) + "_trial" + std::to_string(trial));

    visualizeFullGraphIfNeeded(
      comm_,
      this->getPhaseData(),
      getClusterer(),
      global_max_clusters_,
      config_,
      "temperedlb_full_graph_trial" + std::to_string(trial)
    );

    auto& wm = config_.work_model_;
    if (wm.beta == 0.0 && wm.gamma == 0.0 && wm.delta == 0.0) {
      auto info = runInformationPropagation(total_load);
      VT_LB_LOG(normal, "runTrial: gathered load info from {} ranks\n", info.size());
    } else {
#if 0
      computeGlobalMaxClusters();
#else
      // Just assume max of 1000 clusters per rank for now, until we have bcast
#endif
      // For now, we will assume that if beta/gamma/delta are non-zero, clustering must occur.
      // Every task could be its own cluster, but clusters must exist
      assert(clusterer_ != nullptr && "Clusterer must be valid");
      auto local_summary = buildClusterSummaries();
      auto info = runInformationPropagation(local_summary);
      VT_LB_LOG(normal, "runTrial: gathered load info from {} ranks\n", info.size());
    }

    // Before we restore phase data for the next trial, save the work and task distribution
    // @todo: for now, we recompute work from scratch but we probably can use the breakdown
    trial_work_distribution_.emplace_back(
      WorkModelCalculator::computeWork(
        config_.work_model_,
        WorkModelCalculator::computeWorkBreakdown(this->getPhaseData(), config_)
      ),
      this->getPhaseData().getTaskIds()
    );

    // Restore phase data
    restorePhaseData();
  }

  Clusterer const* getClusterer() const { return clusterer_.get(); }

private:
  void computeGlobalMaxClusters() {
    // compute max number of clusters on any rank
    int local_clusters = 0;
    if (clusterer_) {
      // assume Clusterer provides numClusters(); if not, set appropriately
      local_clusters = static_cast<int>(clusterer_->clusters().size());
    }

    int const root = 0;
    handle_.reduce(root, MPI_INT, MPI_MAX, &local_clusters, &global_max_clusters_, 1);

    if (comm_.getRank() == root) {
      VT_LB_LOG(normal, "global max clusters across ranks: {}\n", global_max_clusters_);
    }
    // @todo: once we have a bcast, broadcast global_max_clusters_ to all ranks
  }

  template <typename T>
  void computeStatistics(T quantity, std::string const& name) {
    // Compute min, max, avg of quantity across all ranks
    double local_value = static_cast<double>(quantity);
    double global_min = 0.0;
    double global_max = 0.0;
    double global_sum = 0.0;
    handle_.reduce(0, MPI_DOUBLE, MPI_MIN, &local_value, &global_min, 1);
    handle_.reduce(0, MPI_DOUBLE, MPI_MAX, &local_value, &global_max, 1);
    handle_.reduce(0, MPI_DOUBLE, MPI_SUM, &local_value, &global_sum, 1);
    double global_avg = global_sum / static_cast<double>(comm_.numRanks());
    double I = 0;
    if (global_avg > 0.0) {
      I = (global_max / global_avg) - 1.0;
    }
    if (comm_.getRank() == 0) {
      VT_LB_LOG(normal, "{} statistics -- min: {}, max: {}, avg: {}, I: {}\n",
                name, global_min, global_max, global_avg, I);
    }
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
  /// @brief Task distribution and work for each trial
  std::vector<std::tuple<double, std::unordered_set<model::TaskType>>> trial_work_distribution_;
};

} /* end namespace vt_lb::algo::temperedlb */

#undef VT_LB_LOG

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H*/
