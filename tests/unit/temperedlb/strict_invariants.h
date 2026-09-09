/*
//@HEADER
// *****************************************************************************
//
//                             strict_invariants.h
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

#if !defined INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_STRICT_INVARIANTS_H
#define INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_STRICT_INVARIANTS_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#include "temperedlb/lb_run_helpers.h"

namespace vt_lb::tests::unit {

/// What a load balancing run did, measured against the properties it must hold
struct StrictInvariantReport {
  double initial_max_load = 0.0;
  double final_max_load = 0.0;
  double initial_total_load = 0.0;
  double final_total_load = 0.0;
  std::size_t initial_task_count = 0;
  std::size_t final_task_count = 0;
  /// Largest number of distinct shared blocks any single rank ended up holding
  std::size_t max_blocks_on_a_rank = 0;
  /// Largest shared-block footprint any single rank ended up holding
  double max_block_bytes_on_a_rank = 0.0;
  /// Whether every task landed on exactly one rank
  bool tasks_partitioned = true;
  /// How many tasks marked non-migratable ended up somewhere else
  std::size_t pinned_tasks_moved = 0;
  /// Load of every shared block, for comparing against an exhaustive optimum
  std::vector<double> block_loads;
};

namespace detail {

/// Task id, load and shared block for every task in the problem, on every rank
struct GlobalTaskFacts {
  std::unordered_map<std::int64_t, double> load;
  std::unordered_map<std::int64_t, std::int64_t> block;
  std::unordered_map<std::int64_t, double> block_bytes;
  /// Where each task started, and -1 for tasks free to move
  std::unordered_map<std::int64_t, std::int64_t> pinned_to_rank;
};

template <typename HandleT>
GlobalTaskFacts gatherTaskFacts(
  HandleT& handle, vt_lb::model::PhaseData const& pd
) {
  std::vector<std::int64_t> ids;
  std::vector<double> loads;
  std::vector<std::int64_t> blocks;
  std::vector<std::int64_t> pinned;
  auto const this_rank = static_cast<std::int64_t>(pd.getRank());
  for (auto const& [task_id, task] : pd.getTasksMap()) {
    auto const& task_blocks = task.getSharedBlocks();
    ids.push_back(static_cast<std::int64_t>(task_id));
    loads.push_back(task.getLoad());
    blocks.push_back(
      task_blocks.empty()
        ? -1
        : static_cast<std::int64_t>(*task_blocks.begin())
    );
    pinned.push_back(task.isMigratable() ? -1 : this_rank);
  }

  auto const gathered_ids =
    handle.allgather(ids.data(), static_cast<int>(ids.size()));

  std::vector<std::int64_t> block_ids;
  std::vector<double> block_sizes;
  for (auto const& [block_id, block] : pd.getSharedBlocksMap()) {
    block_ids.push_back(static_cast<std::int64_t>(block_id));
    block_sizes.push_back(block.getSize());
  }

  GlobalTaskFacts facts;
  facts.load = zipByRank(
    gathered_ids, handle.allgather(loads.data(), static_cast<int>(loads.size()))
  );
  facts.block = zipByRank(
    gathered_ids,
    handle.allgather(blocks.data(), static_cast<int>(blocks.size()))
  );
  facts.pinned_to_rank = zipByRank(
    gathered_ids, handle.allgather(pinned.data(), static_cast<int>(pinned.size()))
  );
  facts.block_bytes = zipByRank(
    handle.allgather(block_ids.data(), static_cast<int>(block_ids.size())),
    handle.allgather(block_sizes.data(), static_cast<int>(block_sizes.size()))
  );
  return facts;
}

} // namespace detail

/**
 * @brief Cap every rank at the blocks the busiest rank already holds
 *
 * A budget only tests anything when it binds. Sizing it to the initial
 * distribution makes it satisfiable from the start and tight thereafter.
 *
 * @return the per-rank block allowance that was applied
 */
