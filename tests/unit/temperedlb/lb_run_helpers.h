/*
//@HEADER
// *****************************************************************************
//
//                               lb_run_helpers.h
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

#if !defined INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_LB_RUN_HELPERS_H
#define INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_LB_RUN_HELPERS_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vt-lb/algo/temperedlb/temperedlb.h>

namespace vt_lb::tests::unit {

/// Task ids per rank before and after a load balancing run
struct LbRunSummary {
  std::unordered_map<int, std::vector<int>> initial;
  std::unordered_map<int, std::vector<int>> final;
};

namespace detail {

inline std::unordered_map<int, std::vector<int>> toIntDistribution(
  std::unordered_map<vt_lb::model::RankType, std::vector<vt_lb::model::TaskType>> const& dist
) {
  std::unordered_map<int, std::vector<int>> out;
  for (auto const& [rank, ids] : dist) {
    auto& vec = out[rank];
    vec.reserve(ids.size());
    for (auto id : ids) {
      vec.push_back(static_cast<int>(id));
    }
  }
  return out;
}

/// Flatten allgathered per-rank vectors into one id-keyed map
template <typename ValueT>
std::unordered_map<std::int64_t, ValueT> zipByRank(
  std::unordered_map<vt_lb::model::RankType, std::vector<std::int64_t>> const& ids,
  std::unordered_map<vt_lb::model::RankType, std::vector<ValueT>> const& values
) {
  std::unordered_map<std::int64_t, ValueT> out;
  for (auto const& [rank, id_vec] : ids) {
    auto const& value_vec = values.at(rank);
    for (std::size_t i = 0; i < id_vec.size(); ++i) {
      out.emplace(id_vec[i], value_vec[i]);
    }
  }
  return out;
}

/// Broadcast the maximum of a per-rank value to every rank
template <typename HandleT>
double allMax(HandleT& handle, double local) {
  double result = 0.0;
  handle.reduce(0, MPI_DOUBLE, MPI_MAX, &local, &result, 1);
  handle.broadcast(0, MPI_DOUBLE, &result, 1);
  return result;
}

/// Broadcast the sum of a per-rank value to every rank
template <typename HandleT>
double allSum(HandleT& handle, double local) {
  double result = 0.0;
  handle.reduce(0, MPI_DOUBLE, MPI_SUM, &local, &result, 1);
  handle.broadcast(0, MPI_DOUBLE, &result, 1);
  return result;
}

} // namespace detail

/// Build TemperedLB, run it, and report the global distribution either side
template <typename CommT>
LbRunSummary runTemperedLB(
  CommT& comm,
  vt_lb::algo::temperedlb::Configuration const& config,
  vt_lb::model::PhaseData const& pd
) {
  vt_lb::algo::temperedlb::TemperedLB<CommT> lb(comm, config);
  lb.inputData(std::make_unique<vt_lb::model::PhaseData>(pd));

  auto initial = detail::toIntDistribution(
    lb.getGlobalDistribution(pd.getTaskIds())
  );
  auto final = detail::toIntDistribution(
    lb.getGlobalDistribution(lb.run())
  );

  return {std::move(initial), std::move(final)};
}

} // namespace vt_lb::tests::unit

#endif /*INCLUDED_VT_LB_TESTS_UNIT_TEMPEREDLB_LB_RUN_HELPERS_H*/
