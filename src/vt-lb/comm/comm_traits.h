/*
//@HEADER
// *****************************************************************************
//
//                               comm_traits.h
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

#if !defined INCLUDED_VT_LB_COMM_COMM_TRAITS_H
#define INCLUDED_VT_LB_COMM_COMM_TRAITS_H

#include <vt-lb/config/cmake_config.h>

#include <cstddef>
#include <type_traits>
#include <utility>
#include <concepts>

namespace vt_lb::comm {

// New concept helpers capturing common communicator API across CommMPI and CommVT
namespace detail {

  // Dummy class and member-function pointer used to instantiate templated send<fn>(...)
  struct DummyClass {
    void dummyMethod();
  };
  inline constexpr auto dummyMemFn = &DummyClass::dummyMethod;

  // Concepts for required member functions

  template <typename Comm>
  concept HasInit = requires(Comm c, int& argc, char**& argv) {
    { c.init(argc, argv) };
  };

  template <typename Comm>
  concept HasFinalize = requires(Comm c) {
    { c.finalize() };
  };

  template <typename Comm>
  concept HasNumRanks = requires(Comm const& c) {
    { c.numRanks() } -> std::integral;
  };

  template <typename Comm>
  concept HasGetRank = requires(Comm const& c) {
    { c.getRank() } -> std::integral;
  };

  template <typename Comm>
  concept HasPoll = requires(Comm c) {
    { c.poll() } -> std::convertible_to<bool>;
  };

  // registerInstanceCollective<T>(T*)
  template <typename Comm, typename T>
  concept HasRegisterInstanceCollective = requires(Comm c, T* t) {
    { c.template registerInstanceCollective<T>(t) };
  };

  // Resolve instance handle type returned by registerInstanceCollective<T>(T*)
  template <typename Comm, typename T>
  using instance_handle_t =
    decltype(std::declval<Comm&>().template registerInstanceCollective<T>(std::declval<T*>()));

  // Unified send pattern: send<fn>(int dest, Handle proxy, ...)
  template <typename Comm>
  concept HasSendUnified =
    HasRegisterInstanceCollective<Comm, DummyClass> &&
    requires(Comm c) {
      { c.template send<dummyMemFn>(
          std::declval<int>(),
          std::declval<instance_handle_t<Comm, DummyClass>>()
        )
      };
    };

} // namespace detail

// Primary concept that a communicator must satisfy
template <typename Comm>
concept Communicator =
  detail::HasInit<Comm> &&
  detail::HasFinalize<Comm> &&
  detail::HasNumRanks<Comm> &&
  detail::HasGetRank<Comm> &&
  detail::HasPoll<Comm> &&
  detail::HasRegisterInstanceCollective<Comm, detail::DummyClass> &&
  detail::HasSendUnified<Comm>;

// Backward-compatible traits shim implemented in terms of concepts
template <typename Comm>
struct CommunicatorTraits {
  static constexpr bool has_init = detail::HasInit<Comm>;
  static constexpr bool has_finalize = detail::HasFinalize<Comm>;
  static constexpr bool has_num_ranks = detail::HasNumRanks<Comm>;
  static constexpr bool has_get_rank = detail::HasGetRank<Comm>;
  static constexpr bool has_poll = detail::HasPoll<Comm>;

  template <typename T>
  using instance_handle_t = detail::instance_handle_t<Comm, T>;

  template <typename T>
  static constexpr bool has_register_instance =
    detail::HasRegisterInstanceCollective<Comm, T>;

  static constexpr bool has_send_unified =
    detail::HasSendUnified<Comm>;

  static constexpr bool is_valid = Communicator<Comm>;
};

// Convenience alias to check conformance at compile time
template <typename Comm>
using is_comm_conformant = std::bool_constant<Communicator<Comm>>;

} /* end namespace vt_lb::comm */

#endif /*INCLUDED_VT_LB_COMM_COMM_TRAITS_H*/