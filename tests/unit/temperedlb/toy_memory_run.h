/*
//@HEADER
// *****************************************************************************
//
//                             toy_memory_run.h 
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

#if !defined INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_TOY_MEMORY_RUN_H
#define INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_TOY_MEMORY_RUN_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "temperedlb/toy_memory_problem.h"

namespace vt_lb::tests::unit {

/// What one run of the toy memory problem produced, on every rank
struct ToyMemoryOutcome {
  double initial_max = 0.0;
  double final_max = 0.0;
  double exact_optimal_max = 0.0;
  double whole_block_optimal_max = 0.0;
  double local_shared_bytes_after = 0.0;
  std::size_t local_shared_blocks_after = 0;
};

namespace detail {

inline vt_lb::algo::temperedlb::Configuration makeToyMemoryConfig(
  int num_ranks, double delta
) {
  vt_lb::algo::temperedlb::Configuration config(num_ranks);
  config.cluster_based_on_shared_blocks_ = true;
  config.cluster_transfer_strategy_ =
    vt_lb::algo::temperedlb::ClusterTransferStrategy::StrictSharedBlock;
  config.work_model_.delta = delta;
  config.work_model_.has_memory_info = true;
  config.work_model_.has_task_serialized_memory_info = false;
  config.work_model_.has_task_working_memory_info = false;
  config.work_model_.has_task_footprint_memory_info = false;
  config.work_model_.has_shared_block_memory_info = true;
  config.deterministic_ = true;
  config.seed_ = 1;
  return config;
}

} // namespace detail

/**
 * @brief Load balance the toy memory problem and report the outcome
 *
 * @param delta weight on off-home shared block bytes in the work model
 */
template <typename CommT>
ToyMemoryOutcome runToyMemoryProblem(CommT& comm, double delta) {
  auto const num_ranks = comm.numRanks();
  auto const rank = comm.getRank();
  auto const data_dir = getToyMemoryProblemDir();

  auto pd = loadToyMemoryPhaseData(
    data_dir / fmt::format("toy_mem.{}.json", rank), rank, toy_rank_memory_limit
  );
  auto optimal_pd = loadToyMemoryPhaseData(
    data_dir / fmt::format("toy_mem_optimal.{}.json", rank), rank,
    toy_rank_memory_limit
  );

  auto const summary = runTemperedLB(
    comm, detail::makeToyMemoryConfig(num_ranks, delta), pd
  );

  struct CollectiveDummy { } dummy;
  auto handle = comm.template registerInstanceCollective<CollectiveDummy>(&dummy);

  ToyMemoryOutcome out;
  out.initial_max = detail::allMax(handle, computeRankLoad(pd));
  out.exact_optimal_max = detail::allMax(handle, computeRankLoad(optimal_pd));

  std::vector<std::int64_t> task_ids;
  std::vector<double> task_loads;
  std::vector<std::int64_t> task_shared_ids;
  for (auto const& [task_id, task] : pd.getTasksMap()) {
    EXPECT_EQ(task.getSharedBlocks().size(), 1u);
    task_ids.push_back(static_cast<std::int64_t>(task_id));
    task_loads.push_back(task.getLoad());
    task_shared_ids.push_back(
      static_cast<std::int64_t>(*task.getSharedBlocks().begin())
    );
  }

  auto const gathered_ids =
    handle.allgather(task_ids.data(), static_cast<int>(task_ids.size()));
  auto const task_id_to_load = detail::zipByRank(
    gathered_ids,
    handle.allgather(task_loads.data(), static_cast<int>(task_loads.size()))
  );
  auto const task_id_to_shared_id = detail::zipByRank(
    gathered_ids,
    handle.allgather(
      task_shared_ids.data(), static_cast<int>(task_shared_ids.size())
    )
  );

  std::vector<std::int64_t> block_ids;
  std::vector<double> block_bytes;
  for (auto const& [shared_id, block] : pd.getSharedBlocksMap()) {
    block_ids.push_back(static_cast<std::int64_t>(shared_id));
    block_bytes.push_back(block.getSize());
  }
  auto const shared_id_to_bytes = detail::zipByRank(
    handle.allgather(block_ids.data(), static_cast<int>(block_ids.size())),
    handle.allgather(block_bytes.data(), static_cast<int>(block_bytes.size()))
  );

  std::vector<std::int64_t> shared_ids;
  std::vector<double> shared_loads;
  for (auto const& [shared_id, load] : computeSharedBlockLoads(pd)) {
    shared_ids.push_back(shared_id);
    shared_loads.push_back(load);
  }
  auto const block_loads = detail::zipByRank(
    handle.allgather(shared_ids.data(), static_cast<int>(shared_ids.size())),
    handle.allgather(shared_loads.data(), static_cast<int>(shared_loads.size()))
  );

  std::vector<double> block_load_values;
  block_load_values.reserve(block_loads.size());
  for (auto const& [shared_id, load] : block_loads) {
    (void)shared_id;
    block_load_values.push_back(load);
  }
  out.whole_block_optimal_max = computeWholeBlockOptimalMaxLoad(
    block_load_values, num_ranks, toy_max_shared_blocks_per_rank
  );

  double local_after = 0.0;
  std::unordered_set<std::int64_t> shared_ids_after;
  if (auto it = summary.final.find(rank); it != summary.final.end()) {
    for (auto task_id : it->second) {
      auto const key = static_cast<std::int64_t>(task_id);
      local_after += task_id_to_load.at(key);
      shared_ids_after.insert(task_id_to_shared_id.at(key));
    }
  }
  for (auto shared_id : shared_ids_after) {
    out.local_shared_bytes_after += shared_id_to_bytes.at(shared_id);
  }
  out.local_shared_blocks_after = shared_ids_after.size();
  out.final_max = detail::allMax(handle, local_after);

  return out;
}

} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_TOY_MEMORY_RUN_H*/
