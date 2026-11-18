#include "gtest/gtest.h"

#include <array>
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

#include <boost/uuid/uuid_io.hpp>

#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/difficulty.h"
#include "cryptonote_config.h"
#include "cryptonote_core/cryptonote_tx_utils.h"
#include "hardforks/hardforks.h"
#include "string_tools.h"

namespace
{
  struct network_vector
  {
    cryptonote::network_type type;
    const char* uuid;
    uint64_t standard_prefix;
    uint64_t integrated_prefix;
    uint64_t subaddress_prefix;
    uint16_t p2p_port;
    uint16_t rpc_port;
    uint16_t zmq_port;
    uint32_t genesis_nonce;
    const char* genesis_hash;
  };

  const std::array<network_vector, 3> networks{{
    {cryptonote::MAINNET, "94834264-d0b2-41dd-b0f2-0ada675c7710", 86, 87, 88,
      6174, 6175, 6176, 2271206363, "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4"},
    {cryptonote::TESTNET, "adc2a271-5539-43d9-b3c2-7aadaf6b938c", 111, 112, 113,
      16174, 16175, 16176, 818137481, "d89bd4d42729aa9f68fd8d6bc86c3b12686a3fcd5ec83a392844efc067d5c040"},
    {cryptonote::STAGENET, "b1dcac5a-9d11-4653-8122-e67eec8b59ac", 131, 132, 133,
      26174, 26175, 26176, 818137482, "0d917308bb74557f9ccefd1360b9875a39df00363474fe4da9ae8e5f989170b7"}
  }};
}

TEST(monzero_consensus, monetary_policy)
{
  EXPECT_EQ(11, CRYPTONOTE_DISPLAY_DECIMAL_POINT);
  EXPECT_EQ(UINT64_C(100000000000), COIN);
  EXPECT_EQ(UINT64_C(10000000000000000000), MONEY_SUPPLY);
  EXPECT_EQ(20, EMISSION_SPEED_FACTOR_PER_MINUTE);
  EXPECT_EQ(UINT64_C(163000000000), FINAL_SUBSIDY_PER_MINUTE);
  EXPECT_EQ(120, DIFFICULTY_TARGET_V1);
  EXPECT_EQ(120, DIFFICULTY_TARGET_V2);
  EXPECT_EQ(60, CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW);
  EXPECT_EQ(10, CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE);
  EXPECT_EQ(2, CURRENT_TRANSACTION_VERSION);
}

TEST(monzero_consensus, protected_network_vectors)
{
  for (const network_vector& expected : networks)
  {
    const cryptonote::config_t& actual = cryptonote::get_config(expected.type);
    EXPECT_EQ(expected.uuid, boost::uuids::to_string(actual.NETWORK_ID));
    EXPECT_EQ(expected.standard_prefix, actual.CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX);
    EXPECT_EQ(expected.integrated_prefix, actual.CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX);
    EXPECT_EQ(expected.subaddress_prefix, actual.CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX);
    EXPECT_EQ(expected.p2p_port, actual.P2P_DEFAULT_PORT);
    EXPECT_EQ(expected.rpc_port, actual.RPC_DEFAULT_PORT);
    EXPECT_EQ(expected.zmq_port, actual.ZMQ_RPC_DEFAULT_PORT);
    EXPECT_EQ(expected.genesis_nonce, actual.GENESIS_NONCE);

    cryptonote::block genesis;
    ASSERT_TRUE(cryptonote::generate_genesis_block(genesis, actual.GENESIS_TX, actual.GENESIS_NONCE));
    EXPECT_EQ(expected.genesis_hash,
      epee::string_tools::pod_to_hex(cryptonote::get_block_hash(genesis)));
  }
}

