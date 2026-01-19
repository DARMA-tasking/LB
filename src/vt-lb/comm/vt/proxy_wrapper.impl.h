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

#if !defined INCLUDED_VT_LB_COMM_PROXY_WRAPPER_IMPL_H
#define INCLUDED_VT_LB_COMM_PROXY_WRAPPER_IMPL_H

#include <vt/transport.h>

#include <cstring>
#include <vector>
#include <type_traits>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace vt_lb::comm {

template <typename ProxyT>
ProxyWrapper<ProxyT>::ProxyWrapper(ProxyT proxy) : ProxyT(proxy) {
  collective_ctx_ = std::make_shared<CollectiveCtx>();
  collective_proxy_ = vt::theObjGroup()->template makeCollective<CollectiveHandlerType>(
    "CommVT_CollectiveHandler", collective_ctx_.get()
  );
}

template <typename ProxyT>
template <typename T>
void ProxyWrapper<ProxyT>::reduceAnonCb(vt::collective::ReduceTMsg<T>* msg, CollectiveCtx* ctx) {
  auto const& val = msg->getVal();
  //printf("%d: callback invoked\n", vt::theContext()->getNode());
  if constexpr (
    std::is_same_v<std::decay_t<T>, int> || std::is_same_v<std::decay_t<T>, double> ||
    std::is_same_v<std::decay_t<T>, float> || std::is_same_v<std::decay_t<T>, long> ||
    std::is_same_v<std::decay_t<T>, long long>
  ) {
    *static_cast<std::decay_t<T>*>(ctx->out_ptr) = val;
  } else {
    using ValT = typename T::value_type;
    static_assert(std::is_trivially_copyable_v<ValT> || std::is_arithmetic_v<ValT>, "Reduce value must be trivially copyable");
    std::memcpy(ctx->out_ptr, val.data(), sizeof(ValT) * std::max<std::size_t>(1, ctx->count));
  }
  ctx->done.store(true, std::memory_order_release);
}

template <typename ProxyT>
typename ProxyWrapper<ProxyT>::VTOp ProxyWrapper<ProxyT>::mapOp(MPI_Op mpio) {
  if (mpio == MPI_SUM) return VTOp::Plus;
  if (mpio == MPI_MAX) return VTOp::Max;
  if (mpio == MPI_MIN) return VTOp::Min;
  return VTOp::Plus;
}

template <typename ProxyT>
template <typename U, typename V>
void ProxyWrapper<ProxyT>::reduce(int root, MPI_Datatype datatype, MPI_Op op, U sendbuf, V recvbuf, int count) {
  if (datatype == MPI_INT) {
    if constexpr (std::is_same_v<U, int*>) reduce_impl<int>(root, op, sendbuf, recvbuf, count);
  } else if (datatype == MPI_DOUBLE) {
    if constexpr (std::is_same_v<U, double*>) reduce_impl<double>(root, op, sendbuf, recvbuf, count);
  } else if (datatype == MPI_FLOAT) {
    if constexpr (std::is_same_v<U, float*>) reduce_impl<float>(root, op, sendbuf, recvbuf, count);
  } else if (datatype == MPI_LONG) {
    if constexpr (std::is_same_v<U, long*>) reduce_impl<long>(root, op, sendbuf, recvbuf, count);
  } else if (datatype == MPI_LONG_LONG) {
    if constexpr (std::is_same_v<U, long long*>) reduce_impl<long long>(root, op, sendbuf, recvbuf, count);
  } else {
    vtAbort("ProxyWrapper::reduce: unsupported MPI_Datatype");
  }
}

