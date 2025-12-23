/*
//@HEADER
// *****************************************************************************
//
//                             cluster_summarizer.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_H

#include <vt-lb/config/cmake_config.h>

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Communication.h>
#include <vt-lb/algo/temperedlb/configuration.h>
#include <vt-lb/algo/temperedlb/task_cluster_summary_info.h>

#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cassert>

namespace vt_lb::algo::temperedlb {

struct Clusterer;

struct ClusterSummarizerUtil {
  /**
   * @brief Convert local cluster ID to global cluster ID
   *
   * @param[in] cluster_id Local cluster ID
   * @param[in] rank This rank
   * @param[in] global_max_clusters Maximum number of clusters on any rank
   *
   * @return Global cluster ID
   */
  static inline int localToGlobalClusterID(int cluster_id, int rank, int global_max_clusters) {
    // Map local cluster IDs to global cluster IDs based on global_max_clusters_
    // Implementation depends on how clusters are represented and communicated
    return cluster_id + rank * global_max_clusters;
  }

  /**
   * @brief Convert global cluster ID to local cluster ID
   *
   * @param[in] global_cluster_id Global cluster ID
   * @param[in] global_max_clusters Maximum number of clusters on any rank
   *
   * @return Local cluster ID
   */
  static inline int globalToLocalClusterID(int global_cluster_id, int global_max_clusters) {
    return global_cluster_id % global_max_clusters;
  }

  /**
   * @brief Convert global cluster ID to rank
   *
   * @param[in] global_cluster_id Global cluster ID
   * @param[in] global_max_clusters Maximum number of clusters on any rank
   *
   * @return Rank
   */
  static inline int globalClusterToRank(int global_cluster_id, int global_max_clusters) {
    return global_cluster_id / global_max_clusters;
  }
};

template <typename CommT>
struct ClusterSummarizer : ClusterSummarizerUtil {
  using HandleType = typename CommT::template HandleType<ClusterSummarizer<CommT>>;

  ClusterSummarizer(
    CommT& comm,
    Clusterer const* clusterer,
    int global_max_clusters
  ) : comm_(comm.clone()),
      handle_(comm_.template registerInstanceCollective<ClusterSummarizer<CommT>>(this)),
      clusterer_(clusterer),
      global_max_clusters_(global_max_clusters)
  {}

  /**
   * @brief Build cluster summaries from phase data and a clusterer
   *
   * @param[in] pd Phase data
   * @param[in] config Configuration object
   *
   * @return Map from global cluster ID to summary info
   */
  std::unordered_map<int, TaskClusterSummaryInfo>
  buildClusterSummaries(
    model::PhaseData const& pd,
    Configuration const& config
  );

  /**
   * @brief Resolve cluster ID for a given task
   *
   * @param[in] from_rank Rank from which the request originated
   * @param[in] task_id Task ID
   * @param[in] source_task_id Source task ID (for inter-cluster edges)
   * @param[in] source_global_cluster_id Source global cluster ID
   */
  void resolveClusterIDForTask(
    int from_rank,
    model::TaskType task_id,
    model::TaskType source_task_id,
    int source_global_cluster_id
  );

  /**
   * @brief Handler receiving cluster ID for a task
   *
   * @param[in] task_id Task ID
   * @param[in] global_cluster_id Global cluster ID
   */
  void recvClusterIDForTask(
    model::TaskType task_id,
    int global_cluster_id
  );

private:
  CommT comm_;
  HandleType handle_;
  Clusterer const* clusterer_ = nullptr;
  int global_max_clusters_ = 1000;
  std::unordered_map<model::TaskType, int> task_to_global_cluster_id_;
};

} /* end namespace vt_lb::algo::temperedlb */

#include <vt-lb/algo/temperedlb/cluster_summarizer.impl.h>

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_H*/