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

#if !defined INCLUDED_VT_LB_COMM_COMM_MPI_DETAIL_H
#define INCLUDED_VT_LB_COMM_COMM_MPI_DETAIL_H

#include <vector>

namespace vt_lb::comm::detail {

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

} /* namespace vt_lb::comm::detail */

#endif //INCLUDED_VT_LB_COMM_COMM_MPI_DETAIL_H