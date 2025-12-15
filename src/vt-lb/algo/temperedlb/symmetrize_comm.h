/*
//@HEADER
// *****************************************************************************
//
//                                 symmetrize_comm.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_SYMMETRIZE_COMM_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_SYMMETRIZE_COMM_H

#include <vt-lb/config/cmake_config.h>

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Communication.h>

#include <unordered_set>
#include <vector>
#include <utility>
#include <unordered_map> // added

namespace vt_lb::algo::temperedlb {

template <typename CommT>
struct CommunicationsSymmetrizer {
  using ThisType = CommunicationsSymmetrizer<CommT>;
  using HandleType = typename CommT::template HandleType<ThisType>;
  using PhaseData = vt_lb::model::PhaseData;
  using Edge = vt_lb::model::Edge;
  using TaskType = vt_lb::model::TaskType;
  using RankType = vt_lb::model::RankType;

  explicit CommunicationsSymmetrizer(CommT& comm, PhaseData& pd)
    : comm_(comm.clone()), pd_(&pd)
  {
    handle_ = comm_.template registerInstanceCollective<ThisType>(this);
  }

  void run() {
    auto const my_rank = pd_->getRank();
    auto comms = pd_->getCommunications();

    // Remove local bidirectional enforcement; only synchronize with remote ranks

    // Batch edges per remote rank
    std::unordered_map<RankType, std::vector<Edge>> rank_batches;

    for (auto const& e : comms) {
      RankType fr = e.getFromRank();
      RankType tr = e.getToRank();

      if (tr != model::invalid_node && tr != my_rank) {
        rank_batches[tr].push_back(e);
      }
      if (fr != model::invalid_node && fr != my_rank && fr != tr) {
        rank_batches[fr].push_back(e);
      }
    }

    bool has_sends_ = false;

    // Send one message per remote rank
    for (auto& kv : rank_batches) {
      auto dest = kv.first;
      has_sends_ = true;
      handle_[dest].template send<&ThisType::recvEdgesHandler>(kv.second);
    }

    // Drain progress
    while (has_sends_ && comm_.poll()) {
      // do nothing
    }

    VT_LB_LOG(
      LoadBalancer, normal,
      "completed symmetrization of communications\n"
    );
  }

private:
  bool hasEdge(TaskType f, TaskType t) const {
    for (auto const& e : pd_->getCommunications()) {
      if (e.getFrom() == f && e.getTo() == t) return true;
    }
    return false;
  }

  // Add edge only if not already present locally; do not add reverse
  void addIfMissingLocal(Edge const& e) {
    if (!hasEdge(e.getFrom(), e.getTo())) {
      pd_->addCommunication(e);
    }
  }

  void recvEdgesHandler(std::vector<Edge> edges) {
    for (auto const& e : edges) {
      addIfMissingLocal(e);
    }
  }

private:
  CommT comm_;
  PhaseData* pd_ = nullptr;
  HandleType handle_;
};

} // namespace vt_lb::algo::temperedlb

#endif // INCLUDED_VT_LB_ALGO_TEMPEREDLB_SYMMETRIZE_COMM_H