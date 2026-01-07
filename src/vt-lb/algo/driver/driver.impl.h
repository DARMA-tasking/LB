/*
//@HEADER
// *****************************************************************************
//
//                                  driver.cc
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

#if !defined INCLUDED_VT_LB_ALGO_DRIVER_DRIVER_IMPL_H
#define INCLUDED_VT_LB_ALGO_DRIVER_DRIVER_IMPL_H

#include <vt-lb/config/cmake_config.h>

#include <vt-lb/algo/temperedlb/temperedlb.h>
#include <vt-lb/algo/driver/driver.h>

namespace vt_lb {

template <typename CommT, typename ConfigT>
void runLB(DriverAlgoEnum algo, CommT& comm, ConfigT config, std::unique_ptr<model::PhaseData> phase_data) {
  switch (algo) {
  case DriverAlgoEnum::None:
    // No load balancing
    break;
  case DriverAlgoEnum::TemperedLB:
    {
    // Run TemperedLB algorithm
    auto lb = std::make_unique<algo::temperedlb::TemperedLB<CommT>>(comm, config);
    lb->inputData(std::move(phase_data));
    lb->run();
    }
    break;
  default:
    throw std::runtime_error("Invalid load balancer algorithm");
  }
}

} /* end namespace vt_lb */

#endif /*INCLUDED_VT_LB_ALGO_DRIVER_DRIVER_IMPL_H*/