template <typename ProxyT>
template <typename T, typename SendBufT, typename RecvBufT>
void ProxyWrapper<ProxyT>::reduce_impl(int root, MPI_Op op, SendBufT sendbuf, RecvBufT recvbuf, int count) {
  VTOp vk = mapOp(op);
  auto ctx = std::make_unique<CollectiveCtx>();
  ctx->out_ptr = static_cast<void*>(recvbuf);
  ctx->count = static_cast<std::size_t>(std::max(1, count));
  ctx->done.store(false);
  //printf("%d: initiating reduce\n", vt::theContext()->getNode());

  if (count == 1) {
    T value = *static_cast<T const*>(sendbuf);
    using MsgT = vt::collective::ReduceTMsg<T>;
    auto cb = vt::theCB()->makeCallbackSingleAnon<MsgT, CollectiveCtx>(
      vt::pipe::LifetimeEnum::Once, ctx.get(), &ProxyWrapper::reduceAnonCb<T>
    );
    auto msg = vt::makeMessage<MsgT>(value);
    if (vt::theContext()->getNode() == root) {
      msg->setCallback(cb);
    }
    ProxyT proxy = ProxyT(*this);
    if (vk == VTOp::Plus) {
      vt::theObjGroup()->template reduce<
        typename ProxyT::ObjGroupType,
        MsgT,
        &MsgT::template msgHandler<
          MsgT, vt::collective::PlusOp<T>, vt::collective::reduce::operators::ReduceCallback<MsgT>
        >
      >(proxy, msg, vt::collective::reduce::ReduceStamp{});
    } else if (vk == VTOp::Max) {
      vt::theObjGroup()->template reduce<
        typename ProxyT::ObjGroupType,
        MsgT,
        &MsgT::template msgHandler<
          MsgT, vt::collective::MaxOp<T>, vt::collective::reduce::operators::ReduceCallback<MsgT>
        >
      >(proxy, msg, vt::collective::reduce::ReduceStamp{});
    } else if (vk == VTOp::Min) {
      vt::theObjGroup()->template reduce<
        typename ProxyT::ObjGroupType,
        MsgT,
        &MsgT::template msgHandler<
          MsgT, vt::collective::MinOp<T>, vt::collective::reduce::operators::ReduceCallback<MsgT>
        >
      >(proxy, msg, vt::collective::reduce::ReduceStamp{});
    } else {
      throw std::runtime_error("Unsupported VTOp in reduce_impl");
    }
  } else {
    std::vector<T> v(static_cast<std::size_t>(count));
    std::memcpy(v.data(), static_cast<void const*>(sendbuf), sizeof(T) * static_cast<std::size_t>(count));
    using MsgT = vt::collective::ReduceTMsg<std::vector<T>>;
    auto cb = vt::theCB()->makeCallbackSingleAnon<MsgT, CollectiveCtx>(
      vt::pipe::LifetimeEnum::Once, ctx.get(), &ProxyWrapper::reduceAnonCb<std::vector<T>>
    );
    auto msg = vt::makeMessage<MsgT>(std::move(v));
    if (vt::theContext()->getNode() == root) {
      msg->setCallback(cb);
    }
    ProxyT proxy = ProxyT(*this);
    if (vk == VTOp::Plus) {
      vt::theObjGroup()->template reduce<
        typename ProxyT::ObjGroupType,
        MsgT,
        &MsgT::template msgHandler<
          MsgT, vt::collective::PlusOp<std::vector<T>>, vt::collective::reduce::operators::ReduceCallback<MsgT>
        >
      >(proxy, msg, vt::collective::reduce::ReduceStamp{});
    } else if (vk == VTOp::Max) {
      vt::theObjGroup()->template reduce<
        typename ProxyT::ObjGroupType,
        MsgT,
        &MsgT::template msgHandler<
          MsgT, vt::collective::MaxOp<std::vector<T>>, vt::collective::reduce::operators::ReduceCallback<MsgT>
        >
      >(proxy, msg, vt::collective::reduce::ReduceStamp{});
    } else if (vk == VTOp::Min) {
      vt::theObjGroup()->template reduce<
        typename ProxyT::ObjGroupType,
        MsgT,
        &MsgT::template msgHandler<
          MsgT, vt::collective::MinOp<std::vector<T>>, vt::collective::reduce::operators::ReduceCallback<MsgT>
        >
      >(proxy, msg, vt::collective::reduce::ReduceStamp{});
    } else {
      throw std::runtime_error("Unsupported VTOp in reduce_impl");
    }
  }

  while (vt::theContext()->getNode() == root && !ctx->done.load(std::memory_order_acquire)) {
    vt::theSched()->runSchedulerOnceImpl();
  }
}

