#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cryptonote_basic.h"
#include "ringct/rctTypes.h"

namespace cryptonote
{
namespace assets
{
  // Wallet-facing data required to discover and decode one confidential asset
  // output. Each output has an independent transaction key, with the sender
  // explicitly selecting standard-address or subaddress key publication.
  struct asset_recipient_data
  {
    rct::key destination{};
    crypto::public_key tx_public_key{};
    rct::ecdhTuple encrypted_amount{};
    crypto::view_tag view_tag{};
  };

  struct decoded_asset_recipient
  {
    uint64_t amount = 0;
    rct::key mask{};
  };

  bool make_asset_recipient_data(
    const account_public_address& recipient,
    bool is_subaddress,
    const crypto::secret_key& tx_secret_key,
    size_t output_index,
    uint64_t amount,
    asset_recipient_data& output,
    rct::key& commitment,
    std::string* error = nullptr);

  // Variant for transaction builders that coordinate masks across every
  // input and output to satisfy commitment conservation.
  bool make_asset_recipient_data_with_mask(
    const account_public_address& recipient,
    bool is_subaddress,
    const crypto::secret_key& tx_secret_key,
    size_t output_index,
    uint64_t amount,
    const rct::key& mask,
    asset_recipient_data& output,
    rct::key& commitment,
    std::string* error = nullptr);

  // Returns true only when the output belongs to the supplied public spend
  // key and view secret key and its decoded opening matches the commitment.
  bool decode_asset_recipient_data(
    const asset_recipient_data& input,
    const crypto::public_key& spend_public_key,
    const crypto::secret_key& view_secret_key,
    size_t output_index,
    const rct::key& commitment,
    decoded_asset_recipient& decoded,
    std::string* error = nullptr);
}
}
