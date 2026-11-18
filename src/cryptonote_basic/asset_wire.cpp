#include "asset_wire.h"

#include <cstring>
#include <limits>

#include "cryptonote_format_utils.h"
#include "ringct/bulletproofs_plus.h"
#include "ringct/rctOps.h"
#include "ringct/rctSigs.h"

namespace cryptonote
{
namespace assets
{
static_assert(MAX_ASSET_WIRE_BYTES == TX_EXTRA_MONZERO_ASSET_MAX_COUNT,
  "asset wire and tx-extra envelope limits must remain identical");

namespace
{
  constexpr char OUTPUT_ID_DOMAIN[] = "MonzeroAssetOutputIdV1";

  bool fail(std::string* error, const std::string& message)
  {
    if (error)
      *error = message;
    return false;
  }

  class writer
  {
  public:
    template<typename T> void pod(const T& value)
    {
      const auto* begin = reinterpret_cast<const uint8_t*>(&value);
      bytes.insert(bytes.end(), begin, begin + sizeof(value));
    }
    void count(size_t value) { pod(static_cast<uint8_t>(value)); }
    void u16(uint16_t value)
    {
      bytes.push_back(static_cast<uint8_t>(value));
      bytes.push_back(static_cast<uint8_t>(value >> 8));
    }
    void u32(uint32_t value)
    {
      for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
    std::vector<uint8_t> bytes;
  };

  class reader
  {
  public:
    explicit reader(const std::vector<uint8_t>& source) : source_(source) {}
    template<typename T> bool pod(T& value)
    {
      if (offset_ > source_.size() || sizeof(value) > source_.size() - offset_)
        return false;
      std::memcpy(&value, source_.data() + offset_, sizeof(value));
      offset_ += sizeof(value);
      return true;
    }
    bool count(size_t limit, size_t& value)
    {
      uint8_t encoded = 0;
      if (!pod(encoded) || encoded > limit)
        return false;
      value = encoded;
      return true;
    }
    bool u16(uint16_t& value)
    {
      uint8_t low = 0, high = 0;
      if (!pod(low) || !pod(high))
        return false;
      value = static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
      return true;
    }
    bool done() const { return offset_ == source_.size(); }
  private:
    const std::vector<uint8_t>& source_;
    size_t offset_ = 0;
  };

  void write_keys(writer& out, const rct::keyV& keys)
  {
    out.count(keys.size());
    for (const rct::key& key : keys)
      out.pod(key);
  }

  bool read_keys(reader& in, size_t limit, rct::keyV& keys)
  {
    size_t count = 0;
    if (!in.count(limit, count))
      return false;
    keys.resize(count);
    for (rct::key& key : keys)
      if (!in.pod(key))
        return false;
    return true;
  }

  void write_range_proof(writer& out, const rct::BulletproofPlus& proof)
  {
    write_keys(out, proof.V);
    out.pod(proof.A); out.pod(proof.A1); out.pod(proof.B);
    out.pod(proof.r1); out.pod(proof.s1); out.pod(proof.d1);
    write_keys(out, proof.L);
    write_keys(out, proof.R);
  }

