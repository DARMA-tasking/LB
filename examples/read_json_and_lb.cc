/*
//@HEADER
// *****************************************************************************
//
//                              read_json_and_lb.h
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

#include <vt-lb/comm/MPI/comm_mpi.h>
#include <vt-lb/algo/driver/driver.h>
#include <vt-lb/input/json_reader.h>
#include <vt-lb/algo/temperedlb/temperedlb.h>

#include <filesystem>
#include <string>
#include <cstdio>
#include <optional>

namespace fs = std::filesystem;

static std::optional<std::string> find_rank_file(const std::string& dir, int rank) {
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

  if (json_br_candidate) return json_br_candidate;
  if (json_candidate) return json_candidate;
  return std::nullopt;
}

int main(int argc, char** argv) {
  auto comm = vt_lb::comm::CommMPI();
  comm.init(argc, argv);

  if (argc < 3) {
    if (comm.getRank() == 0) {
      std::fprintf(stderr, "Usage: %s <directory-containing-per-rank-json> <phase-id>\n", argv[0]);
    }
    comm.finalize();
    return argc < 3 ? 1 : 0;
  }

  std::string dir = argv[1];
  int phase_id = std::stoi(argv[2]);
  int rank = comm.getRank();

  if (!fs::exists(dir) || !fs::is_directory(dir)) {
    std::fprintf(stderr, "%d: Error: '%s' is not a directory or does not exist\n", rank, dir.c_str());
    comm.finalize();
    return 1;
  }

  auto maybe_file = find_rank_file(dir, rank);
  if (!maybe_file) {
    std::fprintf(stderr, "%d: Error: no input file found in '%s' for rank %d (expected *.%d.json or *.%d.json.br)\n",
                 rank, dir.c_str(), rank, rank, rank);
    comm.finalize();
    return 1;
  }

  // Read and parse phase 1 for this rank
  vt_lb::input::JSONReader reader(rank);
  reader.readFile(*maybe_file);
  auto phase_data = reader.parse(phase_id);
  if (!phase_data) {
    std::fprintf(stderr, "%d: Error: failed to parse PhaseData from '%s'\n", rank, maybe_file->c_str());
    comm.finalize();
    return 1;
  }

  // Configure TemperedLB (deterministic single trial/iter example)
  vt_lb::algo::temperedlb::Configuration config{comm.numRanks()};
  config.num_trials_ = 1;
  config.num_iters_ = 8;
  config.cluster_based_on_communication_ = true;
  config.work_model_.beta = 0.2;
  // Optional visualization toggles:
  // config.visualize_task_graph_ = true;
  // config.visualize_full_graph_ = true;

  std::printf("%d: Running TemperedLB on '%s' (phase=1)\n", rank, maybe_file->c_str());

  vt_lb::runLB(
    vt_lb::DriverAlgoEnum::TemperedLB,
    comm,
    config,
    std::move(phase_data)
  );

  comm.finalize();
  return 0;
}

