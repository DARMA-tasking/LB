/*
//@HEADER
// *****************************************************************************
//
//                                 transfer.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_H

#include <vt-lb/model/types.h>
#include <vt-lb/comm/comm_traits.h>
#include <vt-lb/util/logging.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/statistics.h>
#include <vt-lb/algo/temperedlb/transfer_util.h>

#include <unordered_map>
#include <cassert>

namespace vt_lb::algo::temperedlb {

template <comm::Communicator CommT>
struct Transferer {
  using HandleType = typename CommT::template HandleType<Transferer<CommT>>;

  Transferer(CommT& comm, model::PhaseData& pd)
    : comm_(comm.clone()),
      handle_(comm_.template registerInstanceCollective<Transferer<CommT>>(this)),
      pd_(pd)
  {}

  virtual ~Transferer() = default;

  void migrateTask(int const rank, model::Task const& task, [[maybe_unused]] bool include_edges = false) {
    migrate_tasks_[rank].insert(task);
    pd_.eraseTask(task.getId());
  }

  void doMigrations() {
    for (auto&& [rank, tasks] : migrate_tasks_) {
      handle_[rank].template send<&Transferer::migrationHandler>(comm_.getRank(), tasks);
    }

    // Wait for termination on the migrations
    while (comm_.poll()) {
      // do nothing
    }
    migrate_tasks_.clear();
  }

  void sendBackHandler(model::Task const& task) {
    pd_.addTask(task);
  }

  void migrationHandler(int from_rank, std::unordered_set<model::Task> tasks) {
    for (auto& task : tasks) {
      if (!acceptIncomingTask(task)) {
        VT_LB_LOG(
          LoadBalancer, normal,
          "Transferer::migrationHandler: rejecting incoming task {}, load {} due to load constraints\n",
          task.getId(),
          task.getLoad()
        );
        handle_[from_rank].template send<&Transferer::sendBackHandler>(task);
      } else {
        pd_.addTask(task);
      }
    }
  }

  virtual bool acceptIncomingTask(model::Task const& task) = 0;

protected:
  CommT comm_;
  HandleType handle_;
  model::PhaseData& pd_;
  std::unordered_map<int, std::unordered_set<model::Task>> migrate_tasks_;
  std::unordered_map<model::TaskType, std::vector<model::Edge>> migrate_task_edges_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_H*/