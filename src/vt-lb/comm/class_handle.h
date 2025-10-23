/*
//@HEADER
// *****************************************************************************
//
//                                class_handle.h
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

#if !defined INCLUDED_VT_LB_COMM_CLASS_HANDLE_H
#define INCLUDED_VT_LB_COMM_CLASS_HANDLE_H

namespace vt_lb::comm {

struct CommMPI;

template <typename T>
struct ClassHandle;

template <typename T>
struct ClassHandleRank {
  ClassHandleRank(ClassHandle<T> in_handle, int in_rank);
  
  template <auto fn, typename... Args>
  void send(Args&&... args);
  
  template <auto fn, typename... Args>
  void sendTerm(Args&&... args);

private:
  ClassHandle<T> handle_;
  int rank_ = -1;
};

template <typename T>
struct ClassHandle {
  ClassHandle() = default;
  ClassHandle(int in_index, CommMPI* in_comm);
  ClassHandle(ClassHandle const&) = default;
  ClassHandle& operator=(ClassHandle const&) = default;

  void unregister();
  T* get();
  ClassHandleRank<T> operator[](int rank);

  template <auto fn, typename... Args>
  void send(int dest, Args&&... args);

  template <auto fn, typename... Args>
  void sendTerm(int dest, Args&&... args);

  friend struct ClassHandleRank<T>;

private:
  int index_ = 0;
  CommMPI* comm_;
};

} // namespace vt_lb::comm

#endif /*INCLUDED_VT_LB_COMM_CLASS_HANDLE_H*/