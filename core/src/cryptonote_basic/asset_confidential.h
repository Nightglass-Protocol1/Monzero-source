#pragma once

#include <set>
#include <string>
#include <vector>

#include "asset_types.h"
#include "ringct/rctTypes.h"

namespace cryptonote
{
namespace assets
{
  constexpr size_t MAX_CONFIDENTIAL_ASSET_INPUTS = 16;
  constexpr size_t MAX_CONFIDENTIAL_ASSET_OUTPUTS = 16;
  constexpr size_t CONFIDENTIAL_ASSET_RING_SIZE = 16;

  struct confidential_pseudo_input
  {
    crypto::hash source_asset_id{};
    rct::key commitment{};
  };

  struct asset_ring_member
  {
    crypto::hash asset_id{};
    crypto::hash output_id{};
    rct::ctkey public_output{};
  };

  struct asset_ownership_proof
  {
    crypto::hash asset_id{};
    rct::key pseudo_input{};
    crypto::key_image key_image{};
    std::vector<asset_ring_member> ring;
    rct::clsag signature;
  };

  bool derive_asset_ownership_message(
    const asset_ownership_proof& proof,
    network_type network,
    const crypto::hash& carrier_prefix_hash,
    rct::key& message,
    std::string* error = nullptr);

  bool verify_asset_ownership_proof(
    const asset_ownership_proof& proof,
    network_type network,
    const crypto::hash& carrier_prefix_hash,
    std::string* error = nullptr);

  struct confidential_asset_balance
  {
    crypto::hash asset_id{};
    std::vector<confidential_pseudo_input> pseudo_inputs;
    rct::keyV outputs;
    rct::keyV burns;
    std::vector<rct::BulletproofPlus> range_proofs;
  };

  bool verify_confidential_asset_balance(
    const confidential_asset_balance& balance,
    std::string* error = nullptr);

  bool verify_confidential_asset_transaction(
    const std::vector<confidential_asset_balance>& balances,
    const std::set<crypto::hash>& known_assets,
    const boost::optional<issuance_descriptor>& issuance,
    std::string* error = nullptr);

  bool verify_confidential_asset_transaction_with_ownership(
    const std::vector<confidential_asset_balance>& balances,
    const std::vector<asset_ownership_proof>& ownership_proofs,
    const std::set<crypto::hash>& known_assets,
    const boost::optional<issuance_descriptor>& issuance,
    network_type network,
    const crypto::hash& carrier_prefix_hash,
    std::string* error = nullptr);
}
}
