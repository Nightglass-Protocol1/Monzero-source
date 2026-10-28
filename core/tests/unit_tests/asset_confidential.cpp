#include "gtest/gtest.h"

#include <cstring>

#include "cryptonote_basic/asset_confidential.h"
#include "ringct/bulletproofs_plus.h"
#include "ringct/rctOps.h"
#include "ringct/rctSigs.h"
#include "device/device.hpp"

namespace
{
  crypto::hash asset_id(unsigned char value)
  {
    crypto::hash id{};
    id.data[0] = value;
    return id;
  }

  cryptonote::assets::confidential_asset_balance make_balance(
    const crypto::hash& id, uint64_t input_amount,
    const std::vector<uint64_t>& outputs, const std::vector<uint64_t>& burns)
  {
    rct::keyV masks = rct::skvGen(outputs.size() + burns.size());
    rct::key input_mask = rct::zero();
    for (const rct::key& mask : masks)
      sc_add(input_mask.bytes, input_mask.bytes, mask.bytes);
    cryptonote::assets::confidential_asset_balance balance;
    balance.asset_id = id;
    balance.pseudo_inputs.push_back({id, rct::commit(input_amount, input_mask)});
    size_t index = 0;
    for (const uint64_t amount : outputs)
      balance.outputs.push_back(rct::commit(amount, masks[index++]));
    for (const uint64_t amount : burns)
      balance.burns.push_back(rct::commit(amount, masks[index++]));
    std::vector<uint64_t> amounts = outputs;
    amounts.insert(amounts.end(), burns.begin(), burns.end());
    balance.range_proofs.push_back(rct::bulletproof_plus_PROVE(amounts, masks));
    return balance;
  }

  cryptonote::assets::asset_ownership_proof make_ownership_proof(
    const crypto::hash& id, const crypto::hash& carrier, rct::key* generated_pseudo_mask = nullptr)
  {
    constexpr size_t real = 5;
    cryptonote::assets::asset_ownership_proof proof;
    proof.asset_id = id;
    rct::ctkeyV public_ring;
    rct::key spend_secret{}, input_mask{};
    const rct::key amount = rct::d2h(10);
    for (size_t index = 0; index < cryptonote::assets::CONFIDENTIAL_ASSET_RING_SIZE; ++index)
    {
      cryptonote::assets::asset_ring_member member;
      member.asset_id = id;
      member.output_id.data[0] = static_cast<unsigned char>(index + 1);
      rct::key ignored;
      rct::skpkGen(ignored, member.public_output.dest);
      rct::skpkGen(ignored, member.public_output.mask);
      proof.ring.push_back(member);
    }
    rct::skpkGen(spend_secret, proof.ring[real].public_output.dest);
    input_mask = rct::skGen();
    rct::addKeys2(proof.ring[real].public_output.mask, input_mask, amount, rct::H);
    for (const auto& member : proof.ring)
      public_ring.push_back(member.public_output);
    const rct::key pseudo_mask = rct::skGen();
    if (generated_pseudo_mask)
      *generated_pseudo_mask = pseudo_mask;
    rct::addKeys2(proof.pseudo_input, pseudo_mask, amount, rct::H);
    rct::key message;
    std::string error;
    if (!cryptonote::assets::derive_asset_ownership_message(proof, cryptonote::TESTNET, carrier, message, &error))
      throw std::runtime_error(error);
    rct::ctkey input_secret;
    input_secret.dest = spend_secret;
    input_secret.mask = input_mask;
    proof.signature = rct::proveRctCLSAGSimple(
      message, public_ring, input_secret, pseudo_mask, proof.pseudo_input,
      real, hw::get_device("default"));
    std::memcpy(&proof.key_image, &proof.signature.I, sizeof(proof.key_image));
    return proof;
  }
}

TEST(asset_confidential, verifies_private_transfer_and_explicit_burn)
{
  const crypto::hash id = asset_id(1);
  const auto balance = make_balance(id, 10, {7}, {3});
  std::string error;
  ASSERT_TRUE(cryptonote::assets::verify_confidential_asset_balance(balance, &error)) << error;
  ASSERT_TRUE(cryptonote::assets::verify_confidential_asset_transaction({balance}, {id}, boost::none, &error)) << error;
}

TEST(asset_confidential, rejects_inflation_and_commitment_substitution)
{
  const crypto::hash id = asset_id(2);
  std::string error;
  auto inflated = make_balance(id, 10, {11}, {});
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_balance(inflated, &error));
  auto substituted = make_balance(id, 10, {10}, {});
  substituted.outputs.front() = rct::commit(10, rct::skGen());
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_balance(substituted, &error));
  auto malformed = make_balance(id, 10, {10}, {});
  malformed.range_proofs.front().A.bytes[0] ^= 1;
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_balance(malformed, &error));
}

TEST(asset_confidential, rejects_cross_asset_and_duplicate_balance_domains)
{
  const crypto::hash first = asset_id(3);
  const crypto::hash second = asset_id(4);
  std::string error;
  auto crossed = make_balance(first, 9, {9}, {});
  crossed.pseudo_inputs.front().source_asset_id = second;
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_balance(crossed, &error));
  const auto valid = make_balance(first, 9, {9}, {});
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_transaction({valid, valid}, {first}, boost::none, &error));
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_transaction({valid}, {second}, boost::none, &error));
}

