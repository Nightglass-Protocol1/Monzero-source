// Copyright (c) 2026, The Monzero Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <algorithm>
#include <cstdint>

namespace tools
{
  // A wallet's refresh-from-block-height above everything the daemon has or is
  // syncing towards cannot be right: it comes from a date-based estimate made
  // while no daemon answered, or from a mistaken setting. Scanning from it would
  // skip every block, so the wallet would never see its outputs. Returns the
  // height to scan from: unchanged when plausible, otherwise 0, because scanning
  // from genesis can only find more of the wallet's outputs, never fewer.
  inline uint64_t plausible_refresh_start_height(uint64_t refresh_from_block_height,
      uint64_t daemon_height, uint64_t daemon_target_height)
  {
    if (refresh_from_block_height > std::max(daemon_height, daemon_target_height))
      return 0;
    return refresh_from_block_height;
  }
}
