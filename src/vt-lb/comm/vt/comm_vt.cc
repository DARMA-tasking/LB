/*
//@HEADER
// *****************************************************************************
//
//                                comm_vt.cc
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

#include "vt-lb/comm/vt/comm_vt.h"

#if vt_backend_enabled

#include <vt/transport.h>

namespace vt_lb::comm {

void CommVT::init(int& argc, char**& argv, MPI_Comm comm) {
  if (comm == MPI_COMM_NULL) {
    vt::initialize(argc, argv);
  } else {
    // interop mode
    vt::initialize(argc, argv, &comm);
  }
  vt::theTerm()->addDefaultAction([this]{ terminated_ = true; });
}

void CommVT::finalize() {
  vt::finalize();
}

CommVT CommVT::clone() {
  auto ep = vt::theTerm()->makeEpochCollective("vt-lb::comm::CommVT::clone");
  return CommVT{ep};
}

CommVT::CommVT(vt::EpochType epoch) : epoch_(epoch) {
  vt::theTerm()->addAction(epoch_, [this]{ terminated_ = true; });
  vt::theTerm()->pushEpoch(epoch_);
  vt::theTerm()->finishedEpoch(epoch_);
}

CommVT::~CommVT() {
  if (epoch_ != vt::no_epoch) {
    vt::theTerm()->popEpoch(epoch_);
  }
}

int CommVT::numRanks() const {
  return static_cast<int>(vt::theContext()->getNumNodes());
}

int CommVT::getRank() const {
  return static_cast<int>(vt::theContext()->getNode());
}

bool CommVT::poll() const {
  vt::theSched()->runSchedulerOnceImpl();
  return !terminated_;
}

} // namespace vt_lb::comm

#endif /*vt_backend_enabled*/