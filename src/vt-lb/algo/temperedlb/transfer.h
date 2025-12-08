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
};

template <comm::Communicator CommT>
struct BasicTransfer final : Transferer<CommT> {
  BasicTransfer(
    CommT& comm,
    model::PhaseData& pd,
    std::unordered_map<int, RankInfo> const& load_info,
    Statistics stats
  ) : Transferer<CommT>(comm, pd),
      load_info_(load_info),
      stats_(stats)
  {}

  bool isOverloaded(model::LoadType load) const {
    return load > stats_.avg;
  }

  void run(
    TransferUtil::CMFType cmf_type,
    TransferUtil::ObjectOrder obj_ordering,
    CriterionEnum criterion,
    bool deterministic,
    model::LoadType target_max_load,
    std::mt19937 gen_sample_,
    std::random_device& seed_
  ) {
    RankInfo& cur_load = load_info_.at(this->pd_.getRank());

    // Initialize transfer and rejection counters
    int n_transfers = 0, n_rejected = 0;

    // Try to migrate objects only from overloaded ranks
    if (isOverloaded(cur_load.getScaledLoad())) {
      std::vector<int> under = TransferUtil::makeUnderloaded(deterministic, load_info_, stats_.avg);
      std::unordered_map<int, model::TaskType> migrate_tasks;

      auto cur_tasks = this->pd_.getTasksMap();

      if (under.size() > 0) {
        std::vector<model::TaskType> ordered_obj_ids = TransferUtil::orderObjects(
          obj_ordering, cur_tasks, cur_load, target_max_load
        );

        // Iterate through all the objects
        for (auto iter = ordered_obj_ids.begin(); iter != ordered_obj_ids.end(); ) {
          auto obj_id = *iter;
          auto obj_load = cur_tasks[obj_id].getLoad();

          if (cmf_type == TransferUtil::CMFType::Original) {
            // Rebuild the relaxed underloaded set based on updated load of this node
            under = TransferUtil::makeUnderloaded(deterministic, load_info_, stats_.avg);
            if (under.size() == 0) {
              break;
            }
          } else if (cmf_type == TransferUtil::CMFType::NormByMaxExcludeIneligible) {
            // Rebuild the underloaded set and eliminate processors that will
            // fail the Criterion for this object
            under = TransferUtil::makeSufficientlyUnderloaded(
              deterministic, load_info_, criterion, cur_load, target_max_load, obj_load
            );
            if (under.size() == 0) {
              ++n_rejected;
              iter++;
              continue;
            }
          }
          // Rebuild the CMF with the new loads taken into account
          auto cmf = TransferUtil::createCMF(cmf_type, load_info_, target_max_load, under);

          // Select a node using the CMF
          auto const selected_rank = TransferUtil::sampleFromCMF(deterministic, under, cmf, gen_sample_, seed_);

          VT_LB_LOG(
            LoadBalancer, verbose,
            "TemperedLB::originalTransfer: selected_node={}, load_info_.size()={}\n",
            selected_rank, load_info_.size()
          );

          // Find load of selected node
          auto load_iter = load_info_.find(selected_rank);
          assert(load_iter != load_info_.end() && "Selected node not found");
          auto& selected_load = load_iter->second;

          // Check if object is migratable and evaluate criterion for proposed transfer
          bool is_migratable = cur_tasks[obj_id].isMigratable();
          bool eval = Criterion(criterion)(
            cur_load, selected_load, obj_load, target_max_load
          );

          VT_LB_LOG(
            LoadBalancer, verbose,
            "TemperedLB::originalTransfer: under.size()={}, "
            "selected_node={}, selected_load={}, obj_id={}, "
            "is_migratable()={}, obj_load={}, target_max_load={}, "
            "cur_load={}, criterion={}\n",
            under.size(),
            selected_rank,
            selected_load,
            obj_id,
            is_migratable,
            obj_load,
            target_max_load,
            cur_load,
            eval
          );

          // Decide about proposed migration based on criterion evaluation
          if (is_migratable && eval) {
            ++n_transfers;
            // Transfer the object load in seconds
            // to match the object load units on the receiving end
            this->migrateTask(selected_rank, cur_tasks[obj_id]);

            VT_LB_LOG(
              LoadBalancer, verbose,
              "TemperedLB::decide: migrating obj_id={:x} of load={} to rank={}\n",
              obj_id, model::LoadType(obj_load), selected_rank
            );

            // Update loads
            cur_load.load -= obj_load;
            selected_load.load += obj_load;

            iter = ordered_obj_ids.erase(iter);
            cur_tasks.erase(obj_id);
          } else {
            ++n_rejected;
            iter++;
          }

          if (!(cur_load.getScaledLoad() > target_max_load)) {
            break;
          }
        }
      }
    } else {
      // do nothing (underloaded-based algorithm), waits to get work from
      // overloaded nodes
    }

    this->doMigrations();
  }

  /*virutal*/ bool acceptIncomingTask(model::Task const& task) override final {
    RankInfo& cur_load = load_info_.at(this->pd_.getRank());
    VT_LB_LOG(
      LoadBalancer, verbose,
      "BasicTransfer::acceptIncomingTask: current load={} task load={} total load={} max={}\n",
      cur_load.load, task.getLoad(), cur_load.load + task.getLoad(), stats_.max
    );
    if ((cur_load.load + task.getLoad()) * cur_load.rank_alpha > stats_.max) {
      return false;
    } else {
      cur_load.load += task.getLoad();
      return true;
    }
  }

private:
  std::unordered_map<int, RankInfo> load_info_;
  Statistics stats_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_H*/