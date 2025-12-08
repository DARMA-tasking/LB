/*
//@HEADER
// *****************************************************************************
//
//                               configuration.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_CONFIGURATION_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_CONFIGURATION_H

#include <vt-lb/algo/temperedlb/work_model.h>
#include <vt-lb/algo/temperedlb/transfer_util.h>

#include <cmath>

namespace vt_lb::algo::temperedlb {

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

  /// @brief  Criterion type for transfer decisions
  CriterionEnum criterion_ = CriterionEnum::Grapevine;
  /// @brief  Object ordering for transfer decisions
  TransferUtil::ObjectOrder obj_ordering_ = TransferUtil::ObjectOrder::ElmID;
  /// @brief  CMF type for transfer decisions
  TransferUtil::CMFType cmf_type_ = TransferUtil::CMFType::Original;

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
  /// @brief Whether to output full graph visualization: this is expensive
  bool visualize_full_graph_ = false;

  /// @brief Tolerance for convergence
  double converge_tolerance_ = 0.01;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_CONFIGURATION_H*/