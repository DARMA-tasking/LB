/*
//@HEADER
// *****************************************************************************
//
//                               transfer_util.cc
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

#include <vt-lb/algo/temperedlb/transfer_util.h>
#include <vt-lb/model/types.h>
#include <vt-lb/util/logging.h>

#include <set>
#include <cassert>
#include <random>
#include <algorithm>
#include <vector>
#include <stdexcept>

namespace vt_lb::algo::temperedlb {

/*static*/ std::vector<double> TransferUtil::createCMF(
  CMFType cmf_type,
  std::unordered_map<int, RankInfo> const& load_info,
  model::LoadType target_max_load,
  std::vector<int> const& under
) {
  // Build the CMF
  std::vector<double> cmf = {};

  if (under.size() == 1) {
    // trying to compute the cmf for only a single object can result
    // in nan for some cmf types below, so do it the easy way instead
    cmf.push_back(1.0);
    return cmf;
  }

  double sum_p = 0.0;
  double factor = 1.0;

  switch (cmf_type) {
  case CMFType::Original:
    factor = 1.0 / target_max_load;
    break;
  case CMFType::NormByMax:
  case CMFType::NormByMaxExcludeIneligible:
    {
      double l_max = 0.0;
      for (auto&& pe : under) {
        auto iter = load_info.find(pe);
        assert(iter != load_info.end() && "Rank must be in load_info");
        auto load = iter->second;
        if (load.getScaledLoad() > l_max) {
          l_max = load.getScaledLoad();
        }
      }
      factor = 1.0 / (l_max > target_max_load ? l_max : target_max_load);
    }
    break;
  default:
    throw std::runtime_error("Unknown CMF type");
  }

  for (auto&& pe : under) {
    auto iter = load_info.find(pe);
    assert(iter != load_info.end() && "Node must be in load_info");

    auto load = iter->second;
    sum_p += 1. - factor * load.getScaledLoad();
    cmf.push_back(sum_p);
  }

  // Normalize the CMF
  for (auto& elm : cmf) {
    elm /= sum_p;
  }

  assert(cmf.size() == under.size());

  return cmf;
}

/*static*/ int TransferUtil::sampleFromCMF(
  bool deterministic,
  std::vector<int> const& under,
  std::vector<double> const& cmf,
  std::mt19937 gen_sample,
  std::random_device& seed
) {
  // Create the distribution
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  if (!deterministic) {
    gen_sample.seed(seed());
  }

  int selected_rank = -1;

  // Pick from the CMF
  auto const u = dist(gen_sample);
  std::size_t i = 0;
  for (auto&& x : cmf) {
    if (x >= u) {
      selected_rank = under[i];
      break;
    }
    i++;
  }

  return selected_rank;
}

/*static*/ std::vector<int> TransferUtil::makeUnderloaded(
  bool deterministic,
  std::unordered_map<int, RankInfo> const& load_info,
  double underloaded_threshold
) {
  std::vector<int> under = {};
  for (auto&& elm : load_info) {
    if (elm.second.load * elm.second.rank_alpha < underloaded_threshold) {
      under.push_back(elm.first);
    }
  }
  if (deterministic) {
    std::sort(under.begin(), under.end());
  }
  return under;
}

/*static*/ std::vector<int> TransferUtil::makeSufficientlyUnderloaded(
  bool deterministic,
  std::unordered_map<int, RankInfo> const& load_info,
  CriterionEnum criterion,
  RankInfo this_rank_info,
  model::LoadType target_max_load,
  model::LoadType load_to_accommodate
) {
  std::vector<int> sufficiently_under = {};
  for (auto&& elm : load_info) {
    bool eval = Criterion(criterion)(
      this_rank_info,
      elm.second,
      load_to_accommodate,
      target_max_load
    );
    if (eval) {
      sufficiently_under.push_back(elm.first);
    }
  }
  if (deterministic) {
    std::sort(sufficiently_under.begin(), sufficiently_under.end());
  }
  return sufficiently_under;
}

