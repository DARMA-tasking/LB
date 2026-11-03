/*
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia, LLC
// SPDX-License-Identifier: BSD-3-Clause
*/

#include <gtest/gtest.h>
#include <vt-lb/algo/temperedlb/transfer_util.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>
#include <vector>

namespace vt_lb::tests::unit {

using algo::temperedlb::RankInfo;
using algo::temperedlb::TransferUtil;

namespace {

void expectCMF(
  TransferUtil::CMFType type,
  std::unordered_map<int, RankInfo> const& loads,
  double target,
  std::vector<int> const& destinations,
  std::vector<double> const& expected
) {
  auto const cmf = TransferUtil::createCMF(type, loads, target, destinations);
  ASSERT_EQ(cmf.size(), expected.size());
  for (std::size_t i = 0; i < cmf.size(); ++i) {
    ASSERT_TRUE(std::isfinite(cmf[i])) << "CMF entry " << i;
    EXPECT_DOUBLE_EQ(cmf[i], expected[i]) << "CMF entry " << i;
  }

  std::random_device random_seed;
  for (unsigned seed = 0; seed < 16; ++seed) {
    auto const selected = TransferUtil::sampleFromCMF(
      true, destinations, cmf, std::mt19937{seed}, random_seed
    );
    EXPECT_NE(
      std::find(destinations.begin(), destinations.end(), selected),
      destinations.end()
    ) << "Selected invalid rank " << selected << " with seed " << seed;
  }
}

} // namespace

struct TestTransferUtilCMF : testing::TestWithParam<TransferUtil::CMFType> {};

TEST_P(TestTransferUtilCMF, equal_loads_at_target_are_uniform) {
  expectCMF(
    GetParam(), {{3, {2.0, 1.0}}, {7, {2.0, 1.0}}, {11, {2.0, 1.0}}},
    2.0, {3, 7, 11}, {1.0 / 3.0, 2.0 / 3.0, 1.0}
  );
}

TEST_P(TestTransferUtilCMF, equal_loads_above_target_are_uniform) {
  expectCMF(
    GetParam(), {{3, {4.0, 1.0}}, {7, {4.0, 1.0}}},
    2.0, {3, 7}, {0.5, 1.0}
  );
}

TEST_P(TestTransferUtilCMF, equal_scaled_loads_at_target_are_uniform) {
  expectCMF(
    GetParam(), {{3, {1.0, 2.0}}, {7, {2.0, 1.0}}},
    2.0, {3, 7}, {0.5, 1.0}
  );
}

TEST_P(TestTransferUtilCMF, positive_weights_preserve_distribution) {
  expectCMF(
    GetParam(), {{3, {1.0, 1.0}}, {7, {0.0, 1.0}}},
    2.0, {3, 7}, {1.0 / 3.0, 1.0}
  );
}

TEST_P(TestTransferUtilCMF, single_destination_has_probability_one) {
  expectCMF(GetParam(), {{7, {2.0, 1.0}}}, 2.0, {7}, {1.0});
}

INSTANTIATE_TEST_SUITE_P(
  Normalized, TestTransferUtilCMF,
  testing::Values(
    TransferUtil::CMFType::NormByMax,
    TransferUtil::CMFType::NormByMaxExcludeIneligible
  )
);

TEST(TestTransferUtil, original_positive_weights_preserve_distribution) {
  expectCMF(
    TransferUtil::CMFType::Original, {{3, {1.0, 1.0}}, {7, {0.0, 1.0}}},
    2.0, {3, 7}, {1.0 / 3.0, 1.0}
  );
}

} // namespace vt_lb::tests::unit
