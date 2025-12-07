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

#if !defined INCLUDED_VT_LB_COMM_COMM_MPI_H
#define INCLUDED_VT_LB_COMM_COMM_MPI_H

#include <cstdint>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <list>

#include <mpi.h>

#include <checkpoint/checkpoint.h>

#include <vt-lb/util/logging.h>
#include <vt-lb/comm/MPI/termination.h>
#include <vt-lb/comm/MPI/class_handle.h>
#include <vt-lb/comm/MPI/comm_mpi_detail.h>

/**
 * \namespace vt_lb::comm
 * \brief Communication layer implemented in straight MPI code
 *
 * Provides MPI-based communication for load balancing suite
 */
namespace vt_lb::comm {

/**
 * \brief MPI-based communication manager
 *
 * Manages MPI communication between ranks, handling serialization,
 * asynchronous operations, and message dispatch
 */
struct CommMPI {
  using TagType = int;
  using SizeType = std::size_t;
  using RequestType = MPI_Request;

  template <typename T>
  using HandleType = ClassHandle<T>;

  CommMPI() = default;
  CommMPI(CommMPI const&) = delete;
  CommMPI(CommMPI&&) = delete;
  CommMPI& operator=(CommMPI const&) = delete;
  CommMPI& operator=(CommMPI&&) = delete;

private:
  CommMPI(MPI_Comm comm, int rank, int size)
    : comm_(comm), cached_rank_(rank), cached_size_(size)
  {
    initTermination();
  }

public:
 /**
   * \brief Initialize CommMPI
   *
   * \param argc Pointer to argument count
   * \param argv Pointer to argument array
   * \param comm MPI communicator to use (default: MPI_COMM_NULL)
   */
  void init(int& argc, char**& argv, MPI_Comm comm = MPI_COMM_NULL);

  /**
   * \brief Clone the communicator to create a distinct termination scope
   *
   * \param dup_comm Whether to duplicate the underlying MPI_Comm (default: true)
   *
   * \return A new CommMPI instance with the cloned communicator
   */
  CommMPI clone(bool dup_comm = true) const;

 /**
   * \brief Get the number of ranks in the communicator
   * \return Number of ranks
   */
  int numRanks() const;

  /**
   * \brief Get this process's rank
   * \return Current rank
   */
  int getRank() const;

  /**
   * \brief Finalize MPI
   */
  void finalize();

  /**
   * \brief MPI barrier
   */
  void barrier();

  /**
   * \brief Register an instance for collective operations
   *
   * \param obj Pointer to the object to register
   *
   * \return A handle for the registered instance
   */
  template <typename T>
  ClassHandle<T> registerInstanceCollective(T* obj) {
    auto idx = next_class_index_++;
    class_map_[idx] = reinterpret_cast<void*>(obj);
    return ClassHandle<T>{idx, this};
  }

  /**
   * \brief Unregister a previously registered instance
   *
   * \param idx Index of the instance to unregister
   */
  void unregisterInstanceCollective(std::size_t idx) {
    class_map_.erase(idx);
  }

  /**
   * \brief Get a registered instance by index
   *
   * \param idx Index of the instance
   * \return Pointer to the registered instance
   */
  void* getInstanceCollective(std::size_t idx) {
    return class_map_.at(idx);
  }

  template <typename U, typename V>
  void reduce(int root, MPI_Datatype datatype, MPI_Op op, U sendbuf, V recvbuf, int count) {
    MPI_Request request;
    MPI_Ireduce(sendbuf, recvbuf, count, datatype, op, root, comm_, &request);
    int flag = 0;
    while (not flag) {
      MPI_Status status;
      MPI_Test(&request, &flag, &status);
      poll();
    }
  }

  /**
   * \brief Send data to a destination rank
   * \param dest Destination rank
   * \param idx Instance index
   * \param args Arguments to serialize and send
   * \throws std::runtime_error if destination rank is invalid
   */
  template <auto fn, typename ProxyT, typename... Args>
  void send(int dest, ProxyT proxy, Args&&... args) {
    sendImpl<fn>(dest, proxy.getIndex(), false, std::forward<Args>(args)...);
  }

