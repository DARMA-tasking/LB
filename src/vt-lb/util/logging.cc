/*
//@HEADER
// *****************************************************************************
//
//                                logging.cc
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
#include <vt-lb/util/logging.h>
#include <array>

namespace vt_lb::util {

// Simple global logging state
struct LoggingState {
  Verbosity verbosity{Verbosity::terse};
  std::array<bool, 16> components;
  LoggingState() {
    for (auto& c : components) c = false;
    components[static_cast<int>(Component::Communicator)] = true;
  }
  static LoggingState& instance() {
    static LoggingState s;
    return s;
  }
};

// Global rank provider symbol referenced from header (extern)
RankProvider __vt_lb_util_rank_provider = nullptr;

// Global color toggle
static bool g_color_enabled = true;

void setVerbosity(Verbosity v) {
  LoggingState::instance().verbosity = v;
}

Verbosity getVerbosity() {
  return LoggingState::instance().verbosity;
}

void enableAll() {
  auto& s = LoggingState::instance();
  for (auto& c : s.components) c = true;
}

void disableAll() {
  auto& s = LoggingState::instance();
  for (auto& c : s.components) c = false;
}

void enable(Component c) {
  LoggingState::instance().components[static_cast<int>(c)] = true;
}

void disable(Component c) {
  LoggingState::instance().components[static_cast<int>(c)] = false;
}

bool isEnabled(Component c) {
  return LoggingState::instance().components[static_cast<int>(c)];
}

void setRankProvider(RankProvider rp) {
  __vt_lb_util_rank_provider = rp;
}

void clearRankProvider() {
  __vt_lb_util_rank_provider = nullptr;
}

void setColorEnabled(bool enabled) { g_color_enabled = enabled; }
bool getColorEnabled() { return g_color_enabled; }

std::string_view componentName(Component c) {
  switch (c) {
    case Component::Communicator: return "Communicator";
    default: return "Unknown";
  }
}

std::string_view verbosityName(Verbosity v) {
  switch (v) {
    case Verbosity::terse: return "terse";
    case Verbosity::normal: return "normal";
    case Verbosity::verbose: return "verbose";
    default: return "unknown";
  }
}

// ANSI colors
constexpr std::string_view RESET = "\033[0m";
constexpr std::string_view FG_BLUE = "\033[34m";
constexpr std::string_view FG_GREEN = "\033[32m";
constexpr std::string_view FG_YELLOW = "\033[33m";
constexpr std::string_view FG_MAGENTA = "\033[35m";

// Colorized names (wrapped and reset)
std::string_view componentColorName(Component c) {
  switch (c) {
    case Component::Communicator: return "\033[34mCommunicator\033[0m"; // blue
    default: return "\033[35mUnknown\033[0m"; // magenta
  }
}

std::string_view verbosityColorName(Verbosity v) {
  switch (v) {
    case Verbosity::terse:   return "\033[32mterse\033[0m";   // green
    case Verbosity::normal:  return "\033[33mnormal\033[0m";  // yellow
    case Verbosity::verbose: return "\033[35mverbose\033[0m"; // magenta
    default: return "\033[35munknown\033[0m";
  }
}

} /* end namespace vt_lb::util */