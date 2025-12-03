/*
//@HEADER
// *****************************************************************************
//
//                                termination.cc
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

#include "vt-lb/comm/MPI/termination.h"
#include "vt-lb/comm/MPI/comm_mpi.h"

#define DEBUG_TERMINATION 0

#if DEBUG_TERMINATION
#include <cinttypes>
#endif

namespace vt_lb::comm::detail {

void TerminationDetector::init(CommMPI& comm, ClassHandle<TerminationDetector> handle) {
  handle_ = handle;
  rank_ = comm.getRank();
  size_ = comm.numRanks();
  parent_ = (rank_ == 0) ? -1 : (rank_ - 1) / kArity;
  first_child_ = rank_ * kArity + 1;
  num_children_ = std::min(kArity, std::max(0, size_ - first_child_));

  startFirstWave();
}

void TerminationDetector::startFirstWave() {
  if (rank_ == 0) {
    waiting_children_ = num_children_;
    sendControlToChildren();
  }
}

void TerminationDetector::sendControlToChildren() {
#if DEBUG_TERMINATION
  printf("Rank %d: sending control to %d children\n", rank_, num_children_);
#endif
  for (int i = 0; i < num_children_; i++) {
    handle_[first_child_ + i].sendTerm<&TerminationDetector::onControl>();
  }

  if (singleRank()) {
    // Devolved case with one rank, move forward
    checkAllChildrenComplete();
  }
}

void TerminationDetector::sendResponseToParent(uint64_t in_sent, uint64_t in_recv) {
#if DEBUG_TERMINATION
  printf("Rank %d: sending response to parent %d: sent=%" PRIu64 ", recv=%" PRIu64 "\n",
         rank_, parent_, in_sent, in_recv);
#endif
  handle_[parent_].sendTerm<&TerminationDetector::onResponse>(in_sent, in_recv);
}

void TerminationDetector::onControl() {
#if DEBUG_TERMINATION
  printf("Rank %d: received control message, num_children_=%d\n",
         rank_, num_children_);
#endif
  waiting_children_ = num_children_;
  // Forward control to children
  if (num_children_ > 0) {
    sendControlToChildren();
  } else {
    // Leaf node - send response immediately
    sendResponseToParent(sent_, recv_);
  }
}

void TerminationDetector::onResponse(uint64_t in_sent, uint64_t in_recv) {
#if DEBUG_TERMINATION
  printf("Rank %d: received response: sent=%" PRIu64 ", recv=%" PRIu64 ", global_sent1=%" PRIu64 ", global_recv1=%" PRIu64 " waiting_children=%d\n",
         rank_, in_sent, in_recv, global_sent1_, global_recv1_, waiting_children_);
#endif

  global_sent1_ += in_sent;
  global_recv1_ += in_recv;

  waiting_children_--;

  checkAllChildrenComplete();
}

void TerminationDetector::checkAllChildrenComplete() {
  if (waiting_children_ == 0) {

#if DEBUG_TERMINATION
    printf("Rank %d: aggregated total: sent=%" PRIu64 ", recv=%" PRIu64 "\n",
           rank_, global_sent1_, global_recv1_);
#endif

    if (rank_ == 0) {
      // Root checks for termination

      global_sent1_ += sent_;
      global_recv1_ += recv_;

#if DEBUG_TERMINATION
      printf("Root total: s1=%" PRIu64 ", r1=%" PRIu64 ", s2=%" PRIu64 ", r2=%" PRIu64 "\n",
             global_sent1_, global_recv1_, global_sent2_, global_recv2_);
#endif

      if (global_sent1_ == global_recv1_ &&
          global_sent2_ == global_recv2_ &&
          global_sent1_ == global_sent2_ &&
          global_sent1_ > 0) { // Only terminate after some activity
        terminated();
      } else {
        global_sent2_ = global_sent1_;
        global_recv2_ = global_recv1_;
        global_sent1_ = global_recv1_ = 0;

        if (singleRank()) {
          // do nothing...wait for poll to happen again
        } else {
          // Start new wave
          startFirstWave();
        }
      }
    } else {
      // Send response up
      sendResponseToParent(global_sent1_ + sent_, global_recv1_ + recv_);
      global_sent1_ = global_recv1_ = 0;
      waiting_children_ = num_children_;
    }
  }
}

void TerminationDetector::notifyMessageSend() {
  if (!terminated_) {
    sent_++;
#if DEBUG_TERMINATION
    printf("Rank %d: notified send, counter: sent_=%" PRIu64 ", recv_=%" PRIu64 "\n",
           rank_, sent_, recv_);
#endif
  }
}

void TerminationDetector::notifyMessageReceive() {
  if (!terminated_) {
    recv_++;
#if DEBUG_TERMINATION
    printf("Rank %d: notified receive, counter: sent_=%" PRIu64 ", recv_=%" PRIu64 "\n",
           rank_, sent_, recv_);
#endif
  }
}

void TerminationDetector::terminated() {
#if DEBUG_TERMINATION
  printf("%d: %p Terminated!\n", rank_, this);
#endif
  terminated_ = true;
  for (int i = 0; i < num_children_; i++) {
    handle_[first_child_ + i].sendTerm<&TerminationDetector::terminated>();
  }
}

} // namespace vt_lb::comm::detail