/*static*/ std::vector<model::TaskType> TransferUtil::orderObjects(
  ObjectOrder obj_ordering,
  std::unordered_map<model::TaskType, model::Task> cur_objs,
  RankInfo this_rank_info,
  model::LoadType target_max_load
) {
  // define the iteration order
  std::vector<model::TaskType> ordered_obj_ids(cur_objs.size());

  int i = 0;
  for (auto &obj : cur_objs) {
    ordered_obj_ids[i++] = obj.first;
  }

  switch (obj_ordering) {
  case ObjectOrder::ElmID:
    std::sort(
      ordered_obj_ids.begin(), ordered_obj_ids.end(), std::less<model::TaskType>()
    );
    break;
  case ObjectOrder::FewestMigrations:
    {
      // first find the load of the smallest single object that, if migrated
      // away, could bring this processor's load below the target load
      auto over_avg = this_rank_info.getScaledLoad() - target_max_load;
      // if no objects are larger than over_avg, then single_obj_load will still
      // (incorrectly) reflect the total load, which will not be a problem
      auto single_obj_load = this_rank_info.getScaledLoad();
      for (auto &obj : cur_objs) {
        auto obj_load = obj.second.getLoad();
        if (obj_load >= over_avg && obj_load < single_obj_load) {
          single_obj_load = obj_load;
        }
      }
      // sort largest to smallest if <= single_obj_load
      // sort smallest to largest if > single_obj_load
      std::sort(
        ordered_obj_ids.begin(), ordered_obj_ids.end(),
        [&cur_objs, single_obj_load](
          const model::TaskType& left, const model::TaskType& right
        ) {
          auto left_load = cur_objs[left].getLoad();
          auto right_load = cur_objs[right].getLoad();
          if (left_load <= single_obj_load && right_load <= single_obj_load) {
            // we're in the sort load descending regime (first section)
            return left_load > right_load;
          }
          // else
          // EITHER
          // a) both are above the cut, and we're in the sort ascending
          //    regime (second section), so return left < right
          // OR
          // b) one is above the cut and one is at or below, and the one
          //    that is at or below the cut needs to come first, so
          //    also return left < right
          return left_load < right_load;
        }
      );
      if (cur_objs.size() > 0) {
        VT_LB_LOG(
          LoadBalancer, verbose,
          "TemperedLB::decide: over_avg={}, single_obj_load={}\n",
          model::LoadType(over_avg),
          model::LoadType(cur_objs[ordered_obj_ids[0]].getLoad())
        );
      }
    }
    break;
  case ObjectOrder::SmallObjects:
    {
      // first find the smallest object that, if migrated away along with all
      // smaller objects, could bring this processor's load below the target
      // load
      auto over_avg = this_rank_info.getScaledLoad() - target_max_load;
      std::sort(
        ordered_obj_ids.begin(), ordered_obj_ids.end(),
        [&cur_objs](const model::TaskType &left, const model::TaskType &right) {
          auto left_load = cur_objs[left].getLoad();
          auto right_load = cur_objs[right].getLoad();
          // sort load descending
          return left_load > right_load;
        }
      );
      auto cum_obj_load = this_rank_info.getScaledLoad();
      auto single_obj_load = cur_objs[ordered_obj_ids[0]].getLoad() * this_rank_info.rank_alpha;
      for (auto obj_id : ordered_obj_ids) {
        auto this_obj_load = cur_objs[obj_id].getLoad() * this_rank_info.rank_alpha;
        if (cum_obj_load - this_obj_load < over_avg) {
          single_obj_load = this_obj_load;
          break;
        } else {
          cum_obj_load -= this_obj_load;
        }
      }
      // now that we found that object, re-sort based on it
      // sort largest to smallest if <= single_obj_load
      // sort smallest to largest if > single_obj_load
      std::sort(
        ordered_obj_ids.begin(), ordered_obj_ids.end(),
        [&cur_objs, single_obj_load](
          const model::TaskType &left, const model::TaskType &right
        ) {
          auto left_load = cur_objs[left].getLoad();
          auto right_load = cur_objs[right].getLoad();
          if (left_load <= single_obj_load && right_load <= single_obj_load) {
            // we're in the sort load descending regime (first section)
            return left_load > right_load;
          }
          // else
          // EITHER
          // a) both are above the cut, and we're in the sort ascending
          //    regime (second section), so return left < right
          // OR
          // b) one is above the cut and one is at or below, and the one
          //    that is at or below the cut needs to come first, so
          //    also return left < right
          return left_load < right_load;
        }
      );
      if (cur_objs.size() > 0) {
        VT_LB_LOG(
          LoadBalancer, verbose,
          "TemperedLB::decide: over_avg={}, marginal_obj_load={}\n",
          model::LoadType(over_avg),
          model::LoadType(cur_objs[ordered_obj_ids[0]].getLoad())
        );
      }
    }
    break;
  case ObjectOrder::LargestObjects:
    {
      // sort by descending load
      std::sort(
        ordered_obj_ids.begin(), ordered_obj_ids.end(),
        [&cur_objs](const model::TaskType &left, const model::TaskType &right) {
          auto left_load = cur_objs[left].getLoad();
          auto right_load = cur_objs[right].getLoad();
          // sort load descending
          return left_load > right_load;
        }
      );
    }
    break;
  case ObjectOrder::Arbitrary:
    break;
  default:
    throw std::runtime_error("TemperedLB::orderObjects: ordering not supported");
    break;
  }

  return ordered_obj_ids;
}

} /* end namespace vt_lb::algo::temperedlb */