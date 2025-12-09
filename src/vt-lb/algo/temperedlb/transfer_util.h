/*
//@HEADER
// *****************************************************************************
//
//                               transfer_util.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_UTIL_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_UTIL_H

#include <vt-lb/model/types.h>
#include <vt-lb/model/PhaseData.h>
#include <vt-lb/algo/temperedlb/task_cluster_summary_info.h>

#include <vector>
#include <set>
#include <random>
#include <cassert>

#include <fmt-lb/format.h>

namespace vt_lb::algo::temperedlb {

struct RankInfo {
  model::LoadType load = 0.0;
  double rank_alpha = 0.0;

  double getScaledLoad() const {
    return load * rank_alpha;
  }

  template <typename SerializerT>
  void serialize(SerializerT& s) {
    s | load;
    s | rank_alpha;
  }
};

struct RankClusterInfo {
  std::unordered_map<int, TaskClusterSummaryInfo> cluster_summaries;
  double rank_alpha = 0.0;

  template <typename SerializerT>
  void serialize(SerializerT& s) {
    s | cluster_summaries;
    s | rank_alpha;
  }
};

enum struct CriterionEnum : uint8_t {
  Grapevine         = 0,
  ModifiedGrapevine = 1
};

struct GrapevineCriterion {
  bool operator()(RankInfo, RankInfo under, model::LoadType obj, model::LoadType avg) const {
    return !(under.getScaledLoad() + obj * under.rank_alpha > avg);
  }
};

struct ModifiedGrapevineCriterion  {
  bool operator()(RankInfo over, RankInfo under, model::LoadType obj, model::LoadType) const {
    return obj * under.rank_alpha <= over.getScaledLoad() - under.getScaledLoad();
  }
};

struct Criterion {
  explicit Criterion(CriterionEnum const criterion)
    : criterion_(criterion)
  { }

  bool operator()(RankInfo over, RankInfo under, model::LoadType obj, model::LoadType avg) const {
    switch (criterion_) {
    case CriterionEnum::Grapevine:
      return GrapevineCriterion()(over, under, obj, avg);
      break;
    case CriterionEnum::ModifiedGrapevine:
      return ModifiedGrapevineCriterion()(over, under, obj, avg);
      break;
    default:
      assert(false && "Incorrect criterion value");
      return false;
      break;
    };
  }

protected:
  CriterionEnum const criterion_;
};

inline auto format_as(CriterionEnum c) {
  std::string_view name = "Unknown";
  switch (c) {
  case CriterionEnum::Grapevine:
    name = "Grapevine";
    break;
  case CriterionEnum::ModifiedGrapevine:
    name = "ModifiedGrapevine";
    break;
  default:
    name = "Unknown";
    break;
  }
  return name;
}

// Provide fmt::format_as overload for RankInfo so it can be used with fmt::print
inline auto format_as(RankInfo const& ri) {
  return fmt::format("RankInfo{{load={}, rank_alpha={}, scaled_load={}}}",
                     ri.load, ri.rank_alpha, ri.getScaledLoad());
}

struct TransferUtil {

  /// Enum for how the CMF is computed
  enum struct CMFType : uint8_t {
    /**
     * \brief Original approach
     *
     * Remove processors from the CMF as soon as they exceed the target (e.g.,
     * processor-avg) load. Use a CMF factor of 1.0/x, where x is the target load.
     */
    Original   = 0,
    /**
     * \brief Compute the CMF factor using the largest processor load in the CMF
     *
     * Do not remove processors from the CMF that exceed the target load until the
     * next iteration. Use a CMF factor of 1.0/x, where x is the greater of the
     * target load and the load of the most loaded processor in the CMF.
     */
    NormByMax  = 1,
    /**
     * \brief Narrow the CMF to only include processors that can accommodate the
     * transfer
     *
     * Use a CMF factor of 1.0/x, where x is the greater of the target load and
     * the load of the most loaded processor in the CMF. Only include processors
     * in the CMF that will pass the chosen Criterion for the object being
     * considered for transfer.
     */
    NormByMaxExcludeIneligible = 2,
  };

  /// Enum for the order in which local objects are considered for transfer
  enum struct ObjectOrder : uint8_t {
    Arbitrary = 0, //< Arbitrary order: iterate as defined by the unordered_map
    /**
     * \brief By element ID
     *
     * Sort ascending by the ID member of ElementIDStruct.
     */
    ElmID     = 1,
    /**
     * \brief Order for the fewest migrations
     *
     * Order starting with the object with the smallest load that can be
     * transferred to drop the processor load below the average, then by
     * descending load for objects with smaller loads, and finally by ascending
     * load for objects with larger loads.
     */
    FewestMigrations = 2,
    /**
     * \brief Order for migrating the objects with the smallest loads
     *
     * Find the object with the smallest load where the sum of its own load and
     * all smaller loads meets or exceeds the amount by which this processor's
     * load exceeds the target load. Order starting with that object, then by
     * descending load for objects with smaller loads, and finally by ascending
     * load for objects with larger loads.
     */
    SmallObjects = 3,
    /**
     * \brief Order by descending load
     */
    LargestObjects = 4
  };

  static std::vector<double> createCMF(
    CMFType cmf_type,
    std::unordered_map<int, RankInfo> const& load_info,
    model::LoadType target_max_load,
    std::vector<int> const& under
  );

  static int sampleFromCMF(
    bool deterministic,
    std::vector<int> const& under,
    std::vector<double> const& cmf,
    std::mt19937 gen_sample,
    std::random_device& seed
  );

  static std::vector<int> makeUnderloaded(
    bool deterministic,
    std::unordered_map<int, RankInfo> const& load_info,
    double underloaded_threshold
  );

  static std::vector<int> makeSufficientlyUnderloaded(
    bool deterministic,
    std::unordered_map<int, RankInfo> const& load_info,
    CriterionEnum criterion,
    RankInfo this_rank_info,
    model::LoadType target_max_load,
    model::LoadType load_to_accommodate
  );

  static std::vector<model::TaskType> orderObjects(
    ObjectOrder obj_ordering,
    std::unordered_map<model::TaskType, model::Task> cur_objs,
    RankInfo this_rank_info,
    model::LoadType target_max_load
  );
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TRANSFER_UTIL_H*/