  bool read_range_proof(reader& in, rct::BulletproofPlus& proof)
  {
    return read_keys(in, MAX_CONFIDENTIAL_ASSET_OUTPUTS, proof.V)
      && in.pod(proof.A) && in.pod(proof.A1) && in.pod(proof.B)
      && in.pod(proof.r1) && in.pod(proof.s1) && in.pod(proof.d1)
      && read_keys(in, 16, proof.L) && read_keys(in, 16, proof.R)
      && !proof.L.empty() && proof.L.size() == proof.R.size();
  }
}

bool validate_asset_transaction_payload_shape(const asset_transaction_payload& payload,
  std::string* error)
{
  if (payload.version != ASSET_TRANSACTION_WIRE_VERSION)
    return fail(error, "unsupported asset transaction wire version");
  if (payload.network != MAINNET && payload.network != TESTNET && payload.network != STAGENET)
    return fail(error, "asset transaction requires a public network");
  if (payload.carrier_prefix_hash == crypto::null_hash)
    return fail(error, "asset transaction has a zero carrier hash");
  if (payload.balances.empty() || payload.balances.size() > MAX_ASSET_BALANCE_GROUPS)
    return fail(error, "asset transaction has an invalid balance-group count");
  if (payload.output_recipients.size() != payload.balances.size())
    return fail(error, "asset transaction output recipients do not match balance groups");
  if (payload.ownership_proofs.size() > MAX_ASSET_OWNERSHIP_PROOFS)
    return fail(error, "asset transaction has too many ownership proofs");
  size_t total_inputs = 0, total_destinations = 0;
  for (size_t group = 0; group < payload.balances.size(); ++group)
  {
    const confidential_asset_balance& balance = payload.balances[group];
    if (balance.pseudo_inputs.empty()
        || balance.pseudo_inputs.size() > MAX_CONFIDENTIAL_ASSET_INPUTS
        || balance.outputs.size() + balance.burns.size() > MAX_CONFIDENTIAL_ASSET_OUTPUTS
        || balance.range_proofs.empty()
        || balance.range_proofs.size() > MAX_ASSET_RANGE_PROOFS)
      return fail(error, "asset balance group exceeds canonical limits");
    if (payload.output_recipients[group].size() != balance.outputs.size())
      return fail(error, "asset output recipient count does not match commitments");
    total_inputs += balance.pseudo_inputs.size();
    total_destinations += balance.outputs.size() + balance.burns.size();
    if (total_inputs > MAX_ASSET_TOTAL_INPUTS
        || total_destinations > MAX_ASSET_TOTAL_DESTINATIONS)
      return fail(error, "asset transaction exceeds aggregate input or destination limits");
  }
  for (const asset_ownership_proof& proof : payload.ownership_proofs)
    if (proof.ring.size() != CONFIDENTIAL_ASSET_RING_SIZE
        || proof.signature.s.size() != CONFIDENTIAL_ASSET_RING_SIZE)
      return fail(error, "asset ownership proof has a noncanonical ring size");
  return true;
}

bool encode_asset_transaction_payload(const asset_transaction_payload& payload,
  std::vector<uint8_t>& encoded, std::string* error)
{
  if (!validate_asset_transaction_payload_shape(payload, error))
    return false;
  writer out;
  out.pod(payload.version);
  out.pod(static_cast<uint8_t>(payload.network));
  out.pod(payload.carrier_prefix_hash);
  out.pod(static_cast<uint8_t>(payload.issuance ? 1 : 0));
  if (payload.issuance)
  {
    std::vector<uint8_t> issuance;
    if (!encode_issuance_payload(*payload.issuance, issuance, error))
      return false;
    if (issuance.size() > std::numeric_limits<uint16_t>::max())
      return fail(error, "asset issuance payload is too large");
    out.u16(static_cast<uint16_t>(issuance.size()));
    out.bytes.insert(out.bytes.end(), issuance.begin(), issuance.end());
  }
  out.count(payload.balances.size());
  for (size_t group = 0; group < payload.balances.size(); ++group)
  {
    const confidential_asset_balance& balance = payload.balances[group];
    out.pod(balance.asset_id);
    out.count(balance.pseudo_inputs.size());
    for (const confidential_pseudo_input& input : balance.pseudo_inputs)
    {
      out.pod(input.source_asset_id);
      out.pod(input.commitment);
    }
    out.count(balance.outputs.size());
    for (size_t index = 0; index < balance.outputs.size(); ++index)
    {
      const asset_recipient_data& recipient = payload.output_recipients[group][index];
      out.pod(recipient.destination);
      out.pod(balance.outputs[index]);
      out.pod(recipient.tx_public_key);
      out.pod(recipient.encrypted_amount.mask);
      out.pod(recipient.encrypted_amount.amount);
      out.pod(recipient.view_tag);
    }
    write_keys(out, balance.burns);
    out.count(balance.range_proofs.size());
    for (const rct::BulletproofPlus& proof : balance.range_proofs)
      write_range_proof(out, proof);
  }
  out.count(payload.ownership_proofs.size());
  for (const asset_ownership_proof& proof : payload.ownership_proofs)
  {
    out.pod(proof.asset_id);
    out.pod(proof.pseudo_input);
    out.pod(proof.key_image);
    for (const asset_ring_member& member : proof.ring)
    {
      out.pod(member.asset_id); out.pod(member.output_id);
      out.pod(member.public_output.dest); out.pod(member.public_output.mask);
    }
    out.pod(proof.signature.c1);
    out.pod(proof.signature.D);
    for (const rct::key& response : proof.signature.s)
      out.pod(response);
  }
  if (out.bytes.size() > MAX_ASSET_WIRE_BYTES)
    return fail(error, "asset transaction payload exceeds maximum size");
  encoded = std::move(out.bytes);
  return true;
}

bool decode_asset_transaction_payload(const std::vector<uint8_t>& encoded,
  asset_transaction_payload& payload, std::string* error)
{
  if (encoded.empty() || encoded.size() > MAX_ASSET_WIRE_BYTES)
    return fail(error, "asset transaction payload has an invalid size");
  reader in(encoded);
  asset_transaction_payload candidate;
  uint8_t network = 0, has_issuance = 0;
  if (!in.pod(candidate.version) || !in.pod(network)
      || !in.pod(candidate.carrier_prefix_hash) || !in.pod(has_issuance)
      || has_issuance > 1)
    return fail(error, "truncated asset transaction header");
  candidate.network = static_cast<network_type>(network);
  if (has_issuance)
  {
    uint16_t size = 0;
    if (!in.u16(size))
      return fail(error, "truncated asset issuance length");
    std::vector<uint8_t> issuance(size);
    for (uint8_t& byte : issuance)
      if (!in.pod(byte))
        return fail(error, "truncated asset issuance payload");
    issuance_payload decoded;
    if (!decode_issuance_payload(issuance, decoded, error))
      return false;
    candidate.issuance = decoded;
  }
  size_t groups = 0;
  if (!in.count(MAX_ASSET_BALANCE_GROUPS, groups) || groups == 0)
    return fail(error, "invalid asset balance-group count");
  candidate.balances.resize(groups);
  candidate.output_recipients.resize(groups);
  for (size_t group = 0; group < groups; ++group)
  {
    confidential_asset_balance& balance = candidate.balances[group];
    size_t inputs = 0, outputs = 0, proofs = 0;
    if (!in.pod(balance.asset_id) || !in.count(MAX_CONFIDENTIAL_ASSET_INPUTS, inputs) || inputs == 0)
      return fail(error, "truncated or invalid asset inputs");
    balance.pseudo_inputs.resize(inputs);
    for (confidential_pseudo_input& input : balance.pseudo_inputs)
      if (!in.pod(input.source_asset_id) || !in.pod(input.commitment))
        return fail(error, "truncated asset pseudo input");
    if (!in.count(MAX_CONFIDENTIAL_ASSET_OUTPUTS, outputs))
      return fail(error, "invalid asset output count");
    balance.outputs.resize(outputs);
    candidate.output_recipients[group].resize(outputs);
    for (size_t index = 0; index < outputs; ++index)
    {
      asset_recipient_data& recipient = candidate.output_recipients[group][index];
      if (!in.pod(recipient.destination) || !in.pod(balance.outputs[index])
          || !in.pod(recipient.tx_public_key)
          || !in.pod(recipient.encrypted_amount.mask)
          || !in.pod(recipient.encrypted_amount.amount)
          || !in.pod(recipient.view_tag))
        return fail(error, "truncated asset output");
    }
    if (!read_keys(in, MAX_CONFIDENTIAL_ASSET_OUTPUTS - outputs, balance.burns)
        || !in.count(MAX_ASSET_RANGE_PROOFS, proofs) || proofs == 0)
      return fail(error, "invalid asset burn or range-proof count");
    balance.range_proofs.resize(proofs);
    for (rct::BulletproofPlus& proof : balance.range_proofs)
      if (!read_range_proof(in, proof))
        return fail(error, "truncated or noncanonical asset range proof");
  }
  size_t ownership_count = 0;
  if (!in.count(MAX_ASSET_OWNERSHIP_PROOFS, ownership_count))
    return fail(error, "invalid asset ownership-proof count");
  candidate.ownership_proofs.resize(ownership_count);
  for (asset_ownership_proof& proof : candidate.ownership_proofs)
  {
    if (!in.pod(proof.asset_id) || !in.pod(proof.pseudo_input) || !in.pod(proof.key_image))
      return fail(error, "truncated asset ownership proof");
    proof.ring.resize(CONFIDENTIAL_ASSET_RING_SIZE);
    for (asset_ring_member& member : proof.ring)
      if (!in.pod(member.asset_id) || !in.pod(member.output_id)
          || !in.pod(member.public_output.dest) || !in.pod(member.public_output.mask))
        return fail(error, "truncated asset ownership ring");
    if (!in.pod(proof.signature.c1) || !in.pod(proof.signature.D))
      return fail(error, "truncated asset CLSAG header");
    proof.signature.s.resize(CONFIDENTIAL_ASSET_RING_SIZE);
    for (rct::key& response : proof.signature.s)
      if (!in.pod(response))
        return fail(error, "truncated asset CLSAG responses");
    std::memcpy(&proof.signature.I, &proof.key_image, sizeof(proof.signature.I));
  }
  if (!in.done() || !validate_asset_transaction_payload_shape(candidate, error))
    return fail(error, in.done() ? "invalid asset transaction payload" : "trailing asset transaction bytes");
  payload = std::move(candidate);
  return true;
}

bool verify_asset_transaction_payload(const asset_transaction_payload& payload,
  const std::set<crypto::hash>& known_assets, network_type expected_network,
  const crypto::hash& expected_carrier_prefix_hash, std::string* error)
{
  if (!validate_asset_transaction_payload_shape(payload, error))
    return false;
  if (payload.network != expected_network)
    return fail(error, "asset transaction belongs to a different network");
  if (payload.carrier_prefix_hash != expected_carrier_prefix_hash)
    return fail(error, "asset transaction carrier hash mismatch");
  boost::optional<issuance_descriptor> descriptor;
  if (payload.issuance)
  {
    if (!verify_issuance_authorization(payload.issuance->descriptor,
          payload.issuance->issuer_signature, error))
      return false;
    descriptor = payload.issuance->descriptor;
  }
  for (const std::vector<asset_recipient_data>& recipients : payload.output_recipients)
    for (const asset_recipient_data& recipient : recipients)
    {
      const rct::key identity = rct::identity();
      if (!rct::isInMainSubgroup(recipient.destination)
          || std::memcmp(recipient.destination.bytes, identity.bytes,
               sizeof(identity.bytes)) == 0
          || recipient.tx_public_key == crypto::null_pkey
          || !crypto::check_key(recipient.tx_public_key))
        return fail(error, "asset transaction contains invalid recipient data");
    }
  return verify_confidential_asset_transaction_with_ownership(
    payload.balances, payload.ownership_proofs, known_assets, descriptor,
    expected_network, expected_carrier_prefix_hash, error);
}

bool create_issuance_transaction_payload(const issuance_payload& issuance,
  const account_public_address& recipient, bool is_subaddress,
  const crypto::hash& carrier_prefix_hash, asset_transaction_payload& payload,
  std::string* error)
{
  if (carrier_prefix_hash == crypto::null_hash)
    return fail(error, "issuance transaction requires a non-zero carrier hash");
  std::vector<uint8_t> canonical_issuance;
  if (!encode_issuance_payload(issuance, canonical_issuance, error))
    return false;
  crypto::hash asset_id{};
  if (!derive_asset_id(issuance.descriptor, asset_id, error))
    return false;

  confidential_asset_balance balance;
  balance.asset_id = asset_id;
  balance.pseudo_inputs.push_back(
    {asset_id, rct::commit(issuance.descriptor.atomic_supply, rct::zero())});
  // A same-asset CLSAG ring cannot be formed from a singleton NFT issuance.
  // Seed the fixed 16-member anonymity set at issuance: one funded output and
  // 15 zero-valued blinded outputs. Their scalar masks sum to zero, preserving
  // the fixed-supply pseudo input without weakening amount confidentiality.
  std::vector<uint64_t> amounts(CONFIDENTIAL_ASSET_RING_SIZE, 0);
  amounts.front() = issuance.descriptor.atomic_supply;
  rct::keyV masks(CONFIDENTIAL_ASSET_RING_SIZE);
  rct::key mask_sum = rct::zero();
  for (size_t index = 0; index + 1 < masks.size(); ++index)
  {
    masks[index] = rct::skGen();
    sc_add(mask_sum.bytes, mask_sum.bytes, masks[index].bytes);
  }
  sc_sub(masks.back().bytes, rct::zero().bytes, mask_sum.bytes);

  std::vector<asset_recipient_data> recipients;
  recipients.reserve(CONFIDENTIAL_ASSET_RING_SIZE);
  balance.outputs.reserve(CONFIDENTIAL_ASSET_RING_SIZE);
  for (size_t index = 0; index < CONFIDENTIAL_ASSET_RING_SIZE; ++index)
  {
    crypto::public_key ignored{};
    crypto::secret_key tx_secret{};
    crypto::generate_keys(ignored, tx_secret);
    asset_recipient_data recipient_data{};
    rct::key output_commitment{};
    if (!make_asset_recipient_data_with_mask(recipient, is_subaddress,
          tx_secret, index, amounts[index], masks[index], recipient_data,
          output_commitment, error))
      return false;
    recipients.push_back(recipient_data);
    balance.outputs.push_back(output_commitment);
  }
  try
  {
    balance.range_proofs.push_back(rct::bulletproof_plus_PROVE(amounts, masks));
  }
  catch (const std::exception& e)
  {
    return fail(error, std::string{"failed to construct issuance range proof: "} + e.what());
  }

  asset_transaction_payload created;
  created.network = issuance.descriptor.network;
  created.carrier_prefix_hash = carrier_prefix_hash;
  created.issuance = issuance;
  created.balances.push_back(std::move(balance));
  created.output_recipients.push_back(std::move(recipients));
  if (!verify_asset_transaction_payload(created, {}, created.network,
        carrier_prefix_hash, error))
    return false;
  payload = std::move(created);
  return true;
}

bool create_asset_transfer_transaction_payload(network_type network,
  const crypto::hash& asset_id, const std::vector<asset_transfer_input>& inputs,
  const std::vector<asset_transfer_destination>& destinations,
  uint64_t burn_amount, const crypto::hash& carrier_prefix_hash,
  asset_transaction_payload& payload, std::string* error)
{
  if (network != MAINNET && network != TESTNET && network != STAGENET)
    return fail(error, "asset transfer requires a public network");
  if (asset_id == crypto::null_hash || carrier_prefix_hash == crypto::null_hash)
    return fail(error, "asset transfer has a zero asset or carrier id");
  if (inputs.empty() || inputs.size() > MAX_CONFIDENTIAL_ASSET_INPUTS)
    return fail(error, "asset transfer has an invalid input count");
  if (destinations.empty() || destinations.size() > MAX_CONFIDENTIAL_ASSET_OUTPUTS)
    return fail(error, "asset transfer has an invalid destination count");

  uint64_t input_total = 0;
  for (const asset_transfer_input& input : inputs)
  {
    if (input.ring.size() != CONFIDENTIAL_ASSET_RING_SIZE
        || input.real_output_index >= input.ring.size())
      return fail(error, "asset transfer requires complete 16-member rings");
    if (sc_check(input.spend_secret.bytes) != 0 || sc_check(input.mask.bytes) != 0)
      return fail(error, "asset transfer input contains an invalid secret scalar");
    if (input.amount > std::numeric_limits<uint64_t>::max() - input_total)
      return fail(error, "asset transfer input amount overflows");
    input_total += input.amount;

    rct::key input_public{};
    if (!rct::equalKeys(input.ring[input.real_output_index].public_output.mask,
          rct::commit(input.amount, input.mask)))
      return fail(error, "asset transfer opening does not match the real ring member");
    rct::scalarmultBase(input_public, input.spend_secret);
    if (!rct::equalKeys(input.ring[input.real_output_index].public_output.dest, input_public))
      return fail(error, "asset transfer spend secret does not match the real ring member");
    for (const auto &member : input.ring)
      if (member.asset_id != asset_id)
        return fail(error, "asset transfer ring crosses asset domains");
  }

  uint64_t destination_total = 0;
  for (const auto &destination : destinations)
  {
    if (destination.amount > std::numeric_limits<uint64_t>::max() - destination_total)
      return fail(error, "asset transfer destination amount overflows");
    destination_total += destination.amount;
  }
  if (burn_amount > std::numeric_limits<uint64_t>::max() - destination_total
      || destination_total + burn_amount != input_total)
    return fail(error, "asset transfer amounts do not consume the inputs exactly");

  const size_t burn_outputs = burn_amount == 0 ? 0 : 1;
  const size_t output_count = MAX_CONFIDENTIAL_ASSET_OUTPUTS - burn_outputs;
  if (destinations.size() > output_count)
    return fail(error, "asset transfer has too many recipients for its burn");
  std::vector<asset_transfer_destination> padded = destinations;
  while (padded.size() < output_count)
  {
    auto zero = destinations.back();
    zero.amount = 0;
    padded.push_back(zero);
  }

  rct::keyV pseudo_masks(inputs.size());
  rct::key pseudo_mask_sum = rct::zero();
  for (rct::key& pseudo_mask : pseudo_masks)
  {
    pseudo_mask = rct::skGen();
    sc_add(pseudo_mask_sum.bytes, pseudo_mask_sum.bytes, pseudo_mask.bytes);
  }
  const size_t commitment_count = output_count + burn_outputs;
  rct::keyV masks(commitment_count);
  rct::key mask_sum = rct::zero();
  for (size_t index = 0; index + 1 < masks.size(); ++index)
  {
    masks[index] = rct::skGen();
    sc_add(mask_sum.bytes, mask_sum.bytes, masks[index].bytes);
  }
  sc_sub(masks.back().bytes, pseudo_mask_sum.bytes, mask_sum.bytes);

  confidential_asset_balance balance;
  balance.asset_id = asset_id;
  for (size_t index = 0; index < inputs.size(); ++index)
    balance.pseudo_inputs.push_back(
      {asset_id, rct::commit(inputs[index].amount, pseudo_masks[index])});
  std::vector<asset_recipient_data> recipients;
  std::vector<uint64_t> proof_amounts;
  proof_amounts.reserve(commitment_count);
  for (size_t index = 0; index < padded.size(); ++index)
  {
    crypto::public_key ignored{};
    crypto::secret_key tx_secret{};
    crypto::generate_keys(ignored, tx_secret);
    asset_recipient_data recipient_data{};
    rct::key commitment{};
    if (!make_asset_recipient_data_with_mask(padded[index].address,
          padded[index].is_subaddress, tx_secret, index, padded[index].amount,
          masks[index], recipient_data, commitment, error))
      return false;
    recipients.push_back(recipient_data);
    balance.outputs.push_back(commitment);
    proof_amounts.push_back(padded[index].amount);
  }
  if (burn_outputs)
  {
    balance.burns.push_back(rct::commit(burn_amount, masks.back()));
    proof_amounts.push_back(burn_amount);
  }
  try
  {
    balance.range_proofs.push_back(rct::bulletproof_plus_PROVE(proof_amounts, masks));
  }
  catch (const std::exception& e)
  {
    return fail(error, std::string{"failed to construct asset range proof: "} + e.what());
  }

  asset_transaction_payload created;
  created.network = network;
  created.carrier_prefix_hash = carrier_prefix_hash;
  created.balances.push_back(std::move(balance));
  created.output_recipients.push_back(std::move(recipients));
  for (size_t input_index = 0; input_index < inputs.size(); ++input_index)
  {
    const asset_transfer_input& input = inputs[input_index];
    asset_ownership_proof ownership;
    ownership.asset_id = asset_id;
    ownership.pseudo_input = created.balances.front().pseudo_inputs[input_index].commitment;
    ownership.ring = input.ring;
    rct::key message{};
    if (!derive_asset_ownership_message(ownership, network,
          carrier_prefix_hash, message, error))
      return false;
    rct::ctkeyV public_ring;
    public_ring.reserve(input.ring.size());
    for (const auto &member : input.ring)
      public_ring.push_back(member.public_output);
    try
    {
      const rct::ctkey input_secret{input.spend_secret, input.mask};
      ownership.signature = rct::proveRctCLSAGSimple(message, public_ring,
        input_secret, pseudo_masks[input_index], ownership.pseudo_input,
        input.real_output_index, hw::get_device("default"));
    }
    catch (const std::exception& e)
    {
      return fail(error, std::string{"failed to construct asset ownership proof: "} + e.what());
    }
    std::memcpy(&ownership.key_image, &ownership.signature.I,
      sizeof(ownership.key_image));
    created.ownership_proofs.push_back(std::move(ownership));
  }
  if (!verify_asset_transaction_payload(created, {asset_id}, network,
        carrier_prefix_hash, error))
    return false;
  payload = std::move(created);
  return true;
}

bool create_asset_transfer_transaction_payload(network_type network,
  const crypto::hash& asset_id, const std::vector<asset_ring_member>& ring,
  size_t real_output_index, const rct::key& input_spend_secret,
  uint64_t input_amount, const rct::key& input_mask,
  const std::vector<asset_transfer_destination>& destinations,
  uint64_t burn_amount, const crypto::hash& carrier_prefix_hash,
  asset_transaction_payload& payload, std::string* error)
{
  return create_asset_transfer_transaction_payload(network, asset_id,
    {{ring, real_output_index, input_spend_secret, input_amount, input_mask}},
    destinations, burn_amount, carrier_prefix_hash, payload, error);
}

bool attach_issuance_to_native_transaction(transaction& tx,
  const issuance_payload& issuance, const account_public_address& recipient,
  bool is_subaddress, asset_transaction_payload& payload, std::string* error)
{
  std::vector<uint8_t> existing;
  bool found = false;
  if (!get_monzero_asset_tx_extra(tx.extra, existing, found, error))
    return false;
  if (found)
    return fail(error, "native transaction already contains an asset envelope");

  crypto::hash carrier{};
  if (!get_transaction_asset_carrier_hash(tx, carrier, error))
    return false;
  asset_transaction_payload created;
  if (!create_issuance_transaction_payload(issuance, recipient, is_subaddress,
        carrier, created, error))
    return false;
  std::vector<uint8_t> encoded;
  if (!encode_asset_transaction_payload(created, encoded, error)
      || !add_monzero_asset_tx_extra(tx.extra, encoded, error))
    return false;

  boost::optional<asset_transaction_payload> parsed;
  if (!parse_native_asset_transaction(tx, HF_VERSION_MONZERO_ASSETS,
        issuance.descriptor.network, parsed, error) || !parsed)
    return fail(error, "attached issuance envelope did not round trip");
  payload = std::move(created);
  return true;
}

bool derive_asset_output_id(network_type network,
  const crypto::hash& carrier_prefix_hash,
  const crypto::hash& asset_id, uint32_t output_index,
  const confidential_asset_output& output, crypto::hash& output_id,
  std::string* error)
{
  if (network != MAINNET && network != TESTNET && network != STAGENET)
    return fail(error, "asset output identity requires a public network");
  if (carrier_prefix_hash == crypto::null_hash || asset_id == crypto::null_hash)
    return fail(error, "asset output identity has a zero carrier or asset id");
  if (!rct::isInMainSubgroup(output.destination)
      || !rct::isInMainSubgroup(output.commitment))
    return fail(error, "asset output identity contains an invalid point");
  writer bytes;
  bytes.bytes.insert(bytes.bytes.end(), OUTPUT_ID_DOMAIN,
    OUTPUT_ID_DOMAIN + sizeof(OUTPUT_ID_DOMAIN) - 1);
  bytes.pod(get_config(network).NETWORK_ID);
  bytes.pod(carrier_prefix_hash); bytes.pod(asset_id);
  bytes.u32(output_index); bytes.pod(output.destination); bytes.pod(output.commitment);
  output_id = crypto::cn_fast_hash(bytes.bytes.data(), bytes.bytes.size());
  return true;
}

bool parse_native_asset_transaction(const transaction_prefix& tx,
  uint8_t hard_fork_version, network_type expected_network,
  boost::optional<asset_transaction_payload>& payload, std::string* error)
{
  payload = boost::none;
  std::vector<uint8_t> encoded;
  bool found = false;
  if (!get_monzero_asset_tx_extra(tx.extra, encoded, found, error))
    return false;
  if (!found)
    return true;
  if (hard_fork_version < HF_VERSION_MONZERO_ASSETS)
    return fail(error, "Monzero asset envelope appears before activation");
  size_t normalized_extra_size = 0;
  if (!get_monzero_asset_normalized_extra_size(tx, normalized_extra_size, error))
    return false;
  if (normalized_extra_size > MAX_TX_EXTRA_SIZE)
    return fail(error, "non-asset transaction extra exceeds the standard limit");
  crypto::hash carrier{};
  if (!get_transaction_asset_carrier_hash(tx, carrier, error))
    return false;
  asset_transaction_payload decoded;
  if (!decode_asset_transaction_payload(encoded, decoded, error))
    return false;
  if (decoded.network != expected_network)
    return fail(error, "native asset envelope belongs to a different network");
  if (decoded.carrier_prefix_hash != carrier)
    return fail(error, "native asset envelope has the wrong carrier hash");
  payload = std::move(decoded);
  return true;
}
}
}
