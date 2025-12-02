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

#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cassert>

namespace vt_lb::algo::temperedlb {

struct TaskClusterSummaryInfo {
  TaskClusterSummaryInfo() = default;

  int cluster_id = -1;
  int num_tasks_ = 0;
  double cluster_load = 0.0;
  double cluster_intra_send_bytes = 0.0;
  double cluster_intra_recv_bytes = 0.0;
  std::vector<model::Edge> inter_edges_;

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

struct Clusterer;

struct ClusterSummarizer {
  /**
   * @brief Build cluster summaries from phase data and a clusterer
   *
   * @param[in] pd Phase data
   * @param[in] clusterer_ Clusterer instance
   * @param[in] global_max_clusters Global maximum number of clusters
   *
   * @return Map from global cluster ID to summary info
   */
  static std::unordered_map<int, TaskClusterSummaryInfo>
  buildClusterSummaries(
    model::PhaseData const& pd,
    Clusterer const* clusterer_,
    int global_max_clusters
);

  static inline int localToGlobalClusterID(int cluster_id, int rank, int global_max_clusters) {
    // Map local cluster IDs to global cluster IDs based on global_max_clusters_
    // Implementation depends on how clusters are represented and communicated
    return cluster_id + rank * global_max_clusters;
  }

  static inline int globalToLocalClusterID(int global_cluster_id, int global_max_clusters) {
    return global_cluster_id % global_max_clusters;
  }

  static inline int globalClusterToRank(int global_cluster_id, int global_max_clusters) {
    return global_cluster_id / global_max_clusters;
  }
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_CLUSTER_SUMMARIZER_H*/