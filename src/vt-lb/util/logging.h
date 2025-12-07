/*
//@HEADER
// *****************************************************************************
//
//                                logging.h
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

#if !defined INCLUDED_VT_LB_UTIL_LOGGING_H
#define INCLUDED_VT_LB_UTIL_LOGGING_H

#include <fmt-lb/format.h>
#include <string_view>
#include <utility>

namespace vt_lb::util {

enum class Verbosity : int {
  terse = 0,
  normal = 1,
  verbose = 2
};

enum class Component : int {
  Communicator = 0,
  LoadBalancer = 1,
  Clusterer = 2,
  Visualizer = 3,
  Termination = 4
};

void setVerbosity(Verbosity v);
Verbosity getVerbosity();

void enableAll();
void disableAll();

void enable(Component c);
void disable(Component c);
bool isEnabled(Component c);

// Rank provider API
using RankProvider = int(*)();
void setRankProvider(RankProvider rp);
void clearRankProvider();

// Helpers to print names
std::string_view componentName(Component c);
std::string_view verbosityName(Verbosity v);

// Color toggle
void setColorEnabled(bool enabled);
bool getColorEnabled();

// Colored helpers
std::string_view componentColorName(Component c);
std::string_view verbosityColorName(Verbosity v);
std::string prefixColor();

// rank provider symbol is defined in logging.cc
extern RankProvider __vt_lb_util_rank_provider;

/**
 * Log a message formatted via fmt when the component is enabled and the current
 * verbosity is >= msg_verbosity. Prefix includes component, verbosity, and rank (if available).
 */
template <typename... Args>
inline void log(Component comp, Verbosity msg_verbosity, std::string_view fmt_str, Args&&... args) {
  if (isEnabled(comp) && static_cast<int>(getVerbosity()) >= static_cast<int>(msg_verbosity)) {
    auto const comp_str = getColorEnabled() ? componentColorName(comp) : componentName(comp);
    auto const verb_str = getColorEnabled() ? verbosityColorName(msg_verbosity) : verbosityName(msg_verbosity);
    auto const prefix = getColorEnabled() ? prefixColor() :  "LB:";
    if (__vt_lb_util_rank_provider) {
      auto const r = __vt_lb_util_rank_provider();
      fmt::print("{} [{}] ({}) {}: ", prefix, r, verb_str, comp_str);
    } else {
      fmt::print("{} ({}) {}: ", prefix, verb_str, comp_str);
    }
    fmt::print(fmt::runtime(fmt_str), std::forward<Args>(args)...);
    fflush(stdout);
  }
}
} /* end namespace vt_lb::util */

#endif /*INCLUDED_VT_LB_UTIL_LOGGING_H*/