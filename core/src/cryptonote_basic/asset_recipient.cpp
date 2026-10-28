#include "asset_recipient.h"

#include <cstring>

#include "ringct/rctOps.h"

namespace cryptonote
{
namespace assets
{
namespace
{
  bool fail(std::string* error, const std::string& message)
  {
    if (error)
      *error = message;
    return false;
  }

  rct::key derivation_scalar(const crypto::key_derivation& derivation,
    size_t output_index)
  {
    crypto::ec_scalar scalar{};
    crypto::derivation_to_scalar(derivation, output_index, scalar);
    rct::key result{};
    static_assert(sizeof(result) == sizeof(scalar), "scalar representations differ");
    std::memcpy(&result, &scalar, sizeof(result));
    return result;
  }
}

bool make_asset_recipient_data(const account_public_address& recipient,
  bool is_subaddress, const crypto::secret_key& tx_secret_key,
  size_t output_index, uint64_t amount, asset_recipient_data& output,
  rct::key& commitment, std::string* error)
{
  crypto::key_derivation derivation{};
  if (!crypto::generate_key_derivation(recipient.m_view_public_key,
        tx_secret_key, derivation))
    return fail(error, "failed to derive asset output shared secret");
  const rct::key amount_key = derivation_scalar(derivation, output_index);
  return make_asset_recipient_data_with_mask(recipient, is_subaddress,
    tx_secret_key, output_index, amount, rct::genCommitmentMask(amount_key),
    output, commitment, error);
}

bool make_asset_recipient_data_with_mask(const account_public_address& recipient,
  bool is_subaddress, const crypto::secret_key& tx_secret_key,
  size_t output_index, uint64_t amount, const rct::key& mask,
  asset_recipient_data& output, rct::key& commitment, std::string* error)
{
  if (sc_check(mask.bytes) != 0)
    return fail(error, "asset output mask is not a reduced scalar");
  crypto::public_key tx_public_key{};
  crypto::public_key base_tx_public_key{};
  crypto::key_derivation derivation{};
  crypto::public_key destination{};
  if (!crypto::secret_key_to_public_key(tx_secret_key, base_tx_public_key))
    return fail(error, "invalid asset output transaction secret key");
  if (is_subaddress)
  {
    tx_public_key = rct::rct2pk(rct::scalarmultKey(
      rct::pk2rct(recipient.m_spend_public_key), rct::sk2rct(tx_secret_key)));
  }
  else
    tx_public_key = base_tx_public_key;
  if (!crypto::generate_key_derivation(recipient.m_view_public_key,
        tx_secret_key, derivation))
    return fail(error, "failed to derive asset output shared secret");
  if (!crypto::derive_public_key(derivation, output_index,
        recipient.m_spend_public_key, destination))
    return fail(error, "failed to derive asset output destination");

  asset_recipient_data candidate{};
  candidate.destination = rct::pk2rct(destination);
  candidate.tx_public_key = tx_public_key;
  crypto::derive_view_tag(derivation, output_index, candidate.view_tag);

  const rct::key amount_key = derivation_scalar(derivation, output_index);
  candidate.encrypted_amount.mask = mask;
  candidate.encrypted_amount.amount = rct::d2h(amount);
  // Asset balance construction must coordinate output masks. The v2 RingCT
  // ECDH form discards the supplied mask and derives a replacement, so the
  // versioned asset wire format uses the mask-carrying form.
  rct::ecdhEncode(candidate.encrypted_amount, amount_key, false);
  commitment = rct::commit(amount, mask);
  output = candidate;
  return true;
}

bool decode_asset_recipient_data(const asset_recipient_data& input,
  const crypto::public_key& spend_public_key,
  const crypto::secret_key& view_secret_key, size_t output_index,
  const rct::key& commitment, decoded_asset_recipient& decoded,
  std::string* error)
{
  if (!crypto::check_key(input.tx_public_key)
      || !crypto::check_key(rct::rct2pk(input.destination))
      || !rct::isInMainSubgroup(commitment))
    return fail(error, "asset recipient data contains an invalid point");

  crypto::key_derivation derivation{};
  if (!crypto::generate_key_derivation(input.tx_public_key,
        view_secret_key, derivation))
    return fail(error, "failed to derive asset recipient shared secret");
  crypto::view_tag expected_tag{};
  crypto::derive_view_tag(derivation, output_index, expected_tag);
  if (expected_tag != input.view_tag)
    return fail(error, "asset output view tag does not match wallet");

  crypto::public_key expected_destination{};
  if (!crypto::derive_public_key(derivation, output_index,
        spend_public_key, expected_destination))
    return fail(error, "failed to derive expected asset output destination");
  const rct::key expected_destination_key = rct::pk2rct(expected_destination);
  if (std::memcmp(expected_destination_key.bytes, input.destination.bytes,
        sizeof(input.destination.bytes)) != 0)
    return fail(error, "asset output does not belong to wallet");

  const rct::key amount_key = derivation_scalar(derivation, output_index);
  rct::ecdhTuple opening = input.encrypted_amount;
  rct::ecdhDecode(opening, amount_key, false);
  const uint64_t amount = rct::h2d(opening.amount);
  const rct::key expected_commitment = rct::commit(amount, opening.mask);
  if (std::memcmp(expected_commitment.bytes, commitment.bytes,
        sizeof(commitment.bytes)) != 0)
    return fail(error, "asset output opening does not match commitment");

  decoded.amount = amount;
  decoded.mask = opening.mask;
  return true;
}
}
}
