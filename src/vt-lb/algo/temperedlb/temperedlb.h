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

// Include all model types
#include <vt-lb/model/types.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Task.h>
#include <vt-lb/model/Communication.h>
#include <vt-lb/model/SharedBlock.h>

// Include various temperedlb components
#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/algo/temperedlb/clustering.h>
#include <vt-lb/algo/temperedlb/symmetrize_comm.h>
#include <vt-lb/algo/temperedlb/visualize.h>
#include <vt-lb/algo/temperedlb/cluster_summarizer.h>
#include <vt-lb/algo/temperedlb/full_graph_visualizer.h>
#include <vt-lb/algo/temperedlb/info_propagation.h>
#include <vt-lb/algo/temperedlb/transfer.h>
#include <vt-lb/algo/temperedlb/basic_transfer.h>
#include <vt-lb/algo/temperedlb/relaxed_cluster_transfer.h>
#include <vt-lb/algo/temperedlb/statistics.h>

// Logging include
#include <vt-lb/util/logging.h>

#include <limits>
#include <random>
#include <ostream>
#include <fstream>
#include <cassert>

#include <mpi.h>

namespace vt_lb::algo::temperedlb {

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
    ClusterSummarizer<CommT> cs(comm_, getClusterer(), global_max_clusters_);
    return cs.buildClusterSummaries(
      this->getPhaseData(), config_
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
    InformationPropagation<CommT, T> ip(comm_, config_);
    auto gathered_info = ip.run(initial_data);
    VT_LB_LOG(LoadBalancer, normal, "gathered load info size={}\n", gathered_info.size());
    return gathered_info;
  }

  std::unordered_set<model::TaskType> run() {
    // Make communications symmetric before running trials so we only have to do it once
    makeCommunicationsSymmetric();

    for (int trial = 0; trial < config_.num_trials_; ++trial) {
      VT_LB_LOG(LoadBalancer, normal, "Starting trial {}/{}\n", trial + 1, config_.num_trials_);
      runTrial(trial);
      VT_LB_LOG(LoadBalancer, normal, "Finished trial {}/{}\n", trial + 1, config_.num_trials_);
    }

    // Sort trial work distribution by max work
    std::sort(
      trial_work_distribution_.begin(),
      trial_work_distribution_.end(),
      [](auto const& a, auto const& b) {
        return std::get<0>(a) < std::get<0>(b);
      }
    );

    VT_LB_LOG(
      LoadBalancer, normal,
      "Best trial: max work = {}\n", std::get<0>(trial_work_distribution_.front())
    );

    return std::get<1>(trial_work_distribution_.front());
  }

  void runTrial(int trial) {
    // Save a clone of the phase data before load balancing
    savePhaseData();

    for (int iter = 0; iter < config_.num_iters_; ++iter) {
      VT_LB_LOG(LoadBalancer, normal, "  Starting iteration {}/{}\n", iter + 1, config_.num_iters_);
      runIteration(trial, iter);
      VT_LB_LOG(LoadBalancer, normal, "  Finished iteration {}/{}\n", iter + 1, config_.num_iters_);
    }

    // Before we restore phase data for the next trial, save the work and task distribution
    // @todo: for now, we recompute work from scratch but we probably can use the breakdown
    auto after_iters_work = WorkModelCalculator::computeWork(
      config_.work_model_,
      WorkModelCalculator::computeWorkBreakdown(this->getPhaseData(), config_)
    );
    auto final_stats = computeStatistics(after_iters_work, "Final Work After Iters");

    // Save the max work and task distribution for this trial
    trial_work_distribution_.emplace_back(
      final_stats.max,
      this->getPhaseData().getTaskIds()
    );

    // Restore phase data
    restorePhaseData();
  }

  void runIteration(int trial, int iter) {
    double total_load = computeLoad();
    auto load_stats = computeStatistics(total_load, "Compute Load");

    auto work_breakdown = WorkModelCalculator::computeWorkBreakdown(
      this->getPhaseData(), config_
    );
    double const total_work = WorkModelCalculator::computeWork(
      config_.work_model_, work_breakdown
    );
    auto work_stats = computeStatistics(total_work, "Work");

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
    visualizeGraph(
      "temperedlb_rank" + std::to_string(comm_.getRank()) +
      "_trial" + std::to_string(trial) +
      "_iter" + std::to_string(iter)
    );

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
      auto rank_info = RankInfo{total_work, config_.work_model_.rank_alpha};
      auto info = runInformationPropagation(rank_info);
      VT_LB_LOG(LoadBalancer, normal, "runTrial: gathered load info from {} ranks\n", info.size());
      BasicTransfer<CommT> transfer(comm_, *phase_data_, info, work_stats);
      std::mt19937 gen_select_;
      std::random_device seed_;
      transfer.run(
        config_.cmf_type_,
        config_.obj_ordering_,
        config_.criterion_,
        config_.deterministic_,
        load_stats.avg,
        gen_select_,
        seed_
      );
      double const after_work = WorkModelCalculator::computeWork(
        config_.work_model_,
        WorkModelCalculator::computeWorkBreakdown(this->getPhaseData(), config_)
      );
      computeStatistics(after_work, "After Work");
    } else {
      // computeGlobalMaxClusters();
      // Just assume max of 1000 clusters per rank for now, until we have bcast

      // For now, we will assume that if beta/gamma/delta are non-zero, clustering must occur.
      // Every task could be its own cluster, but clusters must exist
      assert(clusterer_ != nullptr && "Clusterer must be valid");
      auto local_summary = buildClusterSummaries();
      auto rank_info = RankClusterInfo{
        local_summary,
        this->getPhaseData().getRankFootprintBytes(),
        config_.work_model_.rank_alpha,
        work_breakdown,
        this->getPhaseData().getSharedBlockIdsHomed()
      };
      auto info = runInformationPropagation(rank_info);

      VT_LB_LOG(
        LoadBalancer, normal,
        "runTrial: gathered load info from {} ranks\n",
        info.size()
      );

      RelaxedClusterTransfer<CommT> transfer(
        comm_, *phase_data_, clusterer_.get(), global_max_clusters_, info, work_stats
      );
      transfer.run(config_);
    }
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
      VT_LB_LOG(LoadBalancer, normal, "global max clusters across ranks: {}\n", global_max_clusters_);
    }
    // @todo: once we have a bcast, broadcast global_max_clusters_ to all ranks
  }

  template <typename T>
  Statistics computeStatistics(T quantity, std::string const& name) {
    // Compute min, max, avg of quantity across all ranks
    double local_value = static_cast<double>(quantity);
    double global_min = 0.0;
    double global_max = 0.0;
    double global_sum = 0.0;
    // For now, do P reductions since we don't have broadcast yet
    for (int p = 0; p < comm_.numRanks(); ++p) {
      handle_.reduce(p, MPI_DOUBLE, MPI_MIN, &local_value, &global_min, 1);
      handle_.reduce(p, MPI_DOUBLE, MPI_MAX, &local_value, &global_max, 1);
      handle_.reduce(p, MPI_DOUBLE, MPI_SUM, &local_value, &global_sum, 1);
    }
    double global_avg = global_sum / static_cast<double>(comm_.numRanks());
    double I = 0;
    if (global_avg > 0.0) {
      I = (global_max / global_avg) - 1.0;
    }
    if (comm_.getRank() == 0) {
      VT_LB_LOG(
        LoadBalancer, normal, "{} statistics -- min: {}, max: {}, avg: {}, I: {}\n",
        name, global_min, global_max, global_avg, I
      );
    }
    return Statistics{global_min, global_max, global_avg, I};
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

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H*/
