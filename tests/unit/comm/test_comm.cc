/*
//@HEADER
// *****************************************************************************
//
//                                 test_comm.cc
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

#include <gtest/gtest.h>

#include "test_parallel_harness.h"
#include "test_helpers.h"

#include <atomic>
#include <vector>
#include <numeric>
#include <tuple>
#include <type_traits>

namespace vt_lb { namespace tests { namespace unit {

template <comm::Communicator CommType>
struct TestCommBasic : TestParallelHarness<CommType> {
  struct TestObject {
    int calls = 0;

    void ping(int v) { calls += v; }
  };

  // Member function pointers for send
  static constexpr auto fn_ping = &TestObject::ping;

  using HandleT = typename CommType::template HandleType<TestObject>;

  // helper to register object and return handle
  HandleT makeHandle(TestObject* obj) {
    return this->comm.template registerInstanceCollective<TestObject>(obj);
  }

  // helper to drain progress
  void progressUntil(std::function<bool()> pred, int max_iters = 10000) {
    int it = 0;
    while (!pred() && it++ < max_iters) {
      this->comm.poll();
    }
  }
};

TYPED_TEST_SUITE(TestCommBasic, CommTypesForTesting, CommNameGenerator);

TYPED_TEST(TestCommBasic, test_init_finalize_and_basic_props) {
  auto& the_comm = this->comm;

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  // Basic size/rank API should work
  ASSERT_GE(the_comm.numRanks(), 1);
  ASSERT_GE(the_comm.getRank(), 0);
  ASSERT_LT(the_comm.getRank(), the_comm.numRanks());
  // Poll should be callable
  (void)the_comm.poll();
}

TYPED_TEST(TestCommBasic, test_send_and_poll_dispatch) {
  auto& the_comm = this->comm;

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  typename TestFixture::TestObject obj{};
  auto handle = this->makeHandle(&obj);

  // Send to neighbor in a ring
  int self = the_comm.getRank();
  int n = the_comm.numRanks();
  int dest = (self + 1) % n;

  // Each rank sends to its right neighbor; the left neighbor sends to us with payload (left+1)
  the_comm.template send<TestFixture::fn_ping>(dest, handle, self + 1);

  // Expected value received from left neighbor
  int left = (self - 1 + n) % n;
  int expected = left + 1; // equals self for self>0, equals n for self==0

  // Progress until our local object receives from our left neighbor
  this->progressUntil([&] { return obj.calls == expected; });
  EXPECT_EQ(obj.calls, expected);
}

TYPED_TEST(TestCommBasic, test_reduce_sum_int_single) {
  auto& the_comm = this->comm;

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  typename TestFixture::TestObject obj{};
  auto handle = this->makeHandle(&obj);

  int const root = 0;
  int send = 1;
  int recv = 0;

  handle.reduce(root, MPI_INT, MPI_SUM, &send, &recv, 1);

  if (the_comm.getRank() == root) {
    // Sum over all ranks of 1
    EXPECT_EQ(recv, the_comm.numRanks());
  }
}

TYPED_TEST(TestCommBasic, test_reduce_max_double_array) {
  auto& the_comm = this->comm;

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  typename TestFixture::TestObject obj{};
  auto handle = this->makeHandle(&obj);

  int const root = 0;
  // each rank contributes two doubles, make max depend on rank
  std::array<double,2> send{{double(the_comm.getRank()), double(the_comm.getRank() + 1)}};
  std::array<double,2> recv{{0.0, 0.0}};

  handle.reduce(root, MPI_DOUBLE, MPI_MAX, send.data(), recv.data(), int(send.size()));

  if (the_comm.getRank() == root) {
    EXPECT_DOUBLE_EQ(recv[0], double(the_comm.numRanks() - 1));
    EXPECT_DOUBLE_EQ(recv[1], double(the_comm.numRanks()));
  }
}

TYPED_TEST(TestCommBasic, test_reduce_sum_float_array) {
  auto& the_comm = this->comm;

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  typename TestFixture::TestObject obj{};
  auto handle = this->makeHandle(&obj);

  int const root = 0;
  std::vector<float> send(4, 1.0f); // each rank contributes 4 ones
  std::vector<float> recv(4, 0.0f);

  handle.reduce(root, MPI_FLOAT, MPI_SUM, send.data(), recv.data(), int(send.size()));

  if (the_comm.getRank() == root) {
    for (auto v : recv) {
      EXPECT_FLOAT_EQ(v, float(the_comm.numRanks()));
    }
  }
}

TYPED_TEST(TestCommBasic, test_broadcast_int_single) {
  auto& the_comm = this->comm;

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  typename TestFixture::TestObject obj{};
  auto handle = this->makeHandle(&obj);

  int const root = 0;
  int value = (the_comm.getRank() == root) ? 42 : 0;

  handle.broadcast(root, MPI_INT, &value, 1);

  // All ranks should see the root's value
  EXPECT_EQ(value, 42);
}

TYPED_TEST(TestCommBasic, test_broadcast_int_array) {
  auto& the_comm = this->comm;

  SET_MIN_NUM_NODES_CONSTRAINT(2);

  typename TestFixture::TestObject obj{};
  auto handle = this->makeHandle(&obj);

  int const root = 0;
  std::array<int,4> buf{{0,0,0,0}};
  if (the_comm.getRank() == root) {
    buf = {{1,2,3,4}};
  }

  handle.broadcast(root, MPI_INT, buf.data(), int(buf.size()));

  // After broadcast, all ranks should have the root's values
  EXPECT_EQ(buf[0], 1);
  EXPECT_EQ(buf[1], 2);
  EXPECT_EQ(buf[2], 3);
  EXPECT_EQ(buf[3], 4);
}

}}} // end namespace vt_lb::tests::unit
