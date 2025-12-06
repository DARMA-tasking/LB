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
  Verbosity verbosity{Verbosity::normal};
  std::array<bool, 16> components;
  LoggingState() {
    for (auto& c : components) c = false;
    components[static_cast<int>(Component::Communicator)] = false;
    components[static_cast<int>(Component::LoadBalancer)] = true;
    components[static_cast<int>(Component::Clusterer)] = true;
    components[static_cast<int>(Component::Termination)] = false;
    components[static_cast<int>(Component::Visualizer)] = true;
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
    case Component::LoadBalancer:   return "LoadBalancer";
    case Component::Clusterer: return "Clusterer";
    case Component::Visualizer: return "Visualizer";
    case Component::Termination: return "Termination";
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
constexpr std::string_view FG_CYAN = "\033[36m";
constexpr std::string_view FG_RED = "\033[31m";
constexpr std::string_view FG_BD_GREEN = "\033[32;1m";

// Helper to map Component to index used in arrays
static inline size_t componentIndex(Component c) {
  return static_cast<size_t>(c);
}

std::string prefixColor() {
  return std::string(FG_BD_GREEN) + std::string("LB:") + std::string(RESET);
}

// Precomputed names for components (plain and colored)
static const std::array<std::string, 16>& componentNamesPlain() {
  static const std::array<std::string, 16> names = []{
    std::array<std::string, 16> arr{};
    arr[static_cast<size_t>(Component::Communicator)] = std::string(componentName(Component::Communicator));
    arr[static_cast<size_t>(Component::LoadBalancer)] = std::string(componentName(Component::LoadBalancer));
    arr[static_cast<size_t>(Component::Clusterer)]    = std::string(componentName(Component::Clusterer));
    arr[static_cast<size_t>(Component::Visualizer)]   = std::string(componentName(Component::Visualizer));
    arr[static_cast<size_t>(Component::Termination)]  = std::string(componentName(Component::Termination));
    // Any unspecified indices remain empty; fallback handled in accessor
    return arr;
  }();
  return names;
}

static const std::array<std::string, 16>& componentNamesColor() {
  static const std::array<std::string, 16> names = []{
    std::array<std::string, 16> arr{};
    arr[static_cast<size_t>(Component::Communicator)] = std::string(FG_BLUE)   + std::string(componentName(Component::Communicator)) + std::string(RESET);
    arr[static_cast<size_t>(Component::LoadBalancer)] = std::string(FG_CYAN)   + std::string(componentName(Component::LoadBalancer)) + std::string(RESET);
    arr[static_cast<size_t>(Component::Clusterer)]    = std::string(FG_MAGENTA)+ std::string(componentName(Component::Clusterer)) + std::string(RESET);
    arr[static_cast<size_t>(Component::Visualizer)]   = std::string(FG_YELLOW) + std::string(componentName(Component::Visualizer)) + std::string(RESET);
    arr[static_cast<size_t>(Component::Termination)]  = std::string(FG_GREEN)  + std::string(componentName(Component::Termination)) + std::string(RESET);
    // Unknown fallback
    arr[0] = arr[0].empty() ? std::string(FG_MAGENTA) + "Unknown" + std::string(RESET) : arr[0];
    return arr;
  }();
  return names;
}

// Colorized names (wrapped and reset)
std::string_view componentColorName(Component c) {
  const auto idx = componentIndex(c);
  const auto& names = getColorEnabled() ? componentNamesColor() : componentNamesPlain();
  // If name not set for this index, fallback to Unknown (colored/plain accordingly)
  if (idx < names.size() && !names[idx].empty()) {
    return std::string_view{names[idx]};
  }
  static const std::string unknown_plain = "Unknown";
  static const std::string unknown_color = std::string(FG_MAGENTA) + "Unknown" + std::string(RESET);
  return std::string_view{ getColorEnabled() ? unknown_color : unknown_plain };
}

// Precomputed names for verbosity (plain and colored)
static const std::array<std::string, 4>& verbosityNamesPlain() {
  static const std::array<std::string, 4> names = []{
    std::array<std::string, 4> arr{};
    arr[static_cast<size_t>(Verbosity::terse)]   = std::string(verbosityName(Verbosity::terse));
    arr[static_cast<size_t>(Verbosity::normal)]  = std::string(verbosityName(Verbosity::normal));
    arr[static_cast<size_t>(Verbosity::verbose)] = std::string(verbosityName(Verbosity::verbose));
    arr[3] = "unknown";
    return arr;
  }();
  return names;
}

static const std::array<std::string, 4>& verbosityNamesColor() {
  static const std::array<std::string, 4> names = []{
    std::array<std::string, 4> arr{};
    arr[static_cast<size_t>(Verbosity::terse)]   = std::string(FG_GREEN)  + std::string(verbosityName(Verbosity::terse))   + std::string(RESET);
    arr[static_cast<size_t>(Verbosity::normal)]  = std::string(FG_YELLOW) + std::string(verbosityName(Verbosity::normal))  + std::string(RESET);
    arr[static_cast<size_t>(Verbosity::verbose)] = std::string(FG_MAGENTA)+ std::string(verbosityName(Verbosity::verbose)) + std::string(RESET);
    arr[3] = std::string(FG_MAGENTA) + "unknown" + std::string(RESET);
    return arr;
  }();
  return names;
}

std::string_view verbosityColorName(Verbosity v) {
  const auto idx = static_cast<size_t>(v);
  const auto& names = getColorEnabled() ? verbosityNamesColor() : verbosityNamesPlain();
  return std::string_view{ names[idx < names.size() ? idx : names.size()-1] };
}

} /* end namespace vt_lb::util */