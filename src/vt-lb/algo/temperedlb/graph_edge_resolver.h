/*
//@HEADER
// *****************************************************************************
//
//                              graph_edge_resolver.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_GRAPH_EDGE_RESOLVER_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_GRAPH_EDGE_RESOLVER_H

#include <vt-lb/model/PhaseData.h>
#include <vt-lb/model/Communication.h>
#include <vt-lb/model/Task.h>
#include <vt-lb/util/assert.h>

#include <unordered_map>
#include <vector>
#include <list>
#include <algorithm>

#define VALIDATE_RESOLVED_GRAPH_EDGES 0

namespace vt_lb::algo::temperedlb {

template <typename CommT>
struct GraphEdgeResolver {
  using ThisType = GraphEdgeResolver<CommT>;
  using HandleType = typename CommT::template HandleType<ThisType>;
  using PhaseData = vt_lb::model::PhaseData;
  using Edge = vt_lb::model::Edge;
  using TaskType = vt_lb::model::TaskType;
  using RankType = vt_lb::model::RankType;

  explicit GraphEdgeResolver(CommT& comm, PhaseData& pd)
    : comm_(comm.clone()), pd_(&pd)
  {
    handle_ = comm_.template registerInstanceCollective<ThisType>(this);
  }

  void run() {
    // first remove any communication where neither endpoint task exists
    pd_->purgeDanglingCommunications();

    std::list<Edge*> to_resolve;
    // resolve rank ID -> list of task IDs
    std::unordered_map<int, std::vector<model::TaskType>> rank_to_tasks;
    std::unordered_map<int, std::vector<model::TaskType>> resolve_tasks_requests;

    auto const num_ranks = comm_.numRanks();

    for (auto& e : pd_->getCommunicationsRef()) {
      auto from_task = pd_->getTask(e.getFrom());
      auto to_task = pd_->getTask(e.getTo());
      vt_lb_assert(
        from_task != nullptr || to_task != nullptr,
        "At least one endpoint task must exist after purge"
      );
      if (from_task == nullptr || to_task == nullptr) {
        if (from_task == nullptr) {
          resolve_tasks_requests[e.getFrom() % num_ranks].push_back(e.getFrom());
        } else {
          rank_to_tasks[e.getFrom() % num_ranks].push_back(e.getFrom());
        }
        if (to_task == nullptr) {
          resolve_tasks_requests[e.getTo() % num_ranks].push_back(e.getTo());
        } else {
          rank_to_tasks[e.getTo() % num_ranks].push_back(e.getTo());
        }

        to_resolve.push_back(&e);
      }
    }

    // Deduplicate per-rank task IDs
    for (auto& kv : rank_to_tasks) {
      auto& v = kv.second;
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
    }
    for (auto& kv : resolve_tasks_requests) {
      auto& v = kv.second;
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
    }

    for (auto& kv : rank_to_tasks) {
      int target_rank = kv.first;
      auto& task_ids = kv.second;
      if (!task_ids.empty()) {
        handle_[target_rank].template send<&ThisType::informTaskLocation>(comm_.getRank(), task_ids);
      }
    }

    for (auto& kv : resolve_tasks_requests) {
      int target_rank = kv.first;
      auto& task_ids = kv.second;
      if (!task_ids.empty()) {
        handle_[target_rank].template send<&ThisType::resolveTasks>(comm_.getRank(), task_ids);
      }
    }

    while (comm_.poll()) {
      // do nothing
    }

    for (auto e : to_resolve) {
      auto from_task = pd_->getTask(e->getFrom());
      auto to_task = pd_->getTask(e->getTo());
      vt_lb_assert(
        from_task != nullptr || to_task != nullptr,
        "At least one endpoint task must exist after purge"
      );
      if (from_task == nullptr) {
        auto it = resolved_tasks_.find(e->getFrom());
        vt_lb_assert(
          it != resolved_tasks_.end(),
          "From task must have been resolved"
        );
        e->setToRank(comm_.getRank());
        e->setFromRank(it->second);
      }
      if (to_task == nullptr) {
        auto it = resolved_tasks_.find(e->getTo());
        vt_lb_assert(
          it != resolved_tasks_.end(),
          "To task must have been resolved"
        );
        e->setToRank(it->second);
        e->setFromRank(comm_.getRank());
      }
    }

#if VALIDATE_RESOLVED_GRAPH_EDGES
    auto comm2 = comm_.clone();
    auto handle2 = comm2.template registerInstanceCollective<ThisType>(this);

    // Validate end points
    for (auto e : to_resolve) {
      auto from_task = pd_->getTask(e->getFrom());
      auto to_task = pd_->getTask(e->getTo());
      if (from_task == nullptr) {
        handle2[e->getFromRank()].template send<&ThisType::validateEndpoint>(
          comm_.getRank(),
          e->getFrom()
        );
      } else if (to_task == nullptr) {
        handle2[e->getToRank()].template send<&ThisType::validateEndpoint>(
          comm_.getRank(),
          e->getTo()
        );
      }
    }

    while (comm2.poll()) {
      // do nothing
    }
#endif
  }

  void returnTaskLocation(
    RankType rank,
    model::TaskType task_id
  ) {
    resolved_tasks_[task_id] = rank;
  }

  void informTaskLocation(
    int rank,
    std::vector<model::TaskType> const& task_ids
  ) {
    VT_LB_LOG(
      LoadBalancer, verbose,
      "GraphEdgeResolver::informTaskLocation: received {} task locations from rank {}\n",
      task_ids.size(), rank
    );

    for (auto const& tid : task_ids) {
      task_location_map_[tid] = rank;

      if (auto it = resolve_tasks_requests_.find(tid); it != resolve_tasks_requests_.end()) {
        for (auto const& req_rank : it->second) {
          handle_[req_rank].template send<&ThisType::returnTaskLocation>(rank, tid);
        }
        resolve_tasks_requests_.erase(it);
      }
    }
  }

  void resolveTasks(
    int rank,
    std::vector<model::TaskType> const& task_ids
  ) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "GraphEdgeResolver::resolveTasks: received {} task resolution requests from rank {}\n",
      task_ids.size(), rank
    );

    for (auto const& tid : task_ids) {
      if (auto it = task_location_map_.find(tid); it != task_location_map_.end()) {
        handle_[rank].template send<&ThisType::returnTaskLocation>(it->second, tid);
      } else {
        resolve_tasks_requests_[tid].push_back(rank);
      }
    }
  }

  void validateEndpoint(
    int rank,
    model::TaskType task_id
  ) {
    VT_LB_LOG(
      LoadBalancer, normal,
      "GraphEdgeResolver::validateEndpoint: received validation request for task {} from rank {}\n",
      task_id, rank
    );

    auto task = pd_->getTask(task_id);
    vt_lb_assert(
      task != nullptr,
      "Validated task must exist locally"
    );
  }

private:
  CommT comm_;
  PhaseData* pd_ = nullptr;
  HandleType handle_;
  std::unordered_map<model::TaskType, int> task_location_map_;
  std::unordered_map<model::TaskType, std::vector<int>> resolve_tasks_requests_;
  std::unordered_map<model::TaskType, int> resolved_tasks_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_GRAPH_EDGE_RESOLVER_H*/