template <typename ProxyT>
template <typename T>
void ProxyWrapper<ProxyT>::broadcast(int root, MPI_Datatype datatype, T* buffer, int count) {
  if (datatype == MPI_INT) {
    if constexpr (std::is_same_v<T, int>) broadcast_impl<int>(root, buffer, count);
  } else if (datatype == MPI_DOUBLE) {
    if constexpr (std::is_same_v<T, double>) broadcast_impl<double>(root, buffer, count);
  } else if (datatype == MPI_FLOAT) {
    if constexpr (std::is_same_v<T, float>) broadcast_impl<float>(root, buffer, count);
  } else if (datatype == MPI_LONG) {
    if constexpr (std::is_same_v<T, long>) broadcast_impl<long>(root, buffer, count);
  } else if (datatype == MPI_LONG_LONG) {
    if constexpr (std::is_same_v<T, long long>) broadcast_impl<long long>(root, buffer, count);
  } else {
    vtAbort("ProxyWrapper::broadcast: unsupported MPI_Datatype");
  }
}

template <typename ProxyT>
template <typename T>
void ProxyWrapper<ProxyT>::broadcast_impl(int root, T* buffer, int count) {
  collective_ctx_->out_ptr = static_cast<void*>(buffer);
  collective_ctx_->count = static_cast<std::size_t>(std::max(1, count));
  collective_ctx_->done.store(false);

  if (vt::theContext()->getNode() == root) {
    if (count == 1) {
      T value = *static_cast<T const*>(buffer);
      collective_proxy_.template broadcast<&CollectiveHandlerType::template broadcastScalar<T>>(value);
    } else {
      std::vector<T> values(buffer, buffer + count);
      collective_proxy_.template broadcast<&CollectiveHandlerType::template broadcastVector<T>>(values);
    }
  }

  while (!collective_ctx_->done.load(std::memory_order_acquire)) {
    vt::theSched()->runSchedulerOnceImpl();
  }
}

template <typename T>
struct AllReduceContainer {
  AllReduceContainer() = default;
  explicit AllReduceContainer(std::vector<T> const& in) {
    by_rank[vt::theContext()->getNode()] = in;
  }

  friend AllReduceContainer<T> operator+(
    AllReduceContainer<T> lhs,
    AllReduceContainer<T> const& rhs
  ) {
    for (auto const& kv : rhs.by_rank) {
      auto& vec = lhs.by_rank[kv.first];
      vec.insert(vec.end(), kv.second.begin(), kv.second.end());
    }
    return lhs;
  }

  template <typename SerializerT>
  void serialize(SerializerT& s) {
    s | by_rank;
  }

  std::unordered_map<int, std::vector<T>> by_rank;
};

template <typename ProxyT>
template <typename T>
std::unordered_map<int, std::vector<T>>
ProxyWrapper<ProxyT>::allgather(T const* sendbuf, int sendcount) {
  std::unordered_map<int, std::vector<T>> out;
  collective_ctx_->out_ptr = static_cast<void*>(&out);
  collective_ctx_->done.store(false);

  std::vector<T> local_vec;
  if (sendcount > 0) {
    local_vec.resize(static_cast<std::size_t>(sendcount));
    std::memcpy(local_vec.data(), sendbuf, sizeof(T) * static_cast<std::size_t>(sendcount));
  }
  AllReduceContainer<T> container{local_vec};
  collective_proxy_.template allreduce<
    &CollectiveHandlerType::template allgatherValues<AllReduceContainer<T>>,
    vt::collective::PlusOp
  >(
    container
  );

  while (!collective_ctx_->done.load(std::memory_order_acquire)) {
    vt::theSched()->runSchedulerOnceImpl();
  }
  return out;
}

} // namespace vt_lb::comm

#endif /* INCLUDED_VT_LB_COMM_PROXY_WRAPPER_IMPL_H */
