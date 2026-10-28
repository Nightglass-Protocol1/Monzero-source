// Copyright (c) 2026, The Monzero Project
// SPDX-License-Identifier: BSD-3-Clause
#include "gtest/gtest.h"
#include "wallet/refresh_start_height.h"

TEST(refresh_start_height, keeps_heights_the_daemon_has_reached)
{
  EXPECT_EQ(0u, tools::plausible_refresh_start_height(0, 1044, 0));
  EXPECT_EQ(900u, tools::plausible_refresh_start_height(900, 1044, 0));
  EXPECT_EQ(1044u, tools::plausible_refresh_start_height(1044, 1044, 0));
}

TEST(refresh_start_height, keeps_heights_a_syncing_daemon_is_heading_for)
{
  // A daemon still syncing reports a low height but a higher target
  EXPECT_EQ(900u, tools::plausible_refresh_start_height(900, 1, 1044));
  EXPECT_EQ(1044u, tools::plausible_refresh_start_height(1044, 50, 1044));
}

TEST(refresh_start_height, rescans_from_genesis_when_height_is_impossible)
{
  // Date-based estimate made while no daemon answered (seen on a fresh GUI install)
  EXPECT_EQ(0u, tools::plausible_refresh_start_height(14862851, 1044, 0));
  EXPECT_EQ(0u, tools::plausible_refresh_start_height(1045, 1044, 0));
  EXPECT_EQ(0u, tools::plausible_refresh_start_height(2000, 1, 1044));
}
