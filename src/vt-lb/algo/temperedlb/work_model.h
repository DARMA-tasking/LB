/*
//@HEADER
// *****************************************************************************
//
//                                work_model.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_WORK_MODEL_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_WORK_MODEL_H

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Task.h>
#include <vt-lb/algo/temperedlb/task_cluster_summary_info.h>

#include <vector>

namespace vt_lb::algo::temperedlb {

struct Configuration;
struct Clusterer;
struct TaskClusterSummaryInfo;
struct RankClusterInfo;

/**
 * @struct WorkModel
 *
 * @brief Parameters and methods for computing task work
 */
struct WorkModel {
  /// @brief  Coefficient for load component (per rank)
  double rank_alpha = 1.0;
  /// @brief  Coefficient for inter-node communication component
  double beta = 0.0;
  /// @brief  Coefficient for intra-node communication component
  double gamma = 0.0;
  /// @brief  Coefficient for shared-memory off-home communication component
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

  /**
   * @brief Apply the work formula to compute the work of a task
   *
   * @param[in] compute The compute component
   * @param[in] inter_comm_bytes The inter-node communication bytes
   * @param[in] intra_comm_bytes The intra-node communication bytes
   * @param[in] shared_comm_bytes The shared-memory communication bytes
   *
   * @return The computed work value
   */
  inline double applyWorkFormula(
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

/**
 * @struct Memory breakdown (for incremental updates)
 *
 * @brief Breakdown of memory components for a task distribution
 */
struct MemoryBreakdown {
  /// @brief Current memory usage
  double current_memory_usage = 0.0;
  /// @brief Current maximum task working memory usage
  double current_max_task_working_bytes = 0.0;
  /// @brief Current maximum task serialized memory usage
  double current_max_task_serialized_bytes = 0.0;

  template <typename SerializerT>
  void serialize(SerializerT& s) {
    s | current_memory_usage;
    s | current_max_task_working_bytes;
    s | current_max_task_serialized_bytes;
  }
};

/**
 * @struct WorkBreakdown (for incremental updates)
 *
 * @brief Breakdown of work components for a task distribution
 */
struct WorkBreakdown {
  /// @brief Compute component
  double compute = 0.0;
  /// @brief Inter-node receive communication component
  double inter_node_recv_comm = 0.0;
  /// @brief Inter-node send communication component
  double inter_node_send_comm = 0.0;
  /// @brief Intra-node receive communication component
  double intra_node_recv_comm = 0.0;
  /// @brief Intra-node send communication component
  double intra_node_send_comm = 0.0;
  /// @brief Shared-memory communication component
  double shared_mem_comm = 0.0;
  /// @brief Memory breakdown
  MemoryBreakdown memory_breakdown;

  template <typename SerializerT>
  void serialize(SerializerT& s) {
    s | compute;
    s | inter_node_recv_comm;
    s | inter_node_send_comm;
    s | intra_node_recv_comm;
    s | intra_node_send_comm;
    s | shared_mem_comm;
    s | memory_breakdown;
  }
};

/**
 * @struct WorkModelCalculator
 *
 * @brief Calculator for computing work from scratch or incrementally
 * calculating from a breakdown
 */
struct WorkModelCalculator {
  /**
   * @brief Compute the work breakdown for the given phase data
   *
   * @param phase_data The phase data
   * @param config The configuration
   *
   * @return The work breakdown
   */
  static WorkBreakdown computeWorkBreakdown(
    model::PhaseData const& phase_data,
    Configuration const& config
  );

  /**
   * @brief Compute the work given a work model and breakdown
   *
   * @param model The work model
   * @param breakdown The work breakdown
   *
   * @return The computed work value
   */
  static double computeWork(
    WorkModel const& model,
    WorkBreakdown const& breakdown
  );

  /**
   * @brief Compute the new work after adding/removing tasks and communications
   *
   * @param phase_data The phase data
   * @param model The work model
   * @param breakdown The current work breakdown
   * @param to_add The tasks to add
   * @param to_add_edges The communication edges to add
   * @param to_remove The task IDs to remove
   *
   * @return The new computed work value
   */
  static double computeWorkUpdate(
    model::PhaseData const& phase_data,
    WorkModel const& model,
    WorkBreakdown breakdown,
    std::vector<model::Task> const& to_add,
    std::vector<model::Edge> to_add_edges,
    std::vector<model::TaskType> const& to_remove
  );

  static double computeWorkUpdateSummary(
    WorkModel const& model,
    RankClusterInfo rank_cluster_info,
    TaskClusterSummaryInfo to_add,
    TaskClusterSummaryInfo to_remove
  );

  /**
   * @brief Compute the memory usage for the given phase data
   *
   * @param config The configuration
   * @param phase_data The phase data
   *
   * @return The computed memory usage with breakdown
   */
  static MemoryBreakdown computeMemoryUsage(
    Configuration const& config,
    model::PhaseData const& phase_data
  );

  /**
   * @brief Check if the memory usage fits within available memory after updates
   *
   * @param config The configuration
   * @param phase_data The phase data
   * @param clusterer The clusterer
   * @param global_max_clusters The global maximum number of clusters
   * @param breakdown The current work breakdown
   * @param to_add The cluster of tasks to add
   * @param to_remove The cluster of tasks to remove
   *
   * @return True if it fits, false otherwise
   */
  static bool checkMemoryFitUpdate(
    Configuration const& config,
    model::PhaseData const& phase_data,
    Clusterer const& clusterer,
    int global_max_clusters,
    WorkBreakdown const& breakdown,
    TaskClusterSummaryInfo to_add,
    TaskClusterSummaryInfo to_remove
  );

  /**
   * @brief Check if the memory usage fits within available memory
   *
   * @param config The configuration
   * @param phase_data The phase data
   * @param total_memory_usage The total memory usage to check
   *
   * @return True if it fits, false otherwise
   */
  static bool checkMemoryFit(
    Configuration const& config,
    model::PhaseData const& phase_data,
    double total_memory_usage
  );

};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_WORK_MODEL_H*/