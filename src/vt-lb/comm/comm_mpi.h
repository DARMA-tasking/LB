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

#include "termination.h"
#include "class_handle.h"

/**
 * \namespace vt_lb::comm
 * \brief Communication layer implemented in straight MPI code
 *
 * Provides MPI-based communication for load balancing suite
 */
namespace vt_lb::comm {

namespace detail {

template <typename Tag, typename T>
std::vector<T>& getRegistry() {
  static std::vector<T> registry;
  return registry;
}

struct BaseRegisteredInfo {
  virtual ~BaseRegisteredInfo() = default;
  virtual void dispatch(void* data, void* object, bool deserialize) const = 0;
};

template <typename ObjT>
struct RegisteredInfo final : BaseRegisteredInfo {
  using FnType = decltype(ObjT::getFunc());
  FnType fn_ptr;
  
  explicit RegisteredInfo(FnType in_fn_ptr) : fn_ptr(in_fn_ptr) {}
  
  void dispatch(void* data, void* object, bool deserialize) const override {
    typename ObjT::TupleType tup;
    if (deserialize) {
      checkpoint::deserializeInPlace<typename ObjT::TupleType>(
        static_cast<char*>(data), &tup
      );
    } else {
      tup = *reinterpret_cast<typename ObjT::TupleType*>(data);
    }
    
    auto obj = reinterpret_cast<typename ObjT::ObjectType*>(object);
    std::apply(
      fn_ptr,
      std::tuple_cat(std::make_tuple(obj), tup)
    );
  }
};

// Combine Registrar and Type into a single structure
template <typename Tag, typename T, typename ObjT>
struct TypeRegistry {
  static std::size_t const idx;
  static std::size_t register_type() {
    auto& reg = getRegistry<Tag, T>();
    auto index = reg.size();
    reg.emplace_back(ObjT::makeRegisteredInfo());
    return index;
  }
};

template <typename Tag, typename T, typename ObjT>
std::size_t const TypeRegistry<Tag, T, ObjT>::idx = TypeRegistry<Tag, T, ObjT>::register_type();

struct MemberTag {};

// Simplify ObjFuncTraits
template <typename T>
struct ObjFuncTraits;

template <typename Return, typename Obj, typename... Args>
struct ObjFuncTraits<Return(Obj::*)(Args...)> {
  using ObjT = Obj;
  using TupleType = std::tuple<std::decay_t<Args>...>;
};

template <typename F, F f>
struct FunctionWrapper {
  using ThisType = FunctionWrapper<F,f>;
  using FuncType = F;
  using TupleType = typename ObjFuncTraits<F>::TupleType;
  using ObjectType = typename ObjFuncTraits<F>::ObjT;
  static constexpr F getFunc() { return f; }
  static constexpr auto makeRegisteredInfo() {
    return std::make_unique<RegisteredInfo<ThisType>>(ThisType::getFunc());
  }
};

inline auto getMember(std::size_t idx) {
  return getRegistry<MemberTag, std::unique_ptr<BaseRegisteredInfo>>().at(idx).get();
}

template <auto f>
inline std::size_t registerMember() {
  return TypeRegistry<MemberTag, std::unique_ptr<BaseRegisteredInfo>, FunctionWrapper<decltype(f), f>>::idx;
}

struct TerminationDetector;

} // namespace detail

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

  /**
   * \brief Construct a new CommMPI instance
   * \param comm The MPI communicator to use (defaults to MPI_COMM_WORLD)
   */
  CommMPI(MPI_Comm comm = MPI_COMM_WORLD) : comm_(comm) {}

 /**
   * \brief Initialize MPI
   * \param argc Pointer to argument count
   * \param argv Pointer to argument array
   */
  void init(int& argc, char**& argv) {
    MPI_Init(&argc, &argv);
    initTermination();
  }

  /**
   * \brief Finalize MPI
   */
  void finalize() {
    MPI_Finalize();
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

  /**
   * \brief Register an instance for collective operations
   * \param obj Pointer to the object to register
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
   * \param idx Index of the instance to unregister
   */
  void unregisterInstanceCollective(std::size_t idx) {
    class_map_.erase(idx);
  }

  void* getInstanceCollective(std::size_t idx) {
    return class_map_.at(idx);
  }

  /**
   * \brief Send data to a destination rank
   * \param dest Destination rank
   * \param idx Instance index
   * \param args Arguments to serialize and send
   * \throws std::runtime_error if destination rank is invalid
   */
  template <auto fn, typename... Args>
  void send(int dest, int idx, Args&&... args) {
    sendImpl<fn>(dest, idx, false, std::forward<Args>(args)...);
  }

  template <auto fn, typename... Args>
  void sendImpl(int dest, int idx, bool is_termination_msg, Args&&... args) {
    // Layout of buffer:
    //   <handler_index : int>      -> used to get the correct registered handler
    //   <class_index : int>        -> used to dispatch to the correct class instance
    //   <is_termination_msg : int> -> used to determine if it should count as a normal message or termination message
    //   <serialized buffer>        -> the actual serialized buffer from magistrate

    // Validate destination rank
    if (dest < 0 || dest >= numRanks()) {
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
   */
  bool poll() {
    // Look for new incoming messages
    {
      int flag = 0;
      MPI_Status status;
      MPI_Iprobe(MPI_ANY_SOURCE, 0, comm_, &flag, &status);
      if (flag) {
        int count = 0;
        MPI_Get_count(&status, MPI_BYTE, &count);
        
        // Validate message size
        if (count < static_cast<int>(3 * sizeof(int))) {
          throw std::runtime_error("Received message is too small");
        }
        
        std::vector<char> buf(count);
        // Ensure buffer is properly aligned
        if (reinterpret_cast<std::uintptr_t>(buf.data()) % alignof(int) != 0) {
          throw std::runtime_error("Buffer alignment error");
        }

        MPI_Recv(buf.data(), count, MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG, comm_, MPI_STATUS_IGNORE);
        BufferIntInterpreter buf_interpreter(buf.data());
        int handler_index = buf_interpreter.handlerIndex();
        int class_index = buf_interpreter.classIndex();
        bool is_termination = buf_interpreter.isTermination() != 0;

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
    }

    // Process pending sends
    for (auto it = pending_.begin(); it != pending_.end();) {
      int flag = 0;
      MPI_Status status;
      MPI_Test(&std::get<0>(*it), &flag, &status);
      if (flag) {
        it = pending_.erase(it);
      } else {
        ++it;
      }
    }

    return !termination_detector_->isTerminated() || pending_.size() > 0;
  }

  /**
   * \brief Get the number of ranks in the communicator
   * \return Number of ranks
   */
  int numRanks() const {
    int size = 0;
    MPI_Comm_size(comm_, &size);
    return size;
  }

  /**
   * \brief Get this process's rank
   * \return Current rank
   */
  int getRank() const {
    int rank = 0;
    MPI_Comm_rank(comm_, &rank);
    return rank;
  }

private:
  void initTermination();

  MPI_Comm comm_;
  std::list<std::tuple<MPI_Request, std::unique_ptr<char[]>>> pending_;
  int next_class_index_ = 0;
  std::unordered_map<int, void*> class_map_;
  std::unique_ptr<detail::TerminationDetector> termination_detector_;
};

inline void CommMPI::initTermination() {
  termination_detector_ = std::make_unique<detail::TerminationDetector>();
  termination_detector_->init(*this, registerInstanceCollective(termination_detector_.get()));
}

} /* end namespace vt_lb::comm */

#include "class_handle.impl.h"

#endif /*INCLUDED_VT_LB_COMM_COMM_MPI_H*/