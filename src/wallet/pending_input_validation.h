// Copyright (c) 2026, The Monzero Project
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <unordered_set>
#include "cryptonote_basic/cryptonote_basic.h"

namespace tools
{
  // Validate saved wallet-local indices against actual transaction key images.
  // No RPC, mutation, or secret-bearing diagnostic. The caller owns the wallet
  // lock for validation through commit; a successful check is not trade consent.
  template<typename Lookup>
  bool pending_inputs_match_wallet(const cryptonote::transaction_prefix &tx,
      const std::vector<size_t> &selected, size_t transfer_count, Lookup lookup)
  {
    if (selected.empty() || selected.size() != tx.vin.size())
      return false;
    std::unordered_set<size_t> indices;
    std::unordered_set<crypto::key_image> images;
    for (const size_t idx: selected)
    {
      if (idx >= transfer_count || !indices.insert(idx).second)
        return false;
      const auto &output = lookup(idx);
      if (!output.m_key_image_known || output.m_key_image_partial
          || output.m_key_image == crypto::key_image{}
          || !images.insert(output.m_key_image).second)
        return false;
    }
    for (const auto &input: tx.vin)
    {
      const auto *key_input = boost::get<cryptonote::txin_to_key>(&input);
      if (!key_input || images.erase(key_input->k_image) != 1)
        return false;
    }
    return images.empty();
  }
}
