/*
//@HEADER
// *****************************************************************************
//
//                             toy_memory_problem.h
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

#if !defined INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_TOY_MEMORY_PROBLEM_H
#define INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_TOY_MEMORY_PROBLEM_H

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann-lb/json.hpp>
#include <fmt-lb/format.h>

#include "temperedlb/lb_run_helpers.h"

namespace vt_lb::tests::unit {

/// Every shared block in the toy problem is this size
inline constexpr double toy_shared_block_bytes = 1600000000.0;
/// The rank memory budget admits exactly this many blocks
inline constexpr int toy_max_shared_blocks_per_rank = 4;
inline constexpr double toy_rank_memory_limit =
  toy_shared_block_bytes * toy_max_shared_blocks_per_rank;

inline std::filesystem::path getToyMemoryProblemDir() {
  return (
    std::filesystem::path(__FILE__).parent_path() /
    ".." / ".." / ".." / "data" / "user-defined-memory-toy-problem"
  ).lexically_normal();
}

inline vt_lb::model::PhaseData loadToyMemoryPhaseData(
  std::filesystem::path const& path, int rank, double rank_memory_limit
) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error(
      fmt::format("Failed to open toy memory file {}", path.string())
    );
  }

  nlohmann::json root = nlohmann::json::parse(stream);
  auto const& tasks = root.at("phases").at(0).at("tasks");

  vt_lb::model::PhaseData pd(rank);
  pd.setRankFootprintBytes(0.0);
  pd.setRankMaxMemoryAvailable(rank_memory_limit);

  for (auto const& task_json : tasks) {
    auto const& entity = task_json.at("entity");
    auto const home_rank = entity.at("home").get<int>();

    vt_lb::model::Task task(
      static_cast<vt_lb::model::TaskType>(entity.at("id").get<std::int64_t>()),
      home_rank,
      task_json.at("node").get<int>(),
      entity.at("migratable").get<bool>(),
      vt_lb::model::TaskMemory{},
      task_json.at("time").get<double>()
    );

    auto const& user_defined = task_json.at("user_defined");
    auto const shared_id = static_cast<vt_lb::model::SharedBlockType>(
      user_defined.at("shared_id").get<std::int64_t>()
    );
    auto const shared_bytes = static_cast<vt_lb::model::BytesType>(
      user_defined.at("shared_bytes").get<double>()
    );

    task.addSharedBlock(shared_id);
    if (not pd.hasSharedBlock(shared_id)) {
      pd.addSharedBlock(
        vt_lb::model::SharedBlock(shared_id, shared_bytes, home_rank)
      );
    }

    pd.addTask(task);
  }

  return pd;
}

inline double computeRankLoad(vt_lb::model::PhaseData const& pd) {
  double local_load = 0.0;
  for (auto const& [task_id, task] : pd.getTasksMap()) {
    (void)task_id;
    local_load += task.getLoad();
  }
  return local_load;
}

inline std::unordered_map<std::int64_t, double> computeSharedBlockLoads(
  vt_lb::model::PhaseData const& pd
) {
  std::unordered_map<std::int64_t, double> shared_loads;
  for (auto const& [task_id, task] : pd.getTasksMap()) {
    (void)task_id;
    auto const& shared_blocks = task.getSharedBlocks();
    if (shared_blocks.empty()) {
      continue;
    }
    auto const shared_id = static_cast<std::int64_t>(*shared_blocks.begin());
    shared_loads[shared_id] += task.getLoad();
  }
  return shared_loads;
}

/// Exhaustive best max load when whole blocks may not be split
inline double computeWholeBlockOptimalMaxLoad(
  std::vector<double> block_loads, int num_ranks, int max_blocks_per_rank
) {
  std::sort(block_loads.begin(), block_loads.end(), std::greater<double>());

  std::vector<double> rank_loads(num_ranks, 0.0);
  std::vector<int> block_counts(num_ranks, 0);
  double best_max_load = std::numeric_limits<double>::infinity();

  auto assign = [&](auto&& self, std::size_t index) -> void {
    if (index == block_loads.size()) {
      best_max_load = std::min(
        best_max_load, *std::max_element(rank_loads.begin(), rank_loads.end())
      );
      return;
    }

    std::unordered_set<double> seen_rank_loads;
    auto const block_load = block_loads[index];
    for (int rank = 0; rank < num_ranks; ++rank) {
      if (block_counts[rank] >= max_blocks_per_rank) {
        continue;
      }
      if (seen_rank_loads.find(rank_loads[rank]) != seen_rank_loads.end()) {
        continue;
      }
      seen_rank_loads.insert(rank_loads[rank]);

      auto const new_load = rank_loads[rank] + block_load;
      if (new_load >= best_max_load) {
        continue;
      }

      rank_loads[rank] = new_load;
      block_counts[rank]++;
      self(self, index + 1);
      block_counts[rank]--;
      rank_loads[rank] -= block_load;
    }
  };

  assign(assign, 0);
  return best_max_load;
}

} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_TOY_MEMORY_PROBLEM_H*/