  /**
   * \brief Helper class for interpreting buffer headers
   */
  class BufferIntInterpreter {
    char* const ptr_;
  public:
    explicit BufferIntInterpreter(char* buf) : ptr_(buf) {
      assert((reinterpret_cast<std::uintptr_t>(buf) % alignof(int)) == 0);
    }
    int& handlerIndex() { return *reinterpret_cast<int*>(ptr_); }
    int& classIndex() { return *(reinterpret_cast<int*>(ptr_) + 1); }
    int& isTermination() { return *(reinterpret_cast<int*>(ptr_) + 2); }
  };

  template <auto fn, typename... Args>
  void sendImpl(int dest, int idx, bool is_termination_msg, Args&&... args) {
    // Layout of buffer:
    //   <handler_index : int>      -> used to get the correct registered handler
    //   <class_index : int>        -> used to dispatch to the correct class instance
    //   <is_termination_msg : int> -> used to determine if it should count as a normal message or termination message
    //   <serialized buffer>        -> the actual serialized buffer from magistrate

    // Validate destination rank
    if (dest < 0 || dest >= numRanks()) {
      VT_LB_LOG(Communicator, terse, "Invalid destination rank {}\n", dest);
      throw std::runtime_error("Invalid destination rank");
    }

    auto const handler_index = detail::registerMember<fn>();
    std::tuple<std::decay_t<Args>...> tup{std::forward<Args>(args)...};
    char* send_ptr = nullptr;
    std::size_t send_ptr_size = 0;
    std::tuple<MPI_Request, std::unique_ptr<char[]>> pending_operation;
    auto ret = checkpoint::serialize(
      tup, [&send_ptr, &send_ptr_size, &pending_operation](std::size_t size) -> char* {
        auto constexpr extra_size = 3*sizeof(int);
        send_ptr_size = size + extra_size;
        pending_operation = std::make_tuple(MPI_Request{}, std::make_unique<char[]>(send_ptr_size));
        send_ptr = std::get<1>(pending_operation).get();
        return send_ptr + extra_size;
      }
    );
    BufferIntInterpreter buf_interpreter(send_ptr);
    buf_interpreter.handlerIndex() = handler_index;
    buf_interpreter.classIndex() = idx;
    buf_interpreter.isTermination() = is_termination_msg ? 1 : 0;

    VT_LB_LOG(
      Communicator, normal,
      "MPI_Isend to {} handler_index={} class_index={} is_termination={}\n",
      dest,
      buf_interpreter.handlerIndex(),
      buf_interpreter.classIndex(),
      is_termination_msg ? 1 : 0
    );

    MPI_Request req;
    MPI_Isend(
      send_ptr, send_ptr_size, MPI_BYTE, dest, 0, comm_,
      &req
    );
    std::get<0>(pending_operation) = req;
    pending_.emplace_back(std::move(pending_operation));
    if (!is_termination_msg) {
      termination_detector_->notifyMessageSend();
    }
  }

  /**
   * \brief Poll for incoming messages and process them
   *
   * Checks for new messages and processes them, also cleans up completed sends
   * \throws std::runtime_error if received message is invalid
   *
   * \return true when termination occurs, false otherwise
   */
  bool poll();

private:
  void initTermination();

  /// @brief Flag indicating if MPI is being used in interop mode
  bool interop_mode_ = false;
  /// @brief MPI communicator
  MPI_Comm comm_ = MPI_COMM_NULL;
  /// @brief Pending operations we are waiting on with buffers
  std::list<std::tuple<MPI_Request, std::unique_ptr<char[]>>> pending_;
  /// @brief Next class index to use for registering class instances
  int next_class_index_ = 0;
  /// @brief Map of class indices to registered instances
  std::unordered_map<int, void*> class_map_;
  /// @brief Termination detector instance
  std::unique_ptr<detail::TerminationDetector> termination_detector_;
  /// @brief Cached rank
  int cached_rank_ = -1;
  /// @brief Cached size
  int cached_size_ = -1;
};

} /* end namespace vt_lb::comm */

#include "vt-lb/comm/MPI/class_handle.impl.h"

#endif /*INCLUDED_VT_LB_COMM_COMM_MPI_H*/