TEST(asset_confidential, validates_fixed_supply_issuance_commitment)
{
  crypto::public_key issuer{};
  crypto::secret_key secret{};
  crypto::generate_keys(issuer, secret);
  cryptonote::assets::issuance_descriptor descriptor;
  descriptor.network = cryptonote::TESTNET;
  descriptor.issuer_key = issuer;
  descriptor.atomic_supply = 10;
  descriptor.issuance_nonce.data[0] = 9;
  crypto::hash id{};
  ASSERT_TRUE(cryptonote::assets::derive_asset_id(descriptor, id));
  cryptonote::assets::confidential_asset_balance balance;
  balance.asset_id = id;
  balance.pseudo_inputs.push_back({id, rct::commit(10, rct::zero())});
  balance.outputs.push_back(rct::commit(10, rct::zero()));
  balance.range_proofs.push_back(rct::bulletproof_plus_PROVE(10, rct::zero()));
  std::string error;
  ASSERT_TRUE(cryptonote::assets::verify_confidential_asset_transaction({balance}, {}, descriptor, &error)) << error;
  balance.pseudo_inputs.front().commitment = rct::commit(11, rct::zero());
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_transaction({balance}, {}, descriptor, &error));
}

TEST(asset_confidential, verifies_domain_separated_clsag_ownership)
{
  const crypto::hash id = asset_id(6);
  crypto::hash carrier{};
  carrier.data[0] = 0x77;
  const auto proof = make_ownership_proof(id, carrier);
  std::string error;
  ASSERT_TRUE(cryptonote::assets::verify_asset_ownership_proof(
    proof, cryptonote::TESTNET, carrier, &error)) << error;

  EXPECT_FALSE(cryptonote::assets::verify_asset_ownership_proof(
    proof, cryptonote::MAINNET, carrier, &error));
  crypto::hash other_carrier = carrier;
  other_carrier.data[1] = 1;
  EXPECT_FALSE(cryptonote::assets::verify_asset_ownership_proof(
    proof, cryptonote::TESTNET, other_carrier, &error));
}

TEST(asset_confidential, rejects_clsag_ring_key_image_and_asset_tampering)
{
  const crypto::hash id = asset_id(7);
  crypto::hash carrier{};
  carrier.data[0] = 0x78;
  const auto original = make_ownership_proof(id, carrier);
  std::string error;

  auto changed_reference = original;
  changed_reference.ring[0].output_id.data[1] = 1;
  EXPECT_FALSE(cryptonote::assets::verify_asset_ownership_proof(
    changed_reference, cryptonote::TESTNET, carrier, &error));
  auto wrong_asset = original;
  wrong_asset.ring[0].asset_id = asset_id(8);
  EXPECT_FALSE(cryptonote::assets::verify_asset_ownership_proof(
    wrong_asset, cryptonote::TESTNET, carrier, &error));
  auto wrong_image = original;
  wrong_image.key_image.data[0] ^= 1;
  EXPECT_FALSE(cryptonote::assets::verify_asset_ownership_proof(
    wrong_image, cryptonote::TESTNET, carrier, &error));
  auto duplicate = original;
  duplicate.ring[1].output_id = duplicate.ring[0].output_id;
  EXPECT_FALSE(cryptonote::assets::verify_asset_ownership_proof(
    duplicate, cryptonote::TESTNET, carrier, &error));
}

TEST(asset_confidential, binds_every_pseudo_input_to_one_unique_ownership_proof)
{
  const crypto::hash id = asset_id(9);
  crypto::hash carrier{};
  carrier.data[0] = 0x79;
  rct::key pseudo_mask;
  const auto ownership = make_ownership_proof(id, carrier, &pseudo_mask);
  cryptonote::assets::confidential_asset_balance balance;
  balance.asset_id = id;
  balance.pseudo_inputs.push_back({id, ownership.pseudo_input});
  balance.outputs.push_back(rct::commit(10, pseudo_mask));
  balance.range_proofs.push_back(rct::bulletproof_plus_PROVE(10, pseudo_mask));
  std::string error;
  ASSERT_TRUE(cryptonote::assets::verify_confidential_asset_transaction_with_ownership(
    {balance}, {ownership}, {id}, boost::none, cryptonote::TESTNET, carrier, &error)) << error;
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_transaction_with_ownership(
    {balance}, {}, {id}, boost::none, cryptonote::TESTNET, carrier, &error));
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_transaction_with_ownership(
    {balance}, {ownership, ownership}, {id}, boost::none, cryptonote::TESTNET, carrier, &error));

  auto duplicate_input = balance;
  duplicate_input.pseudo_inputs.push_back(duplicate_input.pseudo_inputs.front());
  duplicate_input.outputs.push_back(balance.outputs.front());
  duplicate_input.range_proofs.clear();
  duplicate_input.range_proofs.push_back(rct::bulletproof_plus_PROVE(
    std::vector<uint64_t>{10, 10}, rct::keyV{pseudo_mask, pseudo_mask}));
  EXPECT_FALSE(cryptonote::assets::verify_confidential_asset_transaction_with_ownership(
    {duplicate_input}, {ownership, ownership}, {id}, boost::none,
    cryptonote::TESTNET, carrier, &error));
}
