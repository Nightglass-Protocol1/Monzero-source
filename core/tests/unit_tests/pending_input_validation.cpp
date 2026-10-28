// Copyright (c) 2026, The Monzero Project
// SPDX-License-Identifier: BSD-3-Clause
#include "gtest/gtest.h"
#include "wallet/pending_input_validation.h"

namespace
{
  struct Output
  {
    crypto::key_image m_key_image{};
    bool m_key_image_known = true;
    bool m_key_image_partial = false;
  };
  struct Fixture
  {
    std::vector<Output> outputs{2};
    cryptonote::transaction_prefix tx;
    Fixture()
    {
      for (size_t i = 0; i < outputs.size(); ++i)
      {
        outputs[i].m_key_image.data[0] = char(i + 1);
        cryptonote::txin_to_key input{};
        input.k_image = outputs[i].m_key_image;
        tx.vin.push_back(input);
      }
    }
    bool check(std::vector<size_t> selected = {0, 1}) const
    {
      return tools::pending_inputs_match_wallet(tx, selected, outputs.size(),
          [this](size_t idx) -> const Output& { return outputs.at(idx); });
    }
  };
}

TEST(PendingInputValidation, AcceptsExactInputSetRegardlessOfOrdering)
{
  Fixture f;
  EXPECT_TRUE(f.check());
  EXPECT_TRUE(f.check({1, 0}));
}

TEST(PendingInputValidation, RejectsInvalidOrDuplicateIndicesBeforeLookup)
{
  Fixture f;
  EXPECT_FALSE(f.check({}));
  EXPECT_FALSE(f.check({0}));
  EXPECT_FALSE(f.check({0, 0}));
  EXPECT_FALSE(f.check({0, 2}));
  EXPECT_FALSE(f.check({0, size_t(-1)}));
}

TEST(PendingInputValidation, RejectsStaleUnknownOrPartialKeyImages)
{
  Fixture f;
  f.outputs[0].m_key_image.data[0] = 3;
  EXPECT_FALSE(f.check());
  f = Fixture{};
  f.outputs[0].m_key_image_known = false;
  EXPECT_FALSE(f.check());
  f = Fixture{};
  f.outputs[0].m_key_image_partial = true;
  EXPECT_FALSE(f.check());
  f = Fixture{};
  f.outputs[0].m_key_image = {};
  EXPECT_FALSE(f.check());
  f = Fixture{};
  f.outputs[0].m_key_image = f.outputs[1].m_key_image;
  EXPECT_FALSE(f.check());
}

TEST(PendingInputValidation, RejectsWrongDuplicateOrUnsupportedTransactionInputs)
{
  Fixture f;
  f.tx.vin[0] = f.tx.vin[1];
  EXPECT_FALSE(f.check());
  f = Fixture{};
  f.tx.vin[0] = cryptonote::txin_gen{};
  EXPECT_FALSE(f.check());
  f = Fixture{};
  boost::get<cryptonote::txin_to_key>(f.tx.vin[0]).k_image.data[0] = 3;
  EXPECT_FALSE(f.check());
}