TEST(monzero_consensus, protected_hard_fork_schedule)
{
  const std::array<std::pair<const hardfork_t*, size_t>, 3> schedules{{
    {mainnet_hard_forks, num_mainnet_hard_forks},
    {testnet_hard_forks, num_testnet_hard_forks},
    {stagenet_hard_forks, num_stagenet_hard_forks}
  }};

  // v1.0.0: LWMA difficulty from version 17 at these heights.
  const std::array<uint64_t, 3> lwma_heights{{21000, 1345, 21000}};
  for (size_t network = 0; network < schedules.size(); ++network)
  {
    const auto& schedule = schedules[network];
    ASSERT_EQ(17, schedule.second);
    for (size_t index = 0; index < 16; ++index)
    {
      const uint8_t expected_version = static_cast<uint8_t>(index + 1);
      EXPECT_EQ(expected_version, schedule.first[index].version);
      EXPECT_EQ(index + 1, schedule.first[index].height);
      EXPECT_EQ(0, schedule.first[index].threshold);
    }
    EXPECT_EQ(HF_VERSION_LWMA_DIFFICULTY, schedule.first[16].version);
    EXPECT_EQ(lwma_heights[network], schedule.first[16].height);
    EXPECT_EQ(0, schedule.first[16].threshold);
    // The asset research fork must never be scheduled by accident.
    for (size_t index = 0; index < schedule.second; ++index)
      EXPECT_LT(schedule.first[index].version, HF_VERSION_MONZERO_ASSETS);
  }
}

namespace
{
  // Oldest-first window, as Blockchain collects it: block i has the given
  // solve time and difficulty; cumulative difficulty includes block i.
  struct window
  {
    std::vector<uint64_t> timestamps;
    std::vector<cryptonote::difficulty_type> cumulative;
    void add(uint64_t solve_time, cryptonote::difficulty_type difficulty)
    {
      timestamps.push_back((timestamps.empty() ? 1700000000 : timestamps.back()) + solve_time);
      cumulative.push_back((cumulative.empty() ? 0 : cumulative.back()) + difficulty);
      if (timestamps.size() > DIFFICULTY_BLOCKS_COUNT)
      {
        timestamps.erase(timestamps.begin());
        cumulative.erase(cumulative.begin());
      }
    }
  };

  // Blocks mined at a fixed hash rate take difficulty / hashrate seconds.
  // Returns how many blocks pass until difficulty is within 10% of the new
  // equilibrium after the hash rate changes from `before` to `after`.
  size_t blocks_to_settle(uint8_t version, uint64_t before, uint64_t after)
  {
    window w;
    cryptonote::difficulty_type d = before * DIFFICULTY_TARGET_V2;
    for (int i = 0; i < 800; ++i) { w.add(DIFFICULTY_TARGET_V2, d); }
    const double target = static_cast<double>(after * DIFFICULTY_TARGET_V2);
    for (size_t block = 1; block <= 5000; ++block)
    {
      d = cryptonote::next_difficulty_for_version(w.timestamps, w.cumulative, DIFFICULTY_TARGET_V2, version);
      const double value = d.convert_to<double>();
      if (value > 0.9 * target && value < 1.1 * target)
        return block;
      w.add(std::max<uint64_t>(1, (d / after).convert_to<uint64_t>()), d);
    }
    return 5000;
  }
}

TEST(monzero_consensus, lwma_holds_steady_at_constant_hash_rate)
{
  window w;
  for (int i = 0; i < 100; ++i) { w.add(DIFFICULTY_TARGET_V2, 1000000); }
  const auto d = cryptonote::next_difficulty_lwma(w.timestamps, w.cumulative, DIFFICULTY_TARGET_V2);
  EXPECT_EQ(990000, d);  // the average times 0.99
  // Only the newest 61 entries count: older blocks change nothing.
  window long_history = w;
  for (size_t i = 0; i < long_history.cumulative.size() - 61; ++i)
    long_history.timestamps[i] -= 100000;
  EXPECT_EQ(d, cryptonote::next_difficulty_lwma(long_history.timestamps, long_history.cumulative, DIFFICULTY_TARGET_V2));
}