template <typename CommT>
std::size_t capBudgetAtInitialMax(
  CommT& comm, vt_lb::model::PhaseData& pd, double block_bytes, int slack
) {
  std::unordered_set<std::int64_t> blocks;
  for (auto const& [task_id, task] : pd.getTasksMap()) {
    (void)task_id;
    for (auto block : task.getSharedBlocks()) {
      blocks.insert(static_cast<std::int64_t>(block));
    }
  }

  struct CollectiveDummy { } dummy;
  auto handle = comm.template registerInstanceCollective<CollectiveDummy>(&dummy);
  auto const allowed = static_cast<std::size_t>(
    detail::allMax(handle, static_cast<double>(blocks.size()))
  ) + static_cast<std::size_t>(slack);

  pd.setRankMaxMemoryAvailable(block_bytes * static_cast<double>(allowed));
  return allowed;
}

/**
 * @brief Run TemperedLB and measure the properties every run must hold
 *
 * Collective: every rank must call it. The phase data is restored by the
 * balancer at the end of the trial, so it still describes the input.
 */
template <typename CommT>
StrictInvariantReport runAndMeasure(
  CommT& comm,
  vt_lb::algo::temperedlb::Configuration const& config,
  vt_lb::model::PhaseData const& pd
) {
  auto const summary = runTemperedLB(comm, config, pd);

  struct CollectiveDummy { } dummy;
  auto handle = comm.template registerInstanceCollective<CollectiveDummy>(&dummy);
  auto const facts = detail::gatherTaskFacts(handle, pd);

  double local_before = 0.0;
  for (auto const& [task_id, task] : pd.getTasksMap()) {
    (void)task_id;
    local_before += task.getLoad();
  }

  double local_after = 0.0;
  std::unordered_set<std::int64_t> blocks_after;
  std::size_t local_count_after = 0;
  std::size_t local_pinned_moved = 0;
  if (auto it = summary.final.find(comm.getRank()); it != summary.final.end()) {
    for (auto task_id : it->second) {
      auto const key = static_cast<std::int64_t>(task_id);
      local_after += facts.load.at(key);
      ++local_count_after;
      if (auto const block = facts.block.at(key); block != -1) {
        blocks_after.insert(block);
      }
      auto const pinned_to = facts.pinned_to_rank.at(key);
      if (pinned_to != -1 and pinned_to != static_cast<std::int64_t>(comm.getRank())) {
        ++local_pinned_moved;
      }
    }
  }

  double local_block_bytes = 0.0;
  for (auto block : blocks_after) {
    local_block_bytes += facts.block_bytes.at(block);
  }

  StrictInvariantReport report;
  report.initial_max_load = detail::allMax(handle, local_before);
  report.final_max_load = detail::allMax(handle, local_after);
  report.initial_total_load = detail::allSum(handle, local_before);
  report.final_total_load = detail::allSum(handle, local_after);
  report.initial_task_count = facts.load.size();
  report.max_blocks_on_a_rank = static_cast<std::size_t>(
    detail::allMax(handle, static_cast<double>(blocks_after.size()))
  );
  report.max_block_bytes_on_a_rank = detail::allMax(handle, local_block_bytes);
  report.pinned_tasks_moved = static_cast<std::size_t>(
    detail::allSum(handle, static_cast<double>(local_pinned_moved))
  );

  // Blocks are atomic, so the best any arrangement can do is the best packing
  // of whole blocks. Sum each block's load from the globally known facts.
  std::unordered_map<std::int64_t, double> load_by_block;
  for (auto const& [task_id, load] : facts.load) {
    if (auto const block = facts.block.at(task_id); block != -1) {
      load_by_block[block] += load;
    }
  }
  report.block_loads.reserve(load_by_block.size());
  for (auto const& [block, load] : load_by_block) {
    (void)block;
    report.block_loads.push_back(load);
  }

  auto const total_after = detail::allSum(
    handle, static_cast<double>(local_count_after)
  );
  report.final_task_count = static_cast<std::size_t>(total_after);

  // Every task lands exactly once, so the placed count matches the input count
  report.tasks_partitioned =
    report.final_task_count == report.initial_task_count;

  return report;
}

} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_STRICT_INVARIANTS_H*/
