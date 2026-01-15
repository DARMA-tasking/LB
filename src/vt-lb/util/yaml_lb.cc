/*
//@HEADER
// *****************************************************************************
//
//                                yaml_lb.cc
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
// * Redistributions in binary form, must reproduce the above copyright notice,
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

#include <vt-lb/util/yaml_lb.h>
#include <vt-lb/comm/MPI/comm_mpi.h>
#include <vt-lb/input/yaml_reader.h>
#include <vt-lb/input/json_reader.h>
#include <vt-lb/algo/driver/driver.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace vt_lb::util {
void YAML_LB::loadAndRun(std::string const& in_filename, vt_lb::comm::CommMPI& comm) {
  auto yaml_reader = vt_lb::input::YAMLReader();
  yaml_reader.readFile(in_filename);
  std::string jsonDir = yaml_reader.parseJSONRankPath();
  int phase_id = yaml_reader.parsePhaseID();
  vt_lb::DriverAlgoEnum algo = yaml_reader.parseAlgorithm();
  int rank = comm.getRank();

  if (!fs::exists(jsonDir) || !fs::is_directory(jsonDir)) {
    std::fprintf(stderr, "%d: Error: '%s' is not a directory or does not exist\n", rank, jsonDir.c_str());
    throw std::runtime_error("JSON directory does not exist");
  }
  auto maybe_file = findRankFile(jsonDir, rank);
  if (!maybe_file) {
    std::fprintf(stderr,
                 "%d: Error: no input file found in '%s' for rank %d (expected "
                 "*.%d.json or *.%d.json.br)\n",
                 rank, jsonDir.c_str(), rank, rank, rank);
    throw std::runtime_error("No input file found for rank");
  }

  vt_lb::input::JSONReader json_reader(rank);
  json_reader.readFile(*maybe_file);
  auto phase_data = json_reader.parse(phase_id);
  phase_data->setRank(comm.getRank());

  auto lb_config = yaml_reader.parseLBConfig(comm.numRanks());

  std::printf("%d: Running algorithm %d on '%s' (phase=%d)\n", rank, static_cast<int>(algo),
              maybe_file->c_str(), phase_id);
  vt_lb::runLB(algo, comm, lb_config,
               std::move(phase_data));
  return;
}

std::optional<std::string> YAML_LB::findRankFile(std::string const& dir, int rank) {
  std::string suffix_json_br = "." + std::to_string(rank) + ".json.br";
  std::string suffix_json    = "." + std::to_string(rank) + ".json";

  std::optional<std::string> json_candidate;
  std::optional<std::string> json_br_candidate;

  for (auto const& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    auto const fname = entry.path().filename().string();
    if (fname.size() >= suffix_json_br.size() &&
        fname.compare(fname.size() - suffix_json_br.size(), suffix_json_br.size(), suffix_json_br) == 0) {
      json_br_candidate = entry.path().string();
    } else if (fname.size() >= suffix_json.size() &&
               fname.compare(fname.size() - suffix_json.size(), suffix_json.size(), suffix_json) == 0) {
      json_candidate = entry.path().string();
    }
  }

  if (json_br_candidate && json_candidate) {
    fprintf(
      stderr,
      "Warning: both compressed (%s) and uncompressed (%s) files found for rank %d",
      json_br_candidate->c_str(), json_candidate->c_str(), rank
    );
    throw std::runtime_error("Multiple input file types found for rank");
  }

  if (json_br_candidate) return json_br_candidate;
  if (json_candidate) return json_candidate;
  return std::nullopt;
}
} /* end namespace vt_lb::util */
