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
#include <stdlib.h>
#include <string.h>

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

std::unique_ptr<model::PhaseData> JSONReader::parse() {
  using json = nlohmann::json;

  assert(json_ != nullptr && "Must have valid json");
}

} /* end namespace vt_lb::input */
