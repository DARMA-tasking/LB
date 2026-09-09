/*
//@HEADER
// *****************************************************************************
//
//                        random_shared_block_problem.h
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

#if !defined INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_RANDOM_SHARED_BLOCK_PROBLEM_H
#define INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_RANDOM_SHARED_BLOCK_PROBLEM_H

#include <cstdint>
#include <random>
#include <vector>

#include <vt-lb/model/PhaseData.h>

namespace vt_lb::tests::unit {

/// Shape of a generated shared-block problem
struct RandomProblemSpec {
  int blocks_per_rank = 3;
  int min_tasks_per_block = 1;
  int max_tasks_per_block = 5;
  double min_task_load = 1.0;
  double max_task_load = 20.0;
  double block_bytes = 1.0e9;
  /// Memory budget expressed in whole blocks
  int max_blocks_per_rank = 5;
  /// Scatter each block's tasks over every rank rather than homing them all
  bool blocks_span_ranks = false;
  /// Pin every Nth task in place; 0 pins none
  int pin_every_nth_task = 0;

  double rankMemoryLimit() const {
    return block_bytes * static_cast<double>(max_blocks_per_rank);
  }
};

/**
 * @brief Build one rank's share of a randomized shared-block problem
 *
 * Every rank derives the whole problem from the same seed and keeps only the
 * tasks that landed on it, so the ranks agree without communicating. When
 * blocks span ranks a block starts with clusters on several ranks at once,
 * which is what lets two clusters for one block meet on the same rank.
 */
inline vt_lb::model::PhaseData makeRandomSharedBlockProblem(
  int rank, int num_ranks, int seed, RandomProblemSpec const& spec
) {
  using namespace vt_lb::model;

  PhaseData pd(rank);
  pd.setRankFootprintBytes(0.0);
  pd.setRankMaxMemoryAvailable(spec.rankMemoryLimit());

  int const num_blocks = num_ranks * spec.blocks_per_rank;
  std::int64_t const task_stride = spec.max_tasks_per_block + 1;

  for (int b = 0; b < num_blocks; ++b) {
    int const home = b / spec.blocks_per_rank;

    // Seeded per block so every rank draws the same shape for it
    std::mt19937 gen(static_cast<unsigned>(seed * 7919 + b * 104729));
    std::uniform_int_distribution<int> task_count(
      spec.min_tasks_per_block, spec.max_tasks_per_block
    );
    std::uniform_real_distribution<double> task_load(
      spec.min_task_load, spec.max_task_load
    );
    // A split block starts on its home plus one neighbour, not everywhere:
    // spreading it over every rank would leave no rank able to gain a block
    int const partner = num_ranks > 1
      ? (home + 1 + (b % (num_ranks - 1))) % num_ranks
      : home;
    std::uniform_int_distribution<int> pick_partner(0, 1);

    int const n = task_count(gen);
    bool holds_any = false;

    for (int t = 0; t < n; ++t) {
      auto const load = task_load(gen);
      int const start_rank = spec.blocks_span_ranks and pick_partner(gen) == 1
        ? partner
        : home;
      if (start_rank != rank) {
        continue;
      }

      auto const task_id =
        static_cast<TaskType>(static_cast<std::int64_t>(b) * task_stride + t + 1);
      bool const migratable = spec.pin_every_nth_task == 0 or
        (t % spec.pin_every_nth_task) != 0;
      Task task{task_id, home, rank, migratable, TaskMemory{}, load};
      task.addSharedBlock(static_cast<SharedBlockType>(b));
      pd.addTask(task);
      holds_any = true;
    }

    if (holds_any) {
      pd.addSharedBlock(
        SharedBlock{static_cast<SharedBlockType>(b), spec.block_bytes, home}
      );
    }
  }

  return pd;
}

} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_RANDOM_SHARED_BLOCK_PROBLEM_H*/
