#include "asset_confidential.h"

#include <cstring>

#include "ringct/bulletproofs_plus.h"
#include "ringct/rctOps.h"
#include "ringct/rctSigs.h"

namespace cryptonote
{
namespace assets
{
namespace
{
  constexpr char OWNERSHIP_DOMAIN[] = "MonzeroAssetOwnershipCLSAGV1";

  bool fail(std::string* error, const std::string& message)
  {
    if (error)
      *error = message;
    return false;
  }

  bool valid_points(const rct::keyV& points)
  {
    for (const rct::key& point : points)
      if (!rct::isInMainSubgroup(point))
        return false;
    return true;
  }


  template<typename T>
  void append_pod(std::vector<uint8_t>& bytes, const T& value)
  {
    const auto* begin = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(value));
  }
}

bool derive_asset_ownership_message(const asset_ownership_proof& proof,
  network_type network, const crypto::hash& carrier_prefix_hash,
  rct::key& message, std::string* error)
{
  if (network != MAINNET && network != TESTNET && network != STAGENET)
    return fail(error, "ownership proof requires a public network");
  if (proof.asset_id == crypto::null_hash || carrier_prefix_hash == crypto::null_hash)
    return fail(error, "ownership proof has a zero asset or carrier id");
  if (proof.ring.size() != CONFIDENTIAL_ASSET_RING_SIZE)
    return fail(error, "ownership proof has the wrong ring size");
  std::set<crypto::hash> output_ids;
  std::vector<uint8_t> bytes(OWNERSHIP_DOMAIN, OWNERSHIP_DOMAIN + sizeof(OWNERSHIP_DOMAIN) - 1);
  const config_t& config = get_config(network);
  append_pod(bytes, config.NETWORK_ID);
  append_pod(bytes, carrier_prefix_hash);
  append_pod(bytes, proof.asset_id);
  append_pod(bytes, proof.pseudo_input);
  for (const asset_ring_member& member : proof.ring)
  {
    if (member.asset_id != proof.asset_id)
      return fail(error, "ownership ring crosses asset domains");
    if (member.output_id == crypto::null_hash || !output_ids.insert(member.output_id).second)
      return fail(error, "ownership ring has a zero or duplicate output id");
    if (!rct::isInMainSubgroup(member.public_output.dest)
        || !rct::isInMainSubgroup(member.public_output.mask))
      return fail(error, "ownership ring contains an invalid curve point");
    append_pod(bytes, member.output_id);
    append_pod(bytes, member.public_output.dest);
    append_pod(bytes, member.public_output.mask);
  }
  const crypto::hash digest = crypto::cn_fast_hash(bytes.data(), bytes.size());
  std::memcpy(&message, &digest, sizeof(message));
  return true;
}

bool verify_asset_ownership_proof(const asset_ownership_proof& proof,
  network_type network, const crypto::hash& carrier_prefix_hash,
  std::string* error)
{
  rct::key message;
  if (!derive_asset_ownership_message(proof, network, carrier_prefix_hash, message, error))
    return false;
  if (proof.signature.s.size() != proof.ring.size())
    return fail(error, "ownership CLSAG response count does not match its ring");
  rct::clsag signature = proof.signature;
  std::memcpy(&signature.I, &proof.key_image, sizeof(signature.I));
  rct::ctkeyV ring;
  ring.reserve(proof.ring.size());
  for (const asset_ring_member& member : proof.ring)
    ring.push_back(member.public_output);
  try
  {
    if (!rct::verRctCLSAGSimple(message, signature, ring, proof.pseudo_input))
      return fail(error, "invalid asset ownership CLSAG");
  }
  catch (const std::exception&)
  {
    return fail(error, "malformed asset ownership CLSAG");
  }
  return true;
}

bool verify_confidential_asset_balance(const confidential_asset_balance& balance, std::string* error)
{
  if (balance.asset_id == crypto::null_hash)
    return fail(error, "confidential balance has a zero asset id");
  if (balance.pseudo_inputs.empty())
    return fail(error, "confidential balance has no pseudo inputs");
  if (balance.outputs.empty() && balance.burns.empty())
    return fail(error, "confidential balance has no outputs or burns");
  if (balance.pseudo_inputs.size() > MAX_CONFIDENTIAL_ASSET_INPUTS
      || balance.outputs.size() + balance.burns.size() > MAX_CONFIDENTIAL_ASSET_OUTPUTS)
    return fail(error, "confidential balance exceeds input or output limits");
  rct::keyV input_commitments;
  input_commitments.reserve(balance.pseudo_inputs.size());
  for (const confidential_pseudo_input& input : balance.pseudo_inputs)
  {
    if (input.source_asset_id != balance.asset_id)
      return fail(error, "pseudo input crosses asset balance domains");
    input_commitments.push_back(input.commitment);
  }
  if (!valid_points(input_commitments) || !valid_points(balance.outputs) || !valid_points(balance.burns))
    return fail(error, "confidential balance contains an invalid curve point");

  const size_t commitment_count = balance.outputs.size() + balance.burns.size();
  size_t proof_commitments = 0;
  rct::keyV proven_commitments;
  proven_commitments.reserve(commitment_count);
  for (const rct::BulletproofPlus& proof : balance.range_proofs)
  {
    if (proof.V.empty() || proof_commitments > commitment_count - proof.V.size())
      return fail(error, "range proofs do not cover the declared commitments exactly");
    try
    {
      if (!rct::bulletproof_plus_VERIFY(proof))
        return fail(error, "invalid asset Bulletproof+");
    }
    catch (const std::exception&)
    {
      return fail(error, "malformed asset Bulletproof+");
    }
    for (const rct::key& scaled : proof.V)
    {
      proven_commitments.push_back(rct::scalarmult8(scaled));
    }
    proof_commitments += proof.V.size();
  }
  if (proof_commitments != commitment_count)
    return fail(error, "range proofs do not cover every output and burn");
  for (size_t index = 0; index < balance.outputs.size(); ++index)
    if (!rct::equalKeys(proven_commitments[index], balance.outputs[index]))
      return fail(error, "range proof output commitment mismatch");
  for (size_t index = 0; index < balance.burns.size(); ++index)
    if (!rct::equalKeys(proven_commitments[balance.outputs.size() + index], balance.burns[index]))
      return fail(error, "range proof burn commitment mismatch");

  const rct::key input_sum = rct::addKeys(input_commitments);
  rct::keyV destinations = balance.outputs;
  destinations.insert(destinations.end(), balance.burns.begin(), balance.burns.end());
  const rct::key output_sum = rct::addKeys(destinations);
  if (!rct::equalKeys(input_sum, output_sum))
    return fail(error, "asset commitments do not conserve value");
  return true;
}

bool verify_confidential_asset_transaction(
  const std::vector<confidential_asset_balance>& balances,
  const std::set<crypto::hash>& known_assets,
  const boost::optional<issuance_descriptor>& issuance,
  std::string* error)
{
  if (balances.empty())
    return fail(error, "confidential asset transaction is empty");
  std::set<crypto::hash> seen;
  crypto::hash issued_id{};
  if (issuance && !derive_asset_id(*issuance, issued_id, error))
    return false;
  bool found_issuance = false;
  for (const confidential_asset_balance& balance : balances)
  {
    if (!seen.insert(balance.asset_id).second)
      return fail(error, "asset appears in more than one balance group");
    const bool is_issuance = issuance && balance.asset_id == issued_id;
    if (!is_issuance && known_assets.count(balance.asset_id) == 0)
      return fail(error, "confidential balance references an unknown asset");
    if (is_issuance)
    {
      if (known_assets.count(balance.asset_id) != 0)
        return fail(error, "confidential issuance duplicates an existing asset");
      if (balance.pseudo_inputs.size() != 1
          || !rct::equalKeys(balance.pseudo_inputs.front().commitment, rct::commit(issuance->atomic_supply, rct::zero()))
          || balance.pseudo_inputs.front().source_asset_id != balance.asset_id)
        return fail(error, "issuance pseudo input does not commit to the fixed supply");
      found_issuance = true;
    }
    if (!verify_confidential_asset_balance(balance, error))
      return false;
  }
  if (issuance && !found_issuance)
    return fail(error, "issuance has no matching confidential balance group");
  return true;
}

bool verify_confidential_asset_transaction_with_ownership(
  const std::vector<confidential_asset_balance>& balances,
  const std::vector<asset_ownership_proof>& ownership_proofs,
  const std::set<crypto::hash>& known_assets,
  const boost::optional<issuance_descriptor>& issuance,
  network_type network, const crypto::hash& carrier_prefix_hash,
  std::string* error)
{
  if (!verify_confidential_asset_transaction(balances, known_assets, issuance, error))
    return false;
  crypto::hash issued_id{};
  if (issuance && !derive_asset_id(*issuance, issued_id, error))
    return false;
  std::vector<bool> matched(ownership_proofs.size(), false);
  std::vector<crypto::key_image> key_images;
  for (const confidential_asset_balance& balance : balances)
  {
    const bool is_issuance = issuance && balance.asset_id == issued_id;
    for (size_t input_index = 0; input_index < balance.pseudo_inputs.size(); ++input_index)
    {
      if (is_issuance && input_index == 0)
        continue;
      const confidential_pseudo_input& input = balance.pseudo_inputs[input_index];
      size_t match = ownership_proofs.size();
      for (size_t proof_index = 0; proof_index < ownership_proofs.size(); ++proof_index)
      {
        if (!matched[proof_index]
            && ownership_proofs[proof_index].asset_id == input.source_asset_id
            && rct::equalKeys(ownership_proofs[proof_index].pseudo_input, input.commitment))
        {
          if (match != ownership_proofs.size())
            return fail(error, "multiple ownership proofs match one pseudo input");
          match = proof_index;
        }
      }
      if (match == ownership_proofs.size())
        return fail(error, "pseudo input has no ownership proof");
      for (const crypto::key_image& image : key_images)
        if (image == ownership_proofs[match].key_image)
          return fail(error, "duplicate asset key image in transaction");
      key_images.push_back(ownership_proofs[match].key_image);
      if (!verify_asset_ownership_proof(
            ownership_proofs[match], network, carrier_prefix_hash, error))
        return false;
      matched[match] = true;
    }
  }
  for (const bool used : matched)
    if (!used)
      return fail(error, "ownership proof does not match a pseudo input");
  return true;
}
}
}
