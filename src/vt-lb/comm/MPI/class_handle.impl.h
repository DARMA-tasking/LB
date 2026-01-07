/*
//@HEADER
// *****************************************************************************
//
//                              class_handle.impl.h
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

#if !defined INCLUDED_VT_LB_COMM_CLASS_HANDLE_IMPL_H
#define INCLUDED_VT_LB_COMM_CLASS_HANDLE_IMPL_H

#include "class_handle.h"
#include "vt-lb/comm/MPI/comm_mpi.h"

namespace vt_lb::comm {

template <typename T>
ClassHandleRank<T>::ClassHandleRank(ClassHandle<T> in_handle, int in_rank)
  : handle_(in_handle),
    rank_(in_rank)
{}

template <typename T>
template <auto fn, typename... Args>
void ClassHandleRank<T>::send(Args&&... args) {
  handle_.template send<fn>(rank_, std::forward<Args>(args)...);
}

template <typename T>
template <auto fn, typename... Args>
void ClassHandleRank<T>::sendTerm(Args&&... args) {
  handle_.template sendTerm<fn>(rank_, std::forward<Args>(args)...);
}

template <typename T>
ClassHandle<T>::ClassHandle(int in_index, CommMPI* in_comm)
  : index_(in_index),
    comm_(in_comm)
{}

template <typename T>
void ClassHandle<T>::unregister() {
  comm_->unregisterInstanceCollective(index_);
}

template <typename T>
T* ClassHandle<T>::get() {
  return reinterpret_cast<T*>(comm_->getInstanceCollective(index_));
}

template <typename T>
ClassHandleRank<T> ClassHandle<T>::operator[](int rank) {
  return ClassHandleRank<T>{*this, rank};
}

template <typename T>
template <auto fn, typename... Args>
void ClassHandle<T>::send(int dest, Args&&... args) {
  comm_->template send<fn>(dest, *this, std::forward<Args>(args)...);
}

template <typename T>
template <auto fn, typename... Args>
void ClassHandle<T>::sendTerm(int dest, Args&&... args) {
  comm_->template sendImpl<fn>(dest, index_, true, std::forward<Args>(args)...);
}

template <typename T>
template <typename U, typename V>
void ClassHandle<T>::reduce(int root, MPI_Datatype datatype, MPI_Op op, U sendbuf, V recvbuf, int count) {
  comm_->reduce(root, datatype, op, sendbuf, recvbuf, count);
}

template <typename T>
template <typename U>
void ClassHandle<T>::broadcast(int root, MPI_Datatype datatype, U buffer, int count) {
  comm_->broadcast(root, datatype, buffer, count);
}

} // namespace vt_lb::comm

#endif /*INCLUDED_VT_LB_COMM_CLASS_HANDLE_IMPL_H*/