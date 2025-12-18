/*
//@HEADER
// *****************************************************************************
//
//                                json_reader.cc
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

#include <vt-lb/input/json_reader.h>
#include <vt-lb/input/decompression_input_container.h>
#include <vt-lb/input/input_iterator.h>

#include <nlohmann-lb/json.hpp>

#include <fmt-lb/core.h>
#include <fmt-lb/format.h>

#include <fstream>
#include <iostream>
#include <cstdlib>
#include <string>
#include <optional>

namespace vt_lb::input {

bool JSONReader::isCompressed(std::string const& in_filename) const {
  bool compressed = true;

  // determine if the file is compressed or not
  std::ifstream is(in_filename);
  if (not is.good()) {
    auto str = fmt::format("Filename is not valid: {}", in_filename);
    fmt::print(fmt::runtime(str));
    assert(false && "Invalid file");
    return false;
  }
  char f = '\0';
  while (is.good()) {
    f = is.get();
    if (f == ' ' or f == '\t' or f == '\n') {
      continue;
    } else {
      break;
    }
  }
  if (f == '{') {
    compressed = false;
  }
  is.close();

  return compressed;
}

void JSONReader::readFile(std::string const& in_filename) {
  using json = nlohmann::json;

  if (isCompressed(in_filename)) {
    DecompressionInputContainer c(in_filename);
    json j = json::parse(c);
    json_ = std::make_unique<json>(std::move(j));
  } else {
    std::ifstream is(in_filename, std::ios::binary);
    assert(is.good() && "File must be good");
    json j = json::parse(is);
    is.close();
    json_ = std::make_unique<json>(std::move(j));
  }
}

void JSONReader::readString(std::string const& in_json_string) {
  using json = nlohmann::json;
  json j = json::parse(in_json_string);
  json_ = std::make_unique<json>(std::move(j));
}

std::unique_ptr<model::PhaseData> JSONReader::parse(int phase) {
  using json = nlohmann::json;

  assert(json_ != nullptr && "Must have valid json");

  auto const& root = *json_;

  // Find the requested phase entry
  if (!root.contains("phases") || !root["phases"].is_array()) {
    fmt::print("JSON missing 'phases' array\n");
    assert(false);
    return {};
  }

  json const* phase_obj = nullptr;
  for (auto const& p : root["phases"]) {
    if (p.contains("id") && p["id"].is_number_integer() && p["id"].get<int>() == phase) {
      phase_obj = &p;
      break;
    }
  }

  if (phase_obj == nullptr) {
    fmt::print("Requested phase {} not found\n", phase);
    return {}; // or assert if phase must exist
  }

  // Helpers
  auto get_int_opt = [](json const& j, char const* k) -> std::optional<int> {
    return j.contains(k) && j[k].is_number_integer() ? std::optional<int>{j[k].get<int>()} : std::nullopt;
  };
  // Small utility to stringify the json type we actually saw
  auto type_name_of = [](json const& v) -> std::string {
    // nlohmann::json exposes type_name(); fallback to string if needed
    return v.type_name();
  };
  auto get_double_req = [&](json const& j, char const* k) -> double {
    if (!j.contains(k)) {
      fmt::print("Missing required key '{}' (expected number)\n", k);
      assert(false);
    }
    if (!(j[k].is_number_float() || j[k].is_number_integer())) {
      fmt::print("Key '{}' has wrong type: got '{}', expected number\n", k, type_name_of(j[k]));
      assert(false);
    }
    return j[k].is_number_float() ? j[k].get<double>() : static_cast<double>(j[k].get<long long>());
  };
  auto get_int_req = [&](json const& j, char const* k) -> int {
    if (!j.contains(k)) {
      fmt::print("Missing required key '{}' (expected integer)\n", k);
      assert(false);
    }
    if (!j[k].is_number_integer()) {
      fmt::print("Key '{}' has wrong type: got '{}', expected integer\n", k, type_name_of(j[k]));
      assert(false);
    }
    return j[k].get<int>();
  };
  auto get_str_req = [&](json const& j, char const* k) -> std::string {
    if (!j.contains(k)) {
      fmt::print("Missing required key '{}' (expected string)\n", k);
      assert(false);
    }
    if (!j[k].is_string()) {
      fmt::print("Key '{}' has wrong type: got '{}', expected string\n", k, type_name_of(j[k]));
      assert(false);
    }
    return j[k].get<std::string>();
  };
  auto get_bool_req = [&](json const& j, char const* k) -> bool {
    if (!j.contains(k)) {
      fmt::print("Missing required key '{}' (expected boolean)\n", k);
      assert(false);
    }
    if (!j[k].is_boolean()) {
      fmt::print("Key '{}' has wrong type: got '{}', expected boolean\n", k, type_name_of(j[k]));
      assert(false);
    }
    return j[k].get<bool>();
  };
  auto get_index_vec = [&](json const& j) -> std::vector<int> {
    std::vector<int> idx;
    if (j.contains("index")) {
      if (!j["index"].is_array()) {
        fmt::print("Key 'index' has wrong type: got '{}', expected array\n", type_name_of(j["index"]));
        assert(false);
      }
      idx.reserve(j["index"].size());
      for (auto const& v : j["index"]) {
        if (!v.is_number_integer()) {
          fmt::print("Index entries must be integers; got element of type '{}'\n", type_name_of(v));
          assert(false);
        }
        idx.push_back(v.get<int>());
      }
    }
    return idx;
  };
  auto validate_entity_ids = [](json const& entity) {
    bool has_id = entity.contains("id");
    bool has_seq_id = entity.contains("seq_id");
    if (!has_id && !has_seq_id) {
      // Print a compact view of the offending entity
      fmt::print("Either 'id' (bit-encoded) or 'seq_id' must be provided for 'entity'. Offending entity: {}\n",
                 entity.dump());
      assert(false);
    }
  };

  struct EntityDesc {
    // Identification and placement
    std::optional<int> id;
    std::optional<int> seq_id;
    std::optional<int> collection_id;
    std::optional<int> home;
    std::optional<int> objgroup_id;
    bool migratable = false;
    std::string type;
    std::vector<int> index;
  };

  auto parse_entity = [&](json const& entity_json) -> EntityDesc {
    validate_entity_ids(entity_json);
    EntityDesc e;
    e.type = get_str_req(entity_json, "type");
    e.migratable = get_bool_req(entity_json, "migratable");
    e.id = get_int_opt(entity_json, "id");
    e.seq_id = get_int_opt(entity_json, "seq_id");
    e.collection_id = get_int_opt(entity_json, "collection_id");
    e.home = get_int_opt(entity_json, "home");
    e.objgroup_id = get_int_opt(entity_json, "objgroup_id");
    e.index = get_index_vec(entity_json);
    return e;
  };

  struct SubphaseDesc { int id; double time; };
  struct TaskDesc {
    EntityDesc entity;
    int node;
    std::string resource;
    double time;
    std::vector<SubphaseDesc> subphases;
    json attributes;     // optional dict
    json user_defined;   // optional dict
  };
  struct CommDesc {
    EntityDesc to;
    EntityDesc from;
    std::string type;
    int messages;
    double bytes;
  };

  std::vector<TaskDesc> tasks;
  std::vector<CommDesc> comms;

  // Parse tasks
  if (phase_obj->contains("tasks") && (*phase_obj)["tasks"].is_array()) {
    tasks.reserve((*phase_obj)["tasks"].size());
    for (auto const& t : (*phase_obj)["tasks"]) {
      if (!t.contains("entity") || !t["entity"].is_object()) {
        fmt::print(fmt::runtime("Task missing 'entity' object\n")); assert(false);
      }
      TaskDesc td;
      td.entity = parse_entity(t["entity"]);
      td.node = get_int_req(t, "node");
      td.resource = get_str_req(t, "resource");
      td.time = get_double_req(t, "time");
      if (t.contains("subphases") && t["subphases"].is_array()) {
        for (auto const& sp : t["subphases"]) {
          SubphaseDesc sd{get_int_req(sp, "id"), get_double_req(sp, "time")};
          td.subphases.push_back(sd);
        }
      }
      if (t.contains("attributes") && t["attributes"].is_object()) {
        td.attributes = t["attributes"];
      }
      if (t.contains("user_defined") && t["user_defined"].is_object()) {
        td.user_defined = t["user_defined"];
      }
      tasks.push_back(std::move(td));
    }
  }

  // Parse communications
  if (phase_obj->contains("communications") && (*phase_obj)["communications"].is_array()) {
    comms.reserve((*phase_obj)["communications"].size());
    for (auto const& c : (*phase_obj)["communications"]) {
      CommDesc cd;
      cd.type = get_str_req(c, "type");
      if (!c.contains("from") || !c["from"].is_object() || !c.contains("to") || !c["to"].is_object()) {
        fmt::print(fmt::runtime("Communication entries must contain 'from' and 'to' objects\n"));
        assert(false);
      }
      cd.from = parse_entity(c["from"]);
      cd.to = parse_entity(c["to"]);
      cd.messages = get_int_req(c, "messages");
      cd.bytes = get_double_req(c, "bytes");
      comms.push_back(std::move(cd));
    }
  }

  // Build PhaseData
  auto pd = std::make_unique<model::PhaseData>();

  auto get_id = [](EntityDesc const& e) -> int {
    return e.id.has_value() ? e.id.value() : e.seq_id.value();
  };

  // Map parsed tasks into PhaseData
  for (auto const& td : tasks) {
    model::TaskMemory memory{};
    // simple fallback if id is not available to seq_id
    auto id = get_id(td.entity);
    pd->addTask(
      model::Task(id, td.entity.home.value(), td.node, td.entity.migratable, memory, td.time)
    );
  }

  // Map communications into PhaseData
  for (auto const& cd : comms) {
    auto from_id = get_id(cd.from);
    auto to_id = get_id(cd.to);
    auto from_rank = pd->getTask(from_id) != nullptr ? pd->getTask(from_id)->getCurrent() : model::invalid_node;
    auto to_rank = pd->getTask(to_id) != nullptr ? pd->getTask(to_id)->getCurrent() : model::invalid_node;
    pd->addCommunication(model::Edge(from_id, to_id, cd.bytes, from_rank, to_rank));
  }

  return pd;
}

} /* end namespace vt_lb::input */
