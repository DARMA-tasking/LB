/*
//@HEADER
// *****************************************************************************
//
//                                comm_vt.h
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

#if !defined INCLUDED_VT_LB_COMM_COMM_VT_H
#define INCLUDED_VT_LB_COMM_COMM_VT_H

#include <vt/configs/types/types_type.h>
#include <vt/objgroup/proxy/proxy_objgroup.h>

namespace vt_lb::comm {

template <typename ProxyT>
struct ProxyWrapper;

struct CommVT {
  template <typename T>
  using HandleType = ProxyWrapper<vt::objgroup::proxy::Proxy<T>>;

  CommVT() = default;
  CommVT(CommVT const&) = delete;
  CommVT(CommVT&&) = delete;
  ~CommVT();

private:
  CommVT(vt::EpochType epoch);

public:
  void init(int& argc, char**& argv);
  void finalize();
  int numRanks() const;
  int getRank() const;
  bool poll() const;
  CommVT clone();

  template <typename T>
  ProxyWrapper<vt::objgroup::proxy::Proxy<T>> registerInstanceCollective(T* obj);

  template <auto fn, typename ProxyT, typename... Args>
  void send(vt::NodeType dest, ProxyT proxy, Args&&... args);

private:
  bool terminated_ = false;
  vt::EpochType epoch_ = vt::no_epoch;
};

} /* end namespace vt_lb::comm */

#include "vt-lb/comm/VT/comm_vt.impl.h"

#endif /*INCLUDED_VT_LB_COMM_COMM_VT_H*/