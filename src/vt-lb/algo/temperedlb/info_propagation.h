/*
//@HEADER
// *****************************************************************************
//
//                              info_propagation.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_INFO_PROPAGATION_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_INFO_PROPAGATION_H

#include <vt-lb/config/cmake_config.h>

#include <random>
#include <unordered_map>
#include <unordered_set>

namespace vt_lb::algo::temperedlb {

template <typename CommT, typename DataT>
struct InformationPropagation {
  using ThisType = InformationPropagation<CommT, DataT>;
  using JoinedDataType = std::unordered_map<int, DataT>;
  using HandleType = typename CommT::template HandleType<ThisType>;

  /**
   * @brief Construct information propagation instance
   *
   * @param comm Communication interface -- n.b., we clone comm to create a new termination scope
   * @param f Fanout parameter
   * @param k_max Maximum number of rounds
   * @param deterministic Whether to use deterministic selection
   *
   */
  InformationPropagation(CommT& comm, Configuration const& config)
    : comm_(comm.clone()), // collective operation
      f_(config.f_),
      k_max_(config.k_max_),
      deterministic_(config.deterministic_)
  {
    handle_ = comm_.template registerInstanceCollective<ThisType>(this);

    if (deterministic_) {
      gen_select_.seed(config.seed_ + comm_.getRank());
    }
  }

  JoinedDataType run(DataT initial_data) {
    // Insert this rank to avoid self-selection
    already_selected_.insert(comm_.getRank());

    local_data_[comm_.getRank()] = initial_data;

    sendToFanout(1, local_data_);

    // Wait for termination to happen
    while (comm_.poll()) {
      // do nothing
    }

    VT_LB_LOG(
      LoadBalancer, verbose,
      "done with poll: local_data size={}\n", local_data_.size()
    );

    return local_data_;
  }

  void sendToFanout(int round, JoinedDataType const& data) {
    int const rank = comm_.getRank();
    int const num_ranks = comm_.numRanks();

    sent_count_ = 0;
    recv_count_ = 0;

    for (int i = 1; i <= f_; ++i) {
      if (already_selected_.size() >= static_cast<size_t>(num_ranks)) {
        return;
      }

      std::uniform_int_distribution<int> dist(0, num_ranks - 1);
      int target = -1;
      do {
        target = dist(gen_select_);
      } while (already_selected_.find(target) != already_selected_.end());

      already_selected_.insert(target);

      //printf("rank %d sending to rank %d\n", comm_.getRank(), target);
      sent_count_++;
      handle_[target].template send<&ThisType::infoPropagateHandler>(rank, round, data);
    }

    if (deterministic_) {
      // In deterministic mode, we expect an ack from each sent message
      while (sent_count_ != recv_count_) {
        comm_.poll();
      }

      if (round < k_max_) {
        sendToFanout(round + 1, local_data_);
      }
    }
  }

  void infoAckHandler() {
    recv_count_++;
    //printf("rank %d received ack %d/%d\n", comm_.getRank(), recv_count_, sent_count_);
  }

  void infoPropagateHandler(int from_rank, int round, JoinedDataType incoming_data) {
    // Process incoming data and add to local data
    local_data_.insert(incoming_data.begin(), incoming_data.end());

    if (deterministic_) {
      // Acknowledge receipt of message to sender before we go to the next round
      handle_[from_rank].template send<&ThisType::infoAckHandler>();
    } else {
      if (round < k_max_) {
        sendToFanout(round + 1, local_data_);
      }
    }
  }

private:
  CommT comm_;
  int f_ = 2;
  int k_max_ = 2;
  bool deterministic_ = false;
  int sent_count_ = 0;
  int recv_count_ = 0;
  std::unordered_set<int> already_selected_;
  std::unordered_map<int, DataT> local_data_;
  std::mt19937 gen_select_{std::random_device{}()};
  HandleType handle_;
};

} /*end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_INFO_PROPAGATION_H*/