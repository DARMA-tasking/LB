/*
//@HEADER
// *****************************************************************************
//
//                                yaml_reader.cc
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

#include <vt-lb/input/yaml_reader.h>
#include <vt-lb/algo/driver/driver.h>

#include <yaml-cpp/yaml.h>

namespace vt_lb::input {

void YAMLReader::readFile(std::string const& in_filename) {
  try {
    yaml_ = std::make_unique<YAML::Node>(YAML::LoadFile(in_filename));
  } catch (const YAML::Exception& e) {
    fmt::print("Failed to load YAML file '{}': {}\n", in_filename, e.what());
    throw std::runtime_error("Failed to load YAML file");
  }
}
void YAMLReader::loadYamlString(std::string const& yaml_string) {
  try {
    yaml_ = std::make_unique<YAML::Node>(YAML::Load(yaml_string));
  } catch (const YAML::Exception& e) {
    fmt::print("Failed to load YAML string: {}\n", e.what());
    throw std::runtime_error("Failed to load YAML string");
  }
}
std::string YAMLReader::parseJSONRankPath() {
  if (yaml_ == nullptr) {
    throw std::runtime_error("YAML not loaded");
  }

  auto const &root = *yaml_;
  if (root["from_data"] && root["from_data"].IsMap()) {
    auto const& from_data = root["from_data"];
    if (from_data["data_folder"]) {
      return get_str(from_data, "data_folder");
    }
  }

  fmt::print("Could not find JSON folder path.\n");
  throw std::runtime_error("Could not find JSON folder path");
}
int YAMLReader::parsePhaseID() {
  if (yaml_ == nullptr) {
    throw std::runtime_error("YAML not loaded");
  }

  auto const &root = *yaml_;
  if (root["from_data"] && root["from_data"].IsMap()) {
    auto const& from_data = root["from_data"];
    if (from_data["phase_id"]) {
      return get_int(from_data, "phase_id");
    }
  }

  fmt::print("Could not find phase ID.\n");
  throw std::runtime_error("Could not find phase ID");
}
vt_lb::DriverAlgoEnum YAMLReader::parseAlgorithm() {
  if (yaml_ == nullptr) {
    throw std::runtime_error("YAML not loaded");
  }

  auto const &root = *yaml_;
  if (root["algorithm"]) {
    std::string algo_str = get_str(root, "algorithm");
    if (algo_str == "TemperedLB") {
      return vt_lb::DriverAlgoEnum::TemperedLB;
    } else if (algo_str == "None") {
      return vt_lb::DriverAlgoEnum::None;
    } else {
      fmt::print("Unknown algorithm type: '{}'\n", algo_str);
      throw std::runtime_error("Unknown algorithm type");
    }
  }

  fmt::print("Could not find algorithm type.\n");
  throw std::runtime_error("Could not find algorithm type");
}
vt_lb::algo::temperedlb::Configuration YAMLReader::parseLBConfig(int num_ranks) {
  if (yaml_ == nullptr) {
    throw std::runtime_error("YAML not loaded");
  }

  auto const& root = *yaml_;
  // Find LB config
  vt_lb::algo::temperedlb::Configuration config{num_ranks};
  if (root["configuration"] && root["configuration"].IsMap()) {
    auto const& yaml_config = root["configuration"];
    if (yaml_config["num_trials"]) {
      config.num_trials_ = get_int(yaml_config, "num_trials");
    }
    if (yaml_config["num_iters"]) {
      config.num_iters_ = get_int(yaml_config, "num_iters");
    }
    if (yaml_config["fanout"]) {
      config.f_ = get_int(yaml_config, "fanout");
    }
    if (yaml_config["n_rounds"]) {
      config.k_max_ = get_int(yaml_config, "n_rounds");
    }
    if (yaml_config["deterministic"]) {
      config.deterministic_ = get_bool(yaml_config, "deterministic");
    }
    if (yaml_config["seed"]) {
      config.seed_ = get_int(yaml_config, "seed");
    }
    // Maybe a map should be used instead, or only allow ints
    if (yaml_config["transfer_decisions"] && yaml_config["transfer_decisions"].IsMap()) {
      auto const& td = yaml_config["transfer_decisions"];
      if (td["criterion"]) {
        std::string crit_str = get_str(td, "criterion");
        if (crit_str == "Grapevine") {
          config.criterion_ = algo::temperedlb::CriterionEnum::Grapevine;
        } else if (crit_str == "ModifiedGrapevine") {
          config.criterion_ = algo::temperedlb::CriterionEnum::ModifiedGrapevine;
        } else {
          fmt::print("Unknown criterion type: '{}'\n", crit_str);
          throw std::runtime_error("Unknown criterion type");
        }
      }
      if (td["obj_ordering"]) {
        std::string order_str = get_str(td, "obj_ordering");
        if (order_str == "Arbitrary") {
          config.obj_ordering_ = algo::temperedlb::TransferUtil::ObjectOrder::Arbitrary;
        } else if (order_str == "ElmID") {
          config.obj_ordering_ = algo::temperedlb::TransferUtil::ObjectOrder::ElmID;
        } else if (order_str == "FewestMigrations") {
          config.obj_ordering_ = algo::temperedlb::TransferUtil::ObjectOrder::FewestMigrations;
        } else if (order_str == "SmallObjects") {
          config.obj_ordering_ = algo::temperedlb::TransferUtil::ObjectOrder::SmallObjects;
        } else if (order_str == "LargestObjects") {
          config.obj_ordering_ = algo::temperedlb::TransferUtil::ObjectOrder::LargestObjects;
        } else {
          fmt::print("Unknown object ordering type: '{}'\n", order_str);
          throw std::runtime_error("Unknown object ordering type");
        }
      }
      if (td["cmf_type"]) {
        std::string cmf_str = get_str(td, "cmf_type");
        if (cmf_str == "Original") {
          config.cmf_type_ = algo::temperedlb::TransferUtil::CMFType::Original;
        } else if (cmf_str == "NormByMax") {
          config.cmf_type_ = algo::temperedlb::TransferUtil::CMFType::NormByMax;
        } else if (cmf_str == "NormByMaxExcludeIneligible") {
          config.cmf_type_ = algo::temperedlb::TransferUtil::CMFType::NormByMaxExcludeIneligible;
        } else {
          fmt::print("Unknown CMF type: '{}'\n", cmf_str);
          throw std::runtime_error("Unknown CMF type");
        }
      }
    }

    // Work model
    if (yaml_config["work_model"] && yaml_config["work_model"].IsMap()) {
      auto const& wm = yaml_config["work_model"];
      if (wm["parameters"] && wm["parameters"].IsMap()) {
        auto const& params = wm["parameters"];
        if (params["rank_alpha"]) {
          config.work_model_.rank_alpha = get_double(params, "rank_alpha");
        }
        if (params["beta"]) {
          config.work_model_.beta = get_double(params, "beta");
        }
        if (params["gamma"]) {
          config.work_model_.gamma = get_double(params, "gamma");
        }
        if (params["delta"]) {
          config.work_model_.delta = get_double(params, "delta");
        }
      }
      if (wm["memory_info"] && wm["memory_info"].IsMap()) {
        auto const& mem_info = wm["memory_info"];
        if (mem_info["has_mem_info"]) {
          config.work_model_.has_memory_info = get_bool(mem_info, "has_mem_info");
        }
        if (mem_info["has_task_serialized_mem_info"]) {
          config.work_model_.has_task_serialized_memory_info = get_bool(mem_info, "has_task_serialized_mem_info");
        }
        if (mem_info["has_task_working_mem_info"]) {
          config.work_model_.has_task_working_memory_info = get_bool(mem_info, "has_task_working_mem_info");
        }
        if (mem_info["has_task_footprint_mem_info"]) {
          config.work_model_.has_task_footprint_memory_info = get_bool(mem_info, "has_task_footprint_mem_info");
        }
        if (mem_info["has_shared_block_mem_info"]) {
          config.work_model_.has_shared_block_memory_info = get_bool(mem_info, "has_shared_block_mem_info");
        }
      }
    }

    // Clustering
    if (yaml_config["clustering"] && yaml_config["clustering"].IsMap()) {
      auto const& clustering = yaml_config["clustering"];
      if (clustering["based_on_shared_blocks"]) {
        config.cluster_based_on_shared_blocks_ = get_bool(clustering, "based_on_shared_blocks");
      }
      if (clustering["based_on_communication"]) {
        config.cluster_based_on_communication_ = get_bool(clustering, "based_on_communication");
      }
      if (clustering["visualize_task_graph"]) {
        config.visualize_task_graph_ = get_bool(clustering, "visualize_task_graph");
      }
      if (clustering["visualize_clusters"]) {
        config.visualize_clusters_ = get_bool(clustering, "visualize_clusters");
      }
      if (clustering["visualize_full_graph"]) {
        config.visualize_full_graph_ = get_bool(clustering, "visualize_full_graph");
      }
    }

    if (yaml_config["converge_tolerance"]) {
      config.converge_tolerance_ = get_double(yaml_config, "converge_tolerance");
    }
  }
  return config;
}

// Helper methods
int YAMLReader::get_int(YAML::Node const& j, char const* k) {
  if (!j[k]) {
    fmt::print("Missing required key '{}' (expected integer)\n", k);
    throw std::runtime_error("Missing required key");
  }
  if (!j[k].IsScalar()) {
    fmt::print("Key '{}' has wrong type: expected scalar\n", k);
    throw std::runtime_error("Key has wrong type");
  }
  try {
    return j[k].as<int>();
  } catch (const YAML::BadConversion &e) {
    fmt::print("Key '{}' cannot be converted to integer: {}\n", k, e.what());
    throw std::runtime_error("Key cannot be converted to integer");
  }
}
double YAMLReader::get_double(YAML::Node const &j, char const *k) {
  if (!j[k]) {
    fmt::print("Missing required key '{}' (expected number)\n", k);
    throw std::runtime_error("Missing required key");
  }
  if (!j[k].IsScalar()) {
    fmt::print("Key '{}' has wrong type: expected scalar\n", k);
    throw std::runtime_error("Key has wrong type");
  }
  try {
    return j[k].as<double>();
  } catch (const YAML::BadConversion &e) {
    fmt::print("Key '{}' cannot be converted to number: {}\n", k, e.what());
    throw std::runtime_error("Key cannot be converted to number");
  }
}
std::string YAMLReader::get_str(YAML::Node const &j, char const *k) {
  if (!j[k]) {
    fmt::print("Missing required key '{}' (expected string)\n", k);
    throw std::runtime_error("Missing required key");
  }
  if (!j[k].IsScalar()) {
    fmt::print("Key '{}' has wrong type: expected scalar\n", k);
    throw std::runtime_error("Key has wrong type");
  }
  return j[k].as<std::string>();
}
bool YAMLReader::get_bool(YAML::Node const &j, char const *k) {
  if (!j[k]) {
    fmt::print("Missing required key '{}' (expected boolean)\n", k);
    throw std::runtime_error("Missing required key");
  }
  if (!j[k].IsScalar()) {
    fmt::print("Key '{}' has wrong type: expected scalar\n", k);
    throw std::runtime_error("Key has wrong type");
  }
  try {
    return j[k].as<bool>();
  } catch (const YAML::BadConversion &e) {
    fmt::print("Key '{}' cannot be converted to boolean: {}\n", k, e.what());
    throw std::runtime_error("Key cannot be converted to boolean");
  }
}
std::vector<int> YAMLReader::get_index_vec(YAML::Node const &j) {
  std::vector<int> idx;
  if (j["index"]) {
    if (!j["index"].IsSequence()) {
      fmt::print("Key 'index' has wrong type: got '{}', expected sequence\n",
                 type_name_of(j["index"]));
      throw std::runtime_error("Key 'index' has wrong type");
    }
    idx.reserve(j["index"].size());
    for (auto const &v : j["index"]) {
      if (!v.IsScalar()) {
        fmt::print("Key '{}' has wrong type: expected scalar\n", YAML::Dump(v));
        throw std::runtime_error("Key 'index' has wrong type");
      }
      idx.push_back(v.as<int>());
    }
  }
  return idx;
}
std::string YAMLReader::type_name_of(YAML::Node const &v) {
  if (v.IsNull())
    return "null";
  if (v.IsScalar())
    return "scalar";
  if (v.IsSequence())
    return "sequence";
  if (v.IsMap())
    return "map";
  return "unknown";
}
} /* end namespace vt_lb::input */
