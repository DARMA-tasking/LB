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
// * Redistributions in binary form must reproduce the above copyright notice,
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

#include <cstddef>
#include <type_traits>
#include <utility>

namespace vt_lb::comm {

// Trait helpers for SFINAE
namespace detail {
  // Checks for send(MsgType*, int, HandlerType)
  template <typename T, typename Msg, typename Handler>
  using send_t = decltype(std::declval<T>().send(
    std::declval<Msg*>(), std::declval<int>(), std::declval<Handler>()));

  // Checks for registerHandler(HandlerType, ClassType*)
  template <typename T, typename Handler, typename Class>
  using register_handler_t = decltype(std::declval<T>().registerHandler(
    std::declval<Handler>(), std::declval<Class*>()));

  // Checks for numRanks()
  template <typename T>
  using numRanks_t = decltype(std::declval<T>().numRanks());

  // Checks for getRank()
  template <typename T>
  using getRank_t = decltype(std::declval<T>().getRank());

  // Checks for progress()
  template <typename T>
  using progress_t = decltype(std::declval<T>().progress());
}

// Main trait: checks for required communicator interface
// Usage: is_comm_conformant<Comm, MsgType, HandlerType, ClassType>::value
// HandlerType is typically a pointer to member function or function pointer
// ClassType is the class on which the handler is registered
// MsgType is the message type

template <typename Comm, typename Msg, typename Handler, typename Class, typename = void>
struct is_comm_conformant : std::false_type {};

template <typename Comm, typename Msg, typename Handler, typename Class>
struct is_comm_conformant<Comm, Msg, Handler, Class, std::void_t<
  detail::send_t<Comm, Msg, Handler>,
  detail::register_handler_t<Comm, Handler, Class>,
  detail::numRanks_t<Comm>,
  detail::getRank_t<Comm>,
  detail::progress_t<Comm>
>> : std::true_type {};

// Example usage:
// static_assert(is_comm_conformant<MyComm, MyMsg, MyHandler, MyClass>::value, "Comm does not conform");

} /* end namespace vt_lb::comm */

#endif /*INCLUDED_VT_LB_COMM_COMM_TRAITS_H*/