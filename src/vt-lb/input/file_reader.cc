/*
//@HEADER
// *****************************************************************************
//
//                                file_reader.cc
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

#include <vt-lb/input/file_reader.h>
#include <vt-lb/input/json_reader.h>
#include <vt-lb/input/yaml_reader.h>

namespace vt_lb::input {

FileReader::FileReader(int in_rank, std::string const& in_filename) : rank_(in_rank) {
  // Determine file type by extension and create appropriate reader
  if ((in_filename.size() >= 5 &&
       in_filename.substr(in_filename.size() - 5) == ".json") ||
      (in_filename.size() >= 8 &&
       in_filename.substr(in_filename.size() - 8) == ".json.br")) {
    read_behavior = std::make_unique<JSONReader>();
  } else if (in_filename.size() >= 5 &&
             in_filename.substr(in_filename.size() - 5) == ".yaml") {
    read_behavior = std::make_unique<YAMLReader>();
  } else {
    throw std::runtime_error("Unsupported file format: " + in_filename);
  }
  read_behavior->readFile(in_filename);
}

std::unique_ptr<model::PhaseData> FileReader::parse(int phase) {
  read_behavior->parse(phase);

  // Build PhaseData
  auto pd = std::make_unique<model::PhaseData>();

  auto get_id = [](EntityDesc const& e) -> int {
    return e.id.has_value() ? e.id.value() : e.seq_id.value();
  };

  // Map parsed tasks into PhaseData
  for (auto const& td : read_behavior->tasks) {
    model::TaskMemory memory{};
    // simple fallback if id is not available to seq_id
    auto id = get_id(td.entity);
    pd->addTask(
      model::Task(id, td.entity.home.value(), td.node, td.entity.migratable, memory, td.time)
    );
  }

  // Map communications into PhaseData
  for (auto const& cd : read_behavior->comms) {
    auto from_id = get_id(cd.from);
    auto to_id = get_id(cd.to);
    auto from_rank = pd->getTask(from_id) != nullptr ? pd->getTask(from_id)->getCurrent() : model::invalid_rank;
    auto to_rank = pd->getTask(to_id) != nullptr ? pd->getTask(to_id)->getCurrent() : model::invalid_rank;
    pd->addCommunication(model::Edge(from_id, to_id, cd.bytes, from_rank, to_rank));
  }

  return pd;
}


} /* end namespace vt_lb::input */
