/*
//@HEADER
// *****************************************************************************
//
//                              test_graph_helpers.cc
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
#include "graph_helpers.h"

#include <vt-lb/model/PhaseData.h>

#include <random>

namespace vt_lb { namespace tests { namespace unit {

template <comm::Communicator CommType>
struct TestGraphHelpers: TestParallelHarness<CommType> {
};

TYPED_TEST_SUITE(TestGraphHelpers, CommTypesForTesting, CommNameGenerator);

TYPED_TEST(TestGraphHelpers, test_generate_rank_footprint_bytes) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(3635 * rank);

  int min_mem = 100;
  int max_mem = 101;
  std::uniform_int_distribution<> dist(min_mem, max_mem);

  generateRankFootprintBytes(pd, gen, dist);

  // check that the value falls within the distribution
  EXPECT_GE(pd.getRankFootprintBytes(), min_mem);
  EXPECT_LE(pd.getRankFootprintBytes(), max_mem);
};

TYPED_TEST(TestGraphHelpers, test_generate_shared_block_counts_per_rank) {
  auto rank = this->comm.getRank();
  vt_lb::model::PhaseData pd(rank);

  std::mt19937 gen(837 * rank);

  int min_blocks = 10;
  int max_blocks = 30;
  std::uniform_int_distribution<> dist(min_blocks, max_blocks);

  generateSharedBlockCountsPerRank(pd, gen, dist, max_blocks);

  // check that the value falls within the distribution
  auto &blocks = pd.getSharedBlocksMap();
  int count = blocks.size();
  EXPECT_GE(count, min_blocks);
  EXPECT_LE(count, max_blocks);
};

}}} // end namespace vt_lb::tests::unit
