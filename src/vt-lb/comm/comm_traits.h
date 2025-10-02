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
#include <utility>

namespace vt_lb::comm {

// Traits to check for required comm interface
namespace detail {
  template <typename T>
  using send_t = decltype(std::declval<T>().send(
    std::declval<typename T::MsgType>(),
    std::declval<int>(),
    std::declval<typename T::TagType>()
  ));

  template <typename T>
  using recv_t = decltype(std::declval<T>().recv(
    std::declval<typename T::MsgType>(),
    std::declval<int>(),
    std::declval<typename T::TagType>()
  ));

  template <typename T>
  using numRanks_t = decltype(std::declval<T>().numRanks());

  template <typename T>
  using getRank_t = decltype(std::declval<T>().getRank());
} /* end namespace detail */

template <typename T, typename = void>
struct is_comm_conformant : std::false_type {};

template <typename T>
struct is_comm_conformant<T, std::void_t<
  detail::send_t<T>,
  detail::recv_t<T>,
  detail::numRanks_t<T>,
  detail::getRank_t<T>
>> : std::true_type {};

// Usage:
// static_assert(is_comm_conformant<MyCommClass>::value, "Comm class does not conform to required interface");

} /* end namespace vt_lb::comm */

#endif /*INCLUDED_VT_LB_COMM_COMM_TRAITS_H*/