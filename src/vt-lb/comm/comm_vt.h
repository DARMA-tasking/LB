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

#include <cstdint>
#include <memory>
#include <tuple>
#include <unordered_map>

#include <vt/transport.h>

namespace vt_lb::comm {

template <typename U>
void reduceCb(U value) {

}

template <typename ProxyT>
struct ProxyWrapper : ProxyT {
  ProxyWrapper(ProxyT proxy) : ProxyT(proxy) { }

  template <typename U, typename V>
  void reduce(int root, MPI_Datatype datatype, MPI_Op op, U sendbuf, V recvbuf, int count) {
    // @todo finish this??
    // if (op == MPI_MAX) {
    //   auto cb = vt::theCB()->makeSend<reduceCb>(vt::pipe::LifetimeEnum::Once, reduceCb);
    //   this->template reduce<vt::collective::MaxOp>();
    // } else if (op == MPI_MIN) {

    // } else if (op == MPI_SUM) {

    // }

  }

};

struct CommVT {
  CommVT() = default;

  /**
   * \brief Initialize the VT runtime
   * \param argc Pointer to argument count
   * \param argv Pointer to argument array
   */
  void init(int& argc, char**& argv) {
    vt::initialize(argc, argv);
    vt::theTerm()->addDefaultAction([this]{ terminated_ = true; });
  }

  /**
   * \brief Finalize the VT runtime
   */
  void finalize() {
    vt::finalize();
  }

  template <typename T>
  auto registerInstanceCollective(T* obj) {
    return ProxyWrapper{vt::theObjGroup()->makeCollective<T>(
      obj, "CommVT_ObjGroup"
    )};
  }

  template <auto fn, typename ProxyT, typename... Args>
  void send(vt::NodeType dest, ProxyT proxy, Args&&... args) {
    proxy[dest].template send<fn>(std::forward<Args>(args)...);
  }

  /**
   * \brief Get the number of ranks
   * \return Number of ranks
   */
  int numRanks() const {
    return static_cast<int>(vt::theContext()->getNumNodes());
  }

  /**
   * \brief Get this process's rank
   * \return Current rank
   */
  int getRank() const {
    return static_cast<int>(vt::theContext()->getNode());
  }

  /**
   * \brief Poll for incoming messages and process them
   *
   * Triggers VT's scheduler to process pending events
   */
  bool poll() {
    vt::theSched()->runSchedulerOnceImpl();
    return !terminated_;
  }
private:
  bool terminated_ = false;
};

} /* end namespace vt_lb::comm */

#endif /*INCLUDED_VT_LB_COMM_COMM_VT_H*/