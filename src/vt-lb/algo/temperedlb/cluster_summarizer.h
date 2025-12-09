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

struct ClusterSummarizer {
  /**
   * @brief Build cluster summaries from phase data and a clusterer
   *
   * @param[in] pd Phase data
   * @param[in] clusterer_ Clusterer instance
   * @param[in] global_max_clusters Global maximum number of clusters
   * @param[in] config Configuration object
   *
   * @return Map from global cluster ID to summary info
   */
  static std::unordered_map<int, TaskClusterSummaryInfo>
  buildClusterSummaries(
    model::PhaseData const& pd,
    Configuration const& config,
    Clusterer const* clusterer_,
    int global_max_clusters
);

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

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_H*/