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

// #include <vt-lb/comm/MPI/comm_mpi.h>
// #include <vt-lb/algo/driver/driver.h>
// #include <vt-lb/algo/temperedlb/temperedlb.h>
// #include <vt-lb/input/yaml_reader.h>

// #include <filesystem>
// #include <string>
// #include <cstdio>
// #include <optional>
// #include <fstream>

// namespace fs = std::filesystem;

// static std::optional<std::string> find_rank_file(const std::string& dir, int rank) {
//   std::string suffix_yaml    = "." + std::to_string(rank) + ".yaml";

//   std::optional<std::string> yaml_candidate;

//   for (auto const& entry : fs::directory_iterator(dir)) {
//     if (!entry.is_regular_file()) continue;
//     auto const fname = entry.path().filename().string();
//     std::fprintf(stderr, "Checking file: %s\n", fname.c_str());
//     if (fname.size() >= suffix_yaml.size() &&
//         fname.compare(fname.size() - suffix_yaml.size(), suffix_yaml.size(), suffix_yaml) == 0) {
//       yaml_candidate = entry.path().string();
//     }
//   }

//   return yaml_candidate;
// }

int main(int argc, char** argv) {
  // This file will be used to demonstrate the yaml utility
  return 0;
}