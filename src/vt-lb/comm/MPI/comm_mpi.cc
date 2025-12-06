/*
//@HEADER
// *****************************************************************************
//
//                                comm_mpi.h
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

#include <vt-lb/comm/MPI/comm_mpi.h>

#define VT_LB_LOG(mode, ...) ::vt_lb::util::log(::vt_lb::util::Component::Communicator, ::vt_lb::util::Verbosity::mode, __VA_ARGS__)

namespace vt_lb::comm {

void CommMPI::init(int& argc, char**& argv, MPI_Comm comm) {
  if (comm == MPI_COMM_NULL) {
    MPI_Init(&argc, &argv);
    comm_ = MPI_COMM_WORLD;
  } else {
    interop_mode_ = true;
    comm_ = comm;
  }
  MPI_Comm_rank(comm_, &cached_rank_);
  MPI_Comm_size(comm_, &cached_size_);
  // Provide rank to logging
  vt_lb::util::setRankProvider([]() -> int {
    int r = -1;
    int initialized = 0;
    MPI_Initialized(&initialized);
    if (initialized) {
      int rank = 0;
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      r = rank;
    }
    return r;
  });
  initTermination();
  VT_LB_LOG(terse, "Initialized MPI with {} ranks\n", cached_size_);
}

CommMPI CommMPI::clone(bool dup_comm) const {
  MPI_Comm new_comm;
  if (dup_comm) {
    MPI_Comm_dup(comm_, &new_comm);
  } else {
    new_comm = comm_;
  }
  return CommMPI{new_comm, cached_rank_, cached_size_};
}

void CommMPI::finalize() {
  if (!interop_mode_) {
    VT_LB_LOG(terse, "Finalizing MPI\n");
    MPI_Finalize();
  }
  // Clear rank provider when MPI is no longer available
  vt_lb::util::clearRankProvider();
  comm_ = MPI_COMM_NULL;
}

void CommMPI::barrier() {
  VT_LB_LOG(verbose, "MPI_Barrier\n");
  MPI_Barrier(comm_);
}

int CommMPI::getRank() const {
  int rank = 0;
  if (comm_ == MPI_COMM_NULL) {
    throw std::runtime_error("Communicator is not initialized");
  }
  MPI_Comm_rank(comm_, &rank);
  return rank;
}

int CommMPI::numRanks() const {
  int size = 0;
  if (comm_ == MPI_COMM_NULL) {
    throw std::runtime_error("Communicator is not initialized");
  }
  MPI_Comm_size(comm_, &size);
  return size;
}

void CommMPI::initTermination() {
  termination_detector_ = std::make_unique<detail::TerminationDetector>();
  termination_detector_->init(*this, registerInstanceCollective(termination_detector_.get()));
  if (cached_rank_ == 0) {
    termination_detector_->notifyMessageSend();
    termination_detector_->notifyMessageReceive();
  }
  VT_LB_LOG(verbose, "Termination initialized\n");
}

bool CommMPI::poll() {
  // Look for new incoming messages
  {
    int flag = 0;
    MPI_Status status;
    VT_LB_LOG(verbose, "MPI_Iprobe\n");
    MPI_Iprobe(MPI_ANY_SOURCE, 0, comm_, &flag, &status);
    if (flag) {
      int count = 0;
      MPI_Get_count(&status, MPI_BYTE, &count);

      // Validate message size
      if (count < static_cast<int>(3 * sizeof(int))) {
        VT_LB_LOG(terse, "Received too small message ({})\n", count);
        throw std::runtime_error("Received message is too small");
      }

      std::vector<char> buf(count);
      // Ensure buffer is properly aligned
      if (reinterpret_cast<std::uintptr_t>(buf.data()) % alignof(int) != 0) {
        VT_LB_LOG(terse, "Buffer alignment error\n");
        throw std::runtime_error("Buffer alignment error");
      }

      VT_LB_LOG(normal, "MPI_Recv from {} of size {}\n", status.MPI_SOURCE, count);
      MPI_Recv(buf.data(), count, MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG, comm_, MPI_STATUS_IGNORE);
      BufferIntInterpreter buf_interpreter(buf.data());
      int handler_index = buf_interpreter.handlerIndex();
      int class_index = buf_interpreter.classIndex();
      bool is_termination = buf_interpreter.isTermination() != 0;

      VT_LB_LOG(
        verbose,
        "Received message: handler_index={} class_index={} is_termination={}\n",
        handler_index, class_index, is_termination ? 1 : 0
      );

      auto mem_fn = detail::getMember(handler_index);
      assert(class_map_.find(class_index) != class_map_.end() && "Class index not found");
      mem_fn->dispatch(
        buf.data() + 3*sizeof(int),
        class_map_[class_index],
        true
      );

      if (!is_termination) {
        termination_detector_->notifyMessageReceive();
      }
    }

    if (termination_detector_->singleRank()) {
      VT_LB_LOG(verbose, "TD single-rank progression\n");
      termination_detector_->startFirstWave();
    }
  }

  // Process pending sends
  for (auto it = pending_.begin(); it != pending_.end();) {
    int flag = 0;
    MPI_Status status;
    MPI_Test(&std::get<0>(*it), &flag, &status);
    if (flag) {
      VT_LB_LOG(verbose, "Completed pending send\n");
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }

  return !termination_detector_->isTerminated() || pending_.size() > 0;
}

} /* end namespace vt_lb::comm */