TEST(monzero_consensus, lwma_follows_hash_rate_within_its_window)
{
  // Hash rate x100 (a big miner joins) and /100 (it leaves).
  const size_t lwma_up = blocks_to_settle(HF_VERSION_LWMA_DIFFICULTY, 1000, 100000);
  const size_t lwma_down = blocks_to_settle(HF_VERSION_LWMA_DIFFICULTY, 100000, 1000);
  const size_t old_up = blocks_to_settle(HF_VERSION_LWMA_DIFFICULTY - 1, 1000, 100000);
  const size_t old_down = blocks_to_settle(HF_VERSION_LWMA_DIFFICULTY - 1, 100000, 1000);
  std::cout << "blocks to settle (LWMA up/down, old up/down): " << lwma_up << "/" << lwma_down
    << ", " << old_up << "/" << old_down << std::endl;
  EXPECT_LE(lwma_up, 2 * DIFFICULTY_LWMA_WINDOW);
  // Down is slower by design: each solve time counts as at most 6 targets.
  EXPECT_LE(lwma_down, 3 * DIFFICULTY_LWMA_WINDOW);
  // The rule being replaced takes several times longer each way.
  EXPECT_GT(old_up, 4 * lwma_up);
  EXPECT_GT(old_down, 2 * lwma_down);
}

TEST(monzero_consensus, lwma_resists_bad_timestamps)
{
  window w;
  for (int i = 0; i < 100; ++i) { w.add(DIFFICULTY_TARGET_V2, 1000000); }
  const auto steady = cryptonote::next_difficulty_lwma(w.timestamps, w.cumulative, DIFFICULTY_TARGET_V2);
  // A block dated far in the past (out of order) counts as a 1-second solve.
  window backwards = w;
  backwards.timestamps.back() = backwards.timestamps.front();
  const auto after_backwards = cryptonote::next_difficulty_lwma(backwards.timestamps, backwards.cumulative, DIFFICULTY_TARGET_V2);
  EXPECT_GT(after_backwards, steady);
  EXPECT_LT(after_backwards, steady * 11 / 10);
  // A block dated far ahead counts as at most 6 targets.
  window ahead = w;
  ahead.timestamps.back() += 1000000;
  const auto after_ahead = cryptonote::next_difficulty_lwma(ahead.timestamps, ahead.cumulative, DIFFICULTY_TARGET_V2);
  EXPECT_LT(after_ahead, steady);
  EXPECT_GT(after_ahead, steady * 8 / 10);
  // Degenerate windows never divide by zero or return zero.
  EXPECT_EQ(1, cryptonote::next_difficulty_lwma({}, {}, DIFFICULTY_TARGET_V2));
  EXPECT_EQ(1, cryptonote::next_difficulty_lwma({5}, {7}, DIFFICULTY_TARGET_V2));
  window instant;
  for (int i = 0; i < 100; ++i) { instant.add(0, 1000000); }
  EXPECT_GT(cryptonote::next_difficulty_lwma(instant.timestamps, instant.cumulative, DIFFICULTY_TARGET_V2), 0);
}

TEST(monzero_consensus, difficulty_rule_switches_exactly_at_version_17)
{
  window w;
  for (int i = 0; i < 800; ++i) { w.add(i < 700 ? DIFFICULTY_TARGET_V2 : 30, 1000000); }
  EXPECT_EQ(cryptonote::next_difficulty(w.timestamps, w.cumulative, DIFFICULTY_TARGET_V2),
    cryptonote::next_difficulty_for_version(w.timestamps, w.cumulative, DIFFICULTY_TARGET_V2, HF_VERSION_LWMA_DIFFICULTY - 1));
  EXPECT_EQ(cryptonote::next_difficulty_lwma(w.timestamps, w.cumulative, DIFFICULTY_TARGET_V2),
    cryptonote::next_difficulty_for_version(w.timestamps, w.cumulative, DIFFICULTY_TARGET_V2, HF_VERSION_LWMA_DIFFICULTY));
  EXPECT_EQ(360, CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT_V2);
  EXPECT_EQ(60 * 60 * 2, CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT);
}
