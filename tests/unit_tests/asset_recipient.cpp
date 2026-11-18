#include "gtest/gtest.h"

#include <cstring>

#include "cryptonote_basic/account.h"
#include "cryptonote_basic/asset_recipient.h"
#include "cryptonote_basic/subaddress_index.h"
#include "ringct/rctOps.h"

namespace
{
  cryptonote::account_base make_account()
  {
    cryptonote::account_base account;
    account.generate();
    return account;
  }
}

TEST(asset_recipient, view_wallet_discovers_and_decodes_output)
{
  const cryptonote::account_base account = make_account();
  const auto& keys = account.get_keys();
  crypto::public_key tx_public{};
  crypto::secret_key tx_secret{};
  crypto::generate_keys(tx_public, tx_secret);

  cryptonote::assets::asset_recipient_data recipient{};
  rct::key commitment{};
  std::string error;
  ASSERT_TRUE(cryptonote::assets::make_asset_recipient_data(
    keys.m_account_address, false, tx_secret, 7, 123456789, recipient, commitment,
    &error)) << error;
  ASSERT_EQ(tx_public, recipient.tx_public_key);

  cryptonote::assets::decoded_asset_recipient decoded{};
  ASSERT_TRUE(cryptonote::assets::decode_asset_recipient_data(recipient,
    keys.m_account_address.m_spend_public_key, keys.m_view_secret_key, 7,
    commitment, decoded, &error)) << error;
  EXPECT_EQ(123456789u, decoded.amount);
  EXPECT_EQ(commitment, rct::commit(decoded.amount, decoded.mask));
}

TEST(asset_recipient, rejects_wrong_wallet_index_and_commitment)
{
  const cryptonote::account_base owner = make_account();
  const cryptonote::account_base stranger = make_account();
  crypto::public_key tx_public{};
  crypto::secret_key tx_secret{};
  crypto::generate_keys(tx_public, tx_secret);
  cryptonote::assets::asset_recipient_data recipient{};
  rct::key commitment{};
  std::string error;
  ASSERT_TRUE(cryptonote::assets::make_asset_recipient_data(
    owner.get_keys().m_account_address, false, tx_secret, 3, 42, recipient,
    commitment, &error)) << error;

  cryptonote::assets::decoded_asset_recipient decoded{};
  EXPECT_FALSE(cryptonote::assets::decode_asset_recipient_data(recipient,
    stranger.get_keys().m_account_address.m_spend_public_key,
    stranger.get_keys().m_view_secret_key, 3, commitment, decoded, &error));
  EXPECT_FALSE(cryptonote::assets::decode_asset_recipient_data(recipient,
    owner.get_keys().m_account_address.m_spend_public_key,
    owner.get_keys().m_view_secret_key, 4, commitment, decoded, &error));
  rct::key wrong_commitment = commitment;
  wrong_commitment.bytes[0] ^= 1;
  EXPECT_FALSE(cryptonote::assets::decode_asset_recipient_data(recipient,
    owner.get_keys().m_account_address.m_spend_public_key,
    owner.get_keys().m_view_secret_key, 3, wrong_commitment, decoded, &error));
}

TEST(asset_recipient, view_wallet_discovers_subaddress_output)
{
  const cryptonote::account_base account = make_account();
  const auto& keys = account.get_keys();
  const cryptonote::subaddress_index index{0, 1};
  const cryptonote::account_public_address subaddress =
    keys.get_device().get_subaddress(keys, index);
  crypto::public_key tx_public{};
  crypto::secret_key tx_secret{};
  crypto::generate_keys(tx_public, tx_secret);

  cryptonote::assets::asset_recipient_data recipient{};
  rct::key commitment{};
  std::string error;
  ASSERT_TRUE(cryptonote::assets::make_asset_recipient_data(subaddress, true,
    tx_secret, 2, 77, recipient, commitment, &error)) << error;
  EXPECT_NE(tx_public, recipient.tx_public_key);

  cryptonote::assets::decoded_asset_recipient decoded{};
  ASSERT_TRUE(cryptonote::assets::decode_asset_recipient_data(recipient,
    subaddress.m_spend_public_key, keys.m_view_secret_key, 2, commitment,
    decoded, &error)) << error;
  EXPECT_EQ(77u, decoded.amount);
}

TEST(asset_recipient, rejects_tampered_ciphertext_and_view_tag)
{
  const cryptonote::account_base owner = make_account();
  crypto::public_key tx_public{};
  crypto::secret_key tx_secret{};
  crypto::generate_keys(tx_public, tx_secret);
  cryptonote::assets::asset_recipient_data recipient{};
  rct::key commitment{};
  std::string error;
  ASSERT_TRUE(cryptonote::assets::make_asset_recipient_data(
    owner.get_keys().m_account_address, false, tx_secret, 0, 9001, recipient,
    commitment, &error)) << error;

  cryptonote::assets::decoded_asset_recipient decoded{};
  recipient.encrypted_amount.amount.bytes[0] ^= 1;
  EXPECT_FALSE(cryptonote::assets::decode_asset_recipient_data(recipient,
    owner.get_keys().m_account_address.m_spend_public_key,
    owner.get_keys().m_view_secret_key, 0, commitment, decoded, &error));
  recipient.encrypted_amount.amount.bytes[0] ^= 1;
  recipient.view_tag.data ^= 1;
  EXPECT_FALSE(cryptonote::assets::decode_asset_recipient_data(recipient,
    owner.get_keys().m_account_address.m_spend_public_key,
    owner.get_keys().m_view_secret_key, 0, commitment, decoded, &error));
}

TEST(asset_recipient, preserves_builder_selected_commitment_mask)
{
  const cryptonote::account_base owner = make_account();
  crypto::public_key tx_public{};
  crypto::secret_key tx_secret{};
  crypto::generate_keys(tx_public, tx_secret);
  const rct::key selected_mask = rct::skGen();
  cryptonote::assets::asset_recipient_data recipient{};
  rct::key commitment{};
  std::string error;
  ASSERT_TRUE(cryptonote::assets::make_asset_recipient_data_with_mask(
    owner.get_keys().m_account_address, false, tx_secret, 5, 73,
    selected_mask, recipient, commitment, &error)) << error;

  cryptonote::assets::decoded_asset_recipient decoded{};
  ASSERT_TRUE(cryptonote::assets::decode_asset_recipient_data(recipient,
    owner.get_keys().m_account_address.m_spend_public_key,
    owner.get_keys().m_view_secret_key, 5, commitment, decoded, &error)) << error;
  EXPECT_EQ(73u, decoded.amount);
  EXPECT_EQ(selected_mask, decoded.mask);

  rct::key invalid_mask{};
  std::memset(invalid_mask.bytes, 0xff, sizeof(invalid_mask.bytes));
  EXPECT_FALSE(cryptonote::assets::make_asset_recipient_data_with_mask(
    owner.get_keys().m_account_address, false, tx_secret, 5, 73,
    invalid_mask, recipient, commitment, &error));
  EXPECT_EQ("asset output mask is not a reduced scalar", error);
}
