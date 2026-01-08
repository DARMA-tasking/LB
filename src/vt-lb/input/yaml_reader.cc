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
#include <vt-lb/input/input_iterator.h>

#include <yaml-cpp/yaml.h>

#include <optional>

namespace vt_lb::input {

void YAMLReader::readFile(std::string const& in_filename) {
  try {
    yaml_ = std::make_unique<YAML::Node>(YAML::LoadFile(in_filename));
  } catch (const YAML::Exception& e) {
    fmt::print("Failed to load YAML file '{}': {}\n", in_filename, e.what());
    assert(false && "Failed to load YAML file");
  }
}
void YAMLReader::parse(int phase) {
  assert(yaml_ != nullptr && "Must have valid yaml");

  auto const& root = *yaml_;

  // Find the requested phase entry
  if (!root["phases"] || !root["phases"].IsSequence()) {
    fmt::print("YAML missing 'phases' array\n");
    assert(false);
    return;
  }

  YAML::Node phase_obj;
  if (root["phases"].IsSequence()) {
    for (const YAML::Node& p : root["phases"]) {
      fmt::print("\n\nChecking phase entry: {}\n", YAML::Dump(p));
      if (p["id"] && p["id"].IsScalar()) {
        int p_id = p["id"].as<int>();
        if (p_id == phase) {
          phase_obj = p;
          break;
        }
      }
    }
  }

  fmt::print("Found phase object: {}\n", YAML::Dump(phase_obj));

  if (!phase_obj) {
    fmt::print("Requested phase {} not found\n", phase);
    return;
  }

  // Helpers
  auto get_int_opt = [](YAML::Node const& j, char const* k) -> std::optional<int> {
    if (j[k] && j[k].IsScalar()) {
      try {
        return std::optional<int>{j[k].as<int>()};
      } catch (...) {
        return std::nullopt;
      }
    }
    return std::nullopt;
  };
  auto type_name_of = [](YAML::Node const& v) -> std::string {
    if (v.IsNull()) return "null";
    if (v.IsScalar()) return "scalar";
    if (v.IsSequence()) return "sequence";
    if (v.IsMap()) return "map";
    return "unknown";
  };
  auto get_double_req = [&](YAML::Node const& j, char const* k) -> double {
    if (!j[k]) {
      fmt::print("Missing required key '{}' (expected number)\n", k);
      assert(false);
    }
    if (!j[k].IsScalar()) {
      fmt::print("Key '{}' has wrong type: expected scalar\n", k);
      assert(false);
    }
    try {
      return j[k].as<double>();
    } catch (const YAML::BadConversion& e) {
      fmt::print("Key '{}' cannot be converted to number: {}\n", k, e.what());
      assert(false);
      return 0.0;
    }
  };
  auto get_int_req = [&](YAML::Node const& j, char const* k) -> int {
    if (!j[k]) {
      fmt::print("Missing required key '{}' (expected integer)\n", k);
      assert(false);
    }
    if (!j[k].IsScalar()) {
      fmt::print("Key '{}' has wrong type: expected scalar\n", k);
      assert(false);
    }
    try {
      return j[k].as<int>();
    } catch (const YAML::BadConversion& e) {
      fmt::print("Key '{}' cannot be converted to integer: {}\n", k, e.what());
      assert(false);
      return 0;
    }
  };
  auto get_str_req = [&](YAML::Node const& j, char const* k) -> std::string {
    if (!j[k]) {
      fmt::print("Missing required key '{}' (expected string)\n", k);
      assert(false);
    }
    if (!j[k].IsScalar()) {
      fmt::print("Key '{}' has wrong type: expected scalar\n", k);
      assert(false);
    }
    return j[k].as<std::string>();
};
    auto get_bool_req = [&](YAML::Node const& j, char const* k) -> bool {
        if (!j[k]) {
        fmt::print("Missing required key '{}' (expected boolean)\n", k);
        assert(false);
        }
        if (!j[k].IsScalar()) {
        fmt::print("Key '{}' has wrong type: expected scalar\n", k);
        assert(false);
        }
        try {
        return j[k].as<bool>();
        } catch (const YAML::BadConversion& e) {
        fmt::print("Key '{}' cannot be converted to boolean: {}\n", k, e.what());
        assert(false);
        return false;
        }
    };
  auto get_index_vec = [&](YAML::Node const& j) -> std::vector<int> {
    std::vector<int> idx;
    if (j["index"]) {
      if (!j["index"].IsSequence()) {
        fmt::print("Key 'index' has wrong type: got '{}', expected sequence\n", type_name_of(j["index"]));
        assert(false);
      }
      idx.reserve(j["index"].size());
      for (auto const& v : j["index"]) {
        if (!v.IsScalar()) {
        fmt::print("Key '{}' has wrong type: expected scalar\n", YAML::Dump(v));
          assert(false);
        }
        idx.push_back(v.as<int>());
      }
    }
    return idx;
  };
  auto validate_entity_ids = [](YAML::Node const& entity) {
    bool has_id = entity["id"] && entity["id"].IsDefined();
    bool has_seq_id = entity["seq_id"] && entity["seq_id"].IsDefined();
    if (!has_id && !has_seq_id) {
      // Print a compact view of the offending entity
      fmt::print("Either 'id' (bit-encoded) or 'seq_id' must be provided for 'entity'. Offending entity: {}\n",
                 YAML::Dump(entity));
      assert(false);
    }
  };

  auto parse_entity = [&](YAML::Node const& entity_yaml) -> EntityDesc {
    validate_entity_ids(entity_yaml);
    EntityDesc e;
    e.type = get_str_req(entity_yaml, "type");
    e.migratable = get_bool_req(entity_yaml, "migratable");
    e.id = get_int_opt(entity_yaml, "id");
    e.seq_id = get_int_opt(entity_yaml, "seq_id");
    e.collection_id = get_int_opt(entity_yaml, "collection_id");
    e.home = get_int_opt(entity_yaml, "home");
    e.objgroup_id = get_int_opt(entity_yaml, "objgroup_id");
    e.index = get_index_vec(entity_yaml);
    return e;
  };

  // Parse tasks
  if (phase_obj["tasks"] && phase_obj["tasks"].IsSequence()) {
    tasks.reserve(phase_obj["tasks"].size());
    for (auto const& t : phase_obj["tasks"]) {
      if (!t["entity"] || !t["entity"].IsMap()) {
        fmt::print("Task missing 'entity' map\n");
        assert(false);
      }
      TaskDesc td;
      td.entity = parse_entity(t["entity"]);
      td.node = get_int_req(t, "node");
      td.resource = get_str_req(t, "resource");
      td.time = get_double_req(t, "time");
      if (t["subphases"] && t["subphases"].IsSequence()) {
        for (auto const& sp : t["subphases"]) {
          SubphaseDesc sd{get_int_req(sp, "id"), get_double_req(sp, "time")};
          td.subphases.push_back(sd);
        }
      }
      // if (t["attributes"] && t["attributes"].IsMap()) {
      //   td.attributes = t["attributes"];
      // }
      // if (t["user_defined"] && t["user_defined"].IsMap()) {
      //   td.user_defined = t["user_defined"];
      // }
      tasks.push_back(std::move(td));
    }
  }

  // Parse communications
  if (phase_obj["communications"] && phase_obj["communications"].IsSequence()) {
    comms.reserve(phase_obj["communications"].size());
    for (auto const& c : phase_obj["communications"]) {
      CommDesc cd;
      cd.type = get_str_req(c, "type");
      if (!c["from"] || !c["from"].IsMap() || !c["to"] || !c["to"].IsMap()) {
        fmt::print("Communication entries must contain 'from' and 'to' maps\n");
        assert(false);
      }
      cd.from = parse_entity(c["from"]);
      cd.to = parse_entity(c["to"]);
      cd.messages = get_int_req(c, "messages");
      cd.bytes = get_double_req(c, "bytes");
      comms.push_back(std::move(cd));
    }
  }
}
} /* end namespace vt_lb::input */