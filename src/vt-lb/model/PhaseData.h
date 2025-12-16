/*
//@HEADER
// *****************************************************************************
//
//                                PhaseData.h
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
// * Redistributions in binary form, must reproduce the above copyright notice,
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

#if !defined INCLUDED_VT_LB_MODEL_PHASE_DATA_H
#define INCLUDED_VT_LB_MODEL_PHASE_DATA_H

#include "types.h"
#include "SharedBlock.h"
#include "Task.h"
#include "Communication.h"

#include <unordered_map>
#include <vector>

namespace vt_lb::model {

struct PhaseData {
  PhaseData() = default;
  explicit PhaseData(RankType rank) : rank_(rank) {}

  void addTask(Task const& t) { tasks_.emplace(t.getId(), t); }
  void addCommunication(Edge const& e) {
    for (int i = 0; i < static_cast<int>(communications_.size()); ++i) {
      auto& comm = communications_[i];
      if (comm.getFrom() == e.getFrom() && comm.getTo() == e.getTo()) {
        comm = e;
        return;
      }
    }
    communications_.push_back(e);
  }
  void addSharedBlock(SharedBlock const& b) { shared_blocks_.emplace(b.getId(), b); }

  RankType getRank() const { return rank_; }

  Task const* getTask(TaskType id) const {
    auto it = tasks_.find(id);
    return it != tasks_.end() ? &it->second : nullptr;
  }
  Task * getTask(TaskType id) {
    auto it = tasks_.find(id);
    return it != tasks_.end() ? &it->second : nullptr;
  }
  bool hasTask(TaskType id) const { return tasks_.find(id) != tasks_.end(); }
  void eraseTask(TaskType id) { tasks_.erase(id); }

  SharedBlock const* getSharedBlock(SharedBlockType id) const {
    auto it = shared_blocks_.find(id);
    return it != shared_blocks_.end() ? &it->second : nullptr;
  }
  SharedBlock * getSharedBlock(SharedBlockType id) {
    auto it = shared_blocks_.find(id);
    return it != shared_blocks_.end() ? &it->second : nullptr;
  }
  bool hasSharedBlock(SharedBlockType id) const { return shared_blocks_.find(id) != shared_blocks_.end(); }
  void eraseSharedBlock(SharedBlockType id) { shared_blocks_.erase(id); }

  std::unordered_map<TaskType, Task> const& getTasksMap() const { return tasks_; }
  std::vector<Edge> const& getCommunications() const { return communications_; }
  std::vector<Edge>& getCommunicationsRef() { return communications_; }
  std::unordered_map<SharedBlockType, SharedBlock> const& getSharedBlocksMap() const { return shared_blocks_; }
  std::unordered_set<TaskType> getTaskIds() const {
    std::unordered_set<TaskType> ids;
    for (auto const& [id, task] : tasks_) {
      ids.insert(id);
    }
    return ids;
  }
  std::unordered_set<SharedBlockType> getSharedBlockIds() const {
    std::unordered_set<SharedBlockType> ids;
    for (auto const& [id, block] : shared_blocks_) {
      ids.insert(id);
    }
    return ids;
  }
  std::unordered_set<SharedBlockType> getSharedBlockIdsHomed() const {
    std::unordered_set<SharedBlockType> ids;
    for (auto const& [id, sb] : shared_blocks_) {
      if (sb.getHome() == rank_) {
        ids.insert(id);
      }
    }
    return ids;
  }

  BytesType getRankFootprintBytes() const { return rank_footprint_bytes_; }
  void setRankFootprintBytes(BytesType bytes) { rank_footprint_bytes_ = bytes; }

  BytesType getRankMaxMemoryAvailable() const { return rank_max_memory_available_; }
  void setRankMaxMemoryAvailable(BytesType bytes) { rank_max_memory_available_ = bytes; }

  void clear() {
    tasks_.clear();
    communications_.clear();
    shared_blocks_.clear();
    rank_footprint_bytes_ = 0.0;
    rank_max_memory_available_ = 0.0;
  }

  void purgeDanglingCommunications() {
    std::vector<Edge> filtered;
    filtered.reserve(communications_.size());
    for (auto const& e : communications_) {
      bool from_present = hasTask(e.getFrom());
      bool to_present   = hasTask(e.getTo());
      // Keep edge unless both sides are missing
      if (from_present || to_present) {
        filtered.push_back(e);
      }
    }
    communications_.swap(filtered);
  }

  template <typename Serializer>
  void serialize(Serializer& s) {
    s | rank_;
    s | tasks_;
    s | communications_;
    s | shared_blocks_;
    s | rank_footprint_bytes_;
    s | rank_max_memory_available_;
  }

private:
  RankType rank_ = invalid_rank;
  std::unordered_map<TaskType, Task> tasks_;
  std::vector<Edge> communications_;
  std::unordered_map<SharedBlockType, SharedBlock> shared_blocks_;
  BytesType rank_footprint_bytes_ = 0.0;
  BytesType rank_max_memory_available_ = 0.0;
};

} /* end namespace vt_lb::model */

#endif /*INCLUDED_VT_LB_MODEL_PHASE_DATA_H*/
