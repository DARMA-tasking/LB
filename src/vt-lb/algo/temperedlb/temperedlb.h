/*
//@HEADER
// *****************************************************************************
//
//                                 temperedlb.h
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

#if !defined INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H
#define INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H

#include <vt-lb/comm/comm_traits.h>
#include <vt-lb/algo/baselb/baselb.h>

#include <limits>
#include <random>

namespace vt_lb::algo::temperedlb {

struct WorkModel {
  /// @brief  Coefficient for load component (per rank)
  double rank_alpha = 1.0;
  /// @brief  Coefficient for inter-node communication component
  double beta = 0.0;
  /// @brief  Coefficient for intra-node communication component
  double gamma = 0.0;
  /// @brief  Coefficient for shared-memory communication component
  double delta = 0.0;
};

struct Configuration {
  Configuration() = default;
  Configuration(int num_ranks) {
    f_ = 2;
    k_max_ = std::ceil(std::sqrt(std::log(num_ranks)/std::log(2.0)));
  }

  /// @brief  Number of trials to perform
  int num_trials_ = 1;
  /// @brief  Number of iterations per trial
  int num_iters_ = 10;
  /// @brief  Fanout for information propagation
  int f_ = 2;
  /// @brief  Number of rounds of information propagation
  int k_max_ = 1;
  /// @brief Whether to use deterministic selection
  bool deterministic_ = true;
  /// @brief Seed for random number generation when deterministic_ is true
  int seed_ = 29;

  bool async_ip_ = true;

  /// @brief  Work model parameters (rank-alpha, beta, gamma, delta)
  WorkModel work_model_;

  /// @brief Tolerance for convergence
  double converge_tolerance_ = 0.01;
};

template <typename CommT, typename DataT, typename JoinT>
struct InformationPropagation {
  using ThisType = InformationPropagation<CommT, DataT, JoinT>;
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
  InformationPropagation(CommT& comm, int f, int k_max, bool deterministic, int seed)
    : comm_(comm.clone()), // collective operation
      f_(f),
      k_max_(k_max),
      deterministic_(deterministic)
  {
    handle_ = comm_.template registerInstanceCollective<ThisType>(this);

    if (deterministic_) {
      gen_select_.seed(seed + comm_.getRank());
    }
  }

  void run(DataT initial_data) {
    // Insert this rank to avoid self-selection
    already_selected_.insert(comm_.getRank());

    local_data_[comm_.getRank()] = initial_data;

    sendToFanout(1, local_data_);

    // Wait for termination to happen
    while (comm_.poll()) {
      // do nothing
    }

    printf("%d: done with poll: local_data size=%zu\n", comm_.getRank(), local_data_.size());
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

template <typename CommT>
struct TemperedLB : baselb::BaseLB {
  using HandleType = typename CommT::template HandleType<TemperedLB<CommT>>;


  // Assert that CommT conforms to the communication interface we expect
  static_assert(comm::is_comm_conformant<CommT>::value, "CommT must be comm conformant");

  /**
   * @brief Construct a new TemperedLB object
   *
   * @param comm Communication interface
   * @param config Configuration parameters
   */
  TemperedLB(CommT& comm, Configuration config = Configuration())
      : comm_(comm),
        config_(config)
  { }

  void makeHandle() {
    // printf("makeHandle\n");
    handle_ = comm_.template registerInstanceCollective<TemperedLB<CommT>>(this);
  }

  void run() {
    auto total_load = computeLoad();
    printf("%d: initial total load: %f\n", comm_.getRank(), total_load);

    auto& wm = config_.work_model_;
    if (wm.beta == 0.0 && wm.gamma == 0.0 && wm.delta == 0.0) {
      using LoadType = double;
      auto ip = InformationPropagation<CommT, LoadType, TemperedLB<CommT>>(
        comm_,
        config_.f_,
        config_.k_max_,
        config_.deterministic_,
        config_.seed_
      );
      ip.run(total_load);
    }

  }

private:
  /// @brief Communication interface
  CommT& comm_;
  /// @brief Configuration parameters
  Configuration config_;
  /// @brief Handle to this load balancer instance
  HandleType handle_;
};

} /* end namespace vt_lb::algo::temperedlb */

#endif /*INCLUDED_VT_LB_ALGO_TEMPEREDLB_TEMPEREDLB_H*/
