/*
//@HEADER
// *****************************************************************************
//
//                              proxy_wrapper.h
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

#ifndef INCLUDED_VT_LB_COMM_PROXY_WRAPPER_H
#define INCLUDED_VT_LB_COMM_PROXY_WRAPPER_H

#include <atomic>
#include <vt/transport.h>
#include <mpi.h>

namespace vt_lb::comm {

template <typename ProxyT>
struct ProxyWrapper : ProxyT {
  struct CollectiveCtx {
    void* out_ptr = nullptr;
    std::atomic<bool> done{false};
    std::size_t count = 0;
  };

  ProxyWrapper() = default;
  ProxyWrapper(ProxyT proxy);

  template <typename T>
  static void reduceAnonCb(vt::collective::ReduceTMsg<T>* msg, CollectiveCtx* ctx);

  template <typename U, typename V>
  void reduce(int root, MPI_Datatype datatype, MPI_Op op, U sendbuf, V recvbuf, int count);

  template <typename T>
  static void broadcastAnonCb(T val, CollectiveCtx* ctx);

  template <typename U>
  void broadcast(int root, MPI_Datatype datatype, U buffer, int count);

private:
  enum class VTOp { Plus, Max, Min };
  static VTOp mapOp(MPI_Op mpio);

  template <typename T, typename SendBufT, typename RecvBufT>
  void reduce_impl(int root, MPI_Op op, SendBufT sendbuf, RecvBufT recvbuf, int count);

  template <typename T, typename BufT>
  void broadcast_impl(int root, BufT buffer, int count);
};

} // namespace vt_lb::comm

#include "vt-lb/comm/vt/proxy_wrapper.impl.h"

#endif /* INCLUDED_VT_LB_COMM_PROXY_WRAPPER_H */
