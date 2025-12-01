/*
//@HEADER
// *****************************************************************************
//
//                                Task.h
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

#if !defined INCLUDED_VT_LB_MODEL_TASK_H
#define INCLUDED_VT_LB_MODEL_TASK_H

#include "types.h"

#include <unordered_set>

namespace vt_lb::model {

struct TaskMemory {
    TaskMemory() = default;
    TaskMemory(BytesType working, BytesType footprint, BytesType serialized)
      : working_(working), footprint_(footprint), serialized_(serialized)
    {}

    BytesType getWorking() const { return working_; }
    BytesType getFootprint() const { return footprint_; }
    BytesType getSerialized() const { return serialized_; }

    template <typename Serializer>
    void serialize(Serializer& s) {
      s | working_;
      s | footprint_;
      s | serialized_;
    }

private:
    BytesType working_ = 0.0;
    BytesType footprint_ = 0.0;
    BytesType serialized_ = 0.0;
};

struct Task {
    Task() = default;
    Task(TaskType id, RankType home, RankType current, bool migratable,
         TaskMemory const& memory, LoadType load)
      : id_(id),
        home_(home),
        current_(current),
        migratable_(migratable),
        memory_(memory),
        load_(load)
    {}

    TaskType getId() const { return id_; }
    RankType getHome() const { return home_; }
    RankType getCurrent() const { return current_; }
    bool isMigratable() const { return migratable_; }
    TaskMemory const& getMemory() const { return memory_; }
    LoadType getLoad() const { return load_; }

    // Add accessors for shared blocks
    void addSharedBlock(SharedBlockType sb) { shared_blocks_.insert(sb); }
    std::unordered_set<SharedBlockType> const& getSharedBlocks() const { return shared_blocks_; }

    template <typename Serializer>
    void serialize(Serializer& s) {
      s | id_;
      s | home_;
      s | current_;
      s | migratable_;
      s | memory_;
      s | load_;
      s | shared_blocks_;
    }

private:
    TaskType id_ = invalid_task;
    int home_ = invalid_node;
    int current_ = invalid_node;
    bool migratable_ = true;
    TaskMemory memory_;
    LoadType load_ = 0.0;
    std::unordered_set<SharedBlockType> shared_blocks_;

public:
    bool operator==(const Task& other) const { return id_ == other.id_; }
    bool operator!=(const Task& other) const { return !(*this == other); }
};

} /* end namespace vt_lb::model */

#endif /*INCLUDED_VT_LB_MODEL_TASK_H*/
