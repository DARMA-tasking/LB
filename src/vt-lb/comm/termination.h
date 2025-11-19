/*
//@HEADER
// *****************************************************************************
//
//                                termination.h
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

#if !defined INCLUDED_VT_LB_COMM_TERMINATION_H
#define INCLUDED_VT_LB_COMM_TERMINATION_H

#include <cstdint>
#include <memory>
#include "vt-lb/comm/class_handle.h"

namespace vt_lb::comm {

struct CommMPI;
template <typename T>
struct ClassHandle;

namespace detail {

struct TerminationDetector {
  static constexpr int kArity = 4;

  void init(CommMPI& comm, ClassHandle<TerminationDetector> handle);
  void startFirstWave();
  void onControl();
  void onResponse(uint64_t in_sent, uint64_t in_recv);
  void notifyMessageSend();
  void notifyMessageReceive();
  bool isTerminated() const { return terminated_; }
  void sendControlToChildren();
  void sendResponseToParent(uint64_t in_sent, uint64_t in_recv);
  void terminated();

  int rank_ = 0;
  int size_ = 0;
  int parent_ = -1;
  int first_child_ = 0;
  int num_children_ = 0;
  int waiting_children_ = 0;
  bool terminated_ = false;
  uint64_t sent_ = 0;
  uint64_t recv_ = 0;
  uint64_t global_sent1_ = 0, global_recv1_ = 0;
  uint64_t global_sent2_ = 0, global_recv2_ = 0;
  ClassHandle<TerminationDetector> handle_;
};

} // namespace detail
} // namespace vt_lb::comm

#endif /*INCLUDED_VT_LB_COMM_TERMINATION_H*/
