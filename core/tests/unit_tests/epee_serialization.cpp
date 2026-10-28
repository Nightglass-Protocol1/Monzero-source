// Copyright (c) 2020-2022, The Monero Project

//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "serialization/keyvalue_serialization.h"
#include "storages/portable_storage.h"
#include "storages/portable_storage_template_helper.h"
#include "span.h"
#include "wallet/wallet_rpc_server_commands_defs.h"

TEST(epee_binary, two_keys)
{
  static constexpr const std::uint8_t data[] = {
    0x01, 0x11, 0x01, 0x1, 0x01, 0x01, 0x02, 0x1, 0x1, 0x08, 0x01, 'a',
    0x0B, 0x00, 0x01, 'b', 0x0B, 0x00
  };

  epee::serialization::portable_storage storage{};
  EXPECT_TRUE(storage.load_from_binary(data));
}

TEST(epee_binary, duplicate_key)
{
  static constexpr const std::uint8_t data[] = {
    0x01, 0x11, 0x01, 0x1, 0x01, 0x01, 0x02, 0x1, 0x1, 0x08, 0x01, 'a',
    0x0B, 0x00, 0x01, 'a', 0x0B, 0x00
  };

  epee::serialization::portable_storage storage{};
  EXPECT_FALSE(storage.load_from_binary(data));
}

namespace
{

template<typename t_param>
struct ParentObjWithOptChild
{
  t_param     params;

  ParentObjWithOptChild(): params{} {}

  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(params)
  END_KV_SERIALIZE_MAP()
};

struct ObjWithOptChild
{
  bool test_value;

  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE_OPT(test_value, true);
  END_KV_SERIALIZE_MAP()
};
}

TEST(epee_binary, serialize_deserialize)
{
  ParentObjWithOptChild<ObjWithOptChild> o;
  std::string o_json;
  o.params.test_value = true;

  EXPECT_TRUE(epee::serialization::store_t_to_json(o, o_json));
  EXPECT_TRUE(o.params.test_value);

  EXPECT_TRUE(epee::serialization::load_t_from_json(o, o_json));
  EXPECT_TRUE(o.params.test_value);

  ParentObjWithOptChild<ObjWithOptChild> o2;
  std::string o2_json;
  o.params.test_value = false;

  EXPECT_TRUE(epee::serialization::store_t_to_json(o2, o2_json));
  EXPECT_FALSE(o2.params.test_value);

  EXPECT_TRUE(epee::serialization::load_t_from_json(o2, o2_json));
  EXPECT_FALSE(o2.params.test_value);

  // compiler sets default value of test_value to false
  ParentObjWithOptChild<ObjWithOptChild> o3;
  std::string o3_json;

  EXPECT_TRUE(epee::serialization::store_t_to_json(o3, o3_json));
  EXPECT_FALSE(o3.params.test_value);

  EXPECT_TRUE(epee::serialization::load_t_from_json(o3, o3_json));
  EXPECT_FALSE(o3.params.test_value);

  // test optional field default initialization.
  ParentObjWithOptChild<ObjWithOptChild> o4;
  std::string o4_json = "{\"params\": {}}";

  EXPECT_TRUE(epee::serialization::load_t_from_json(o4, o4_json));
  EXPECT_TRUE(o4.params.test_value);
}

TEST(epee_binary, wallet_asset_rpc_requests_round_trip_and_default)
{
  tools::wallet_rpc::COMMAND_RPC_CREATE_ASSET::request create;
  create.address = "asset-recipient";
  create.asset_type = "nft";
  create.atomic_supply = 1;
  create.decimals = 0;
  create.metadata_hash = std::string(64, '1');
  create.metadata_reference = "ipfs://metadata";
  create.collection_id = std::string(64, '2');
  create.account_index = 3;
  create.subaddr_indices = {4, 5};
  create.priority = 2;
  create.ring_size = 16;
  create.do_not_relay = true;
  create.get_tx_hex = true;
  create.get_tx_metadata = true;
  std::string json;
  ASSERT_TRUE(epee::serialization::store_t_to_json(create, json));
  tools::wallet_rpc::COMMAND_RPC_CREATE_ASSET::request decoded_create;
  ASSERT_TRUE(epee::serialization::load_t_from_json(decoded_create, json));
  EXPECT_EQ(create.address, decoded_create.address);
  EXPECT_EQ(create.asset_type, decoded_create.asset_type);
  EXPECT_EQ(create.atomic_supply, decoded_create.atomic_supply);
  EXPECT_EQ(create.metadata_hash, decoded_create.metadata_hash);
  EXPECT_EQ(create.subaddr_indices, decoded_create.subaddr_indices);
  EXPECT_TRUE(decoded_create.do_not_relay);

  tools::wallet_rpc::COMMAND_RPC_TRANSFER_ASSET::request transfer;
  ASSERT_TRUE(epee::serialization::load_t_from_json(transfer,
    "{\"asset_id\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}"));
  EXPECT_EQ(0u, transfer.amount);
  EXPECT_EQ(0u, transfer.burn_amount);
  EXPECT_EQ(0u, transfer.account_index);
  EXPECT_TRUE(transfer.subaddr_indices.empty());
  EXPECT_FALSE(transfer.do_not_relay);

  tools::wallet_rpc::COMMAND_RPC_GET_ASSETS::request holdings;
  ASSERT_TRUE(epee::serialization::load_t_from_json(holdings, "{}"));
  EXPECT_TRUE(holdings.asset_id.empty());
  EXPECT_FALSE(holdings.include_spent);
}
