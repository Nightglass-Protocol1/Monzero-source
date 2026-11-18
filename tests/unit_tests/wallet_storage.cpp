// Copyright (c) 2023, The Monero Project
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

#include "unit_tests_utils.h"
#include "gtest/gtest.h"

#include "file_io_utils.h"
#include "wallet/wallet2.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "common/util.h"

using namespace boost::filesystem;
using namespace epee::file_io_utils;

static constexpr const char WALLET_PRERELEASE_PRIMARY_ADDRESS[] =
    "FR9UZX754kKBkyjL1TQEsBBNLNmv7eJMv4nFsjCEs8sXT2hGE1BisLiAuT6Epc5REweUtata45REdNHfsmHM8vP6GB3YWsu";

TEST(wallet_storage, store_to_file2file)
{
    const path source_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease";
    const path interm_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease_copy_file2file";
    const path target_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease_new_file2file";

    ASSERT_TRUE(is_file_exist(source_wallet_file.string() + ".keys"));

    // Only the pre-release keys file is bundled; the cache is rebuilt.
    if (is_file_exist(interm_wallet_file.string()))
        remove(interm_wallet_file);
    tools::copy_file(source_wallet_file.string() + ".keys", interm_wallet_file.string() + ".keys");

    ASSERT_FALSE(is_file_exist(interm_wallet_file.string()));
    ASSERT_TRUE(is_file_exist(interm_wallet_file.string() + ".keys"));

    if (is_file_exist(target_wallet_file.string()))
        remove(target_wallet_file);
    if (is_file_exist(target_wallet_file.string() + ".keys"))
        remove(target_wallet_file.string() + ".keys");
    ASSERT_FALSE(is_file_exist(target_wallet_file.string()));
    ASSERT_FALSE(is_file_exist(target_wallet_file.string() + ".keys"));

    epee::wipeable_string password("beepbeep");

    const auto files_are_expected = [&]()
    {
        EXPECT_FALSE(is_file_exist(interm_wallet_file.string()));
        EXPECT_FALSE(is_file_exist(interm_wallet_file.string() + ".keys"));
        EXPECT_TRUE(is_file_exist(target_wallet_file.string()));
        EXPECT_TRUE(is_file_exist(target_wallet_file.string() + ".keys"));
    };

    {
        tools::wallet2 w;
        w.load(interm_wallet_file.string(), password);
        w.store();
        const std::string primary_address = w.get_address_as_str();
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, primary_address);
        w.store_to(target_wallet_file.string(), password);
        files_are_expected();
    }

    files_are_expected();

    {
        tools::wallet2 w;
        w.load(target_wallet_file.string(), password);
        const std::string primary_address = w.get_address_as_str();
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, primary_address);
        w.store_to("", "");
        files_are_expected();
    }

    files_are_expected();
}

TEST(wallet_storage, store_to_mem2file)
{
    const path target_wallet_file = unit_test::data_dir / "wallet_mem2file";

    if (is_file_exist(target_wallet_file.string()))
        remove(target_wallet_file);
    if (is_file_exist(target_wallet_file.string() + ".keys"))
        remove(target_wallet_file.string() + ".keys");
    ASSERT_FALSE(is_file_exist(target_wallet_file.string()));
    ASSERT_FALSE(is_file_exist(target_wallet_file.string() + ".keys"));

    epee::wipeable_string password("beepbeep2");

    {
        tools::wallet2 w;
        w.generate("", password);
        w.store_to(target_wallet_file.string(), password);

        EXPECT_TRUE(is_file_exist(target_wallet_file.string()));
        EXPECT_TRUE(is_file_exist(target_wallet_file.string() + ".keys"));
    }

    EXPECT_TRUE(is_file_exist(target_wallet_file.string()));
    EXPECT_TRUE(is_file_exist(target_wallet_file.string() + ".keys"));

    {
        tools::wallet2 w;
        w.load(target_wallet_file.string(), password);

        EXPECT_TRUE(is_file_exist(target_wallet_file.string()));
        EXPECT_TRUE(is_file_exist(target_wallet_file.string() + ".keys"));
    }

    EXPECT_TRUE(is_file_exist(target_wallet_file.string()));
    EXPECT_TRUE(is_file_exist(target_wallet_file.string() + ".keys"));
}

TEST(wallet_storage, change_password_same_file)
{
    const path source_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease";
    const path interm_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease_copy_change_password_same";

    ASSERT_TRUE(is_file_exist(source_wallet_file.string() + ".keys"));

    // Only the pre-release keys file is bundled; the cache is rebuilt.
    if (is_file_exist(interm_wallet_file.string()))
        remove(interm_wallet_file);
    tools::copy_file(source_wallet_file.string() + ".keys", interm_wallet_file.string() + ".keys");

    ASSERT_FALSE(is_file_exist(interm_wallet_file.string()));
    ASSERT_TRUE(is_file_exist(interm_wallet_file.string() + ".keys"));

    epee::wipeable_string old_password("beepbeep");
    epee::wipeable_string new_password("meepmeep");

    {
        tools::wallet2 w;
        w.load(interm_wallet_file.string(), old_password);
        w.store();
        const std::string primary_address = w.get_address_as_str();
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, primary_address);
        w.change_password(w.get_wallet_file(), old_password, new_password);
    }

    {
        tools::wallet2 w;
        w.load(interm_wallet_file.string(), new_password);
        const std::string primary_address = w.get_address_as_str();
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, primary_address);
    }

    {
        tools::wallet2 w;
        EXPECT_THROW(w.load(interm_wallet_file.string(), old_password), tools::error::invalid_password);
    }
}

TEST(wallet_storage, change_password_different_file)
{
    const path source_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease";
    const path interm_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease_copy_change_password_diff";
    const path target_wallet_file = unit_test::data_dir / "wallet_monzero_prerelease_new_change_password_diff";

    ASSERT_TRUE(is_file_exist(source_wallet_file.string() + ".keys"));

    // Only the pre-release keys file is bundled; the cache is rebuilt.
    if (is_file_exist(interm_wallet_file.string()))
        remove(interm_wallet_file);
    tools::copy_file(source_wallet_file.string() + ".keys", interm_wallet_file.string() + ".keys");

    ASSERT_FALSE(is_file_exist(interm_wallet_file.string()));
    ASSERT_TRUE(is_file_exist(interm_wallet_file.string() + ".keys"));

    if (is_file_exist(target_wallet_file.string()))
        remove(target_wallet_file);
    if (is_file_exist(target_wallet_file.string() + ".keys"))
        remove(target_wallet_file.string() + ".keys");
    ASSERT_FALSE(is_file_exist(target_wallet_file.string()));
    ASSERT_FALSE(is_file_exist(target_wallet_file.string() + ".keys"));

    epee::wipeable_string old_password("beepbeep");
    epee::wipeable_string new_password("meepmeep");

    {
        tools::wallet2 w;
        w.load(interm_wallet_file.string(), old_password);
        w.store();
        const std::string primary_address = w.get_address_as_str();
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, primary_address);
        w.change_password(target_wallet_file.string(), old_password, new_password);
    }

    EXPECT_FALSE(is_file_exist(interm_wallet_file.string()));
    EXPECT_FALSE(is_file_exist(interm_wallet_file.string() + ".keys"));
    EXPECT_TRUE(is_file_exist(target_wallet_file.string()));
    EXPECT_TRUE(is_file_exist(target_wallet_file.string() + ".keys"));

    {
        tools::wallet2 w;
        w.load(target_wallet_file.string(), new_password);
        const std::string primary_address = w.get_address_as_str();
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, primary_address);
    }
}

TEST(wallet_storage, change_password_in_memory)
{
    const epee::wipeable_string password1("monero");
    const epee::wipeable_string password2("means money");
    const epee::wipeable_string password_wrong("is traceable");

    tools::wallet2 w;
    w.generate("", password1);
    const std::string primary_address_1 = w.get_address_as_str();
    w.change_password("", password1, password2);
    const std::string primary_address_2 = w.get_address_as_str();
    EXPECT_EQ(primary_address_1, primary_address_2);

    EXPECT_THROW(w.change_password("", password_wrong, password1), tools::error::invalid_password);
}

TEST(wallet_storage, change_password_mem2file)
{
    const path target_wallet_file = unit_test::data_dir / "wallet_change_password_mem2file";

    if (is_file_exist(target_wallet_file.string()))
        remove(target_wallet_file);
    if (is_file_exist(target_wallet_file.string() + ".keys"))
        remove(target_wallet_file.string() + ".keys");
    ASSERT_FALSE(is_file_exist(target_wallet_file.string()));
    ASSERT_FALSE(is_file_exist(target_wallet_file.string() + ".keys"));

    const epee::wipeable_string password1("https://safecurves.cr.yp.to/rigid.html");
    const epee::wipeable_string password2(
        "https://csrc.nist.gov/csrc/media/projects/crypto-standards-development-process/documents/dualec_in_x982_and_sp800-90.pdf");
    
    std::string primary_address_1, primary_address_2;
    {
        tools::wallet2 w;
        w.generate("", password1);
        primary_address_1 = w.get_address_as_str();
        w.change_password(target_wallet_file.string(), password1, password2);
    }

    EXPECT_TRUE(is_file_exist(target_wallet_file.string()));
    EXPECT_TRUE(is_file_exist(target_wallet_file.string() + ".keys"));

    {
        tools::wallet2 w;
        w.load(target_wallet_file.string(), password2);
        primary_address_2 = w.get_address_as_str();
    }

    EXPECT_EQ(primary_address_1, primary_address_2);
}

namespace
{
    // Copies a bundled keys file to a scratch name so a test may load it
    path scratch_keys_copy(const char *fixture, const char *scratch)
    {
        const path source = unit_test::data_dir / fixture;
        const path target = unit_test::data_dir / scratch;
        if (is_file_exist(target.string()))
            remove(target);
        if (is_file_exist(target.string() + ".keys"))
            remove(target.string() + ".keys");
        tools::copy_file(source.string() + ".keys", target.string() + ".keys");
        return target;
    }

    std::string keys_file_contents(const path &wallet_file)
    {
        std::string contents;
        EXPECT_TRUE(load_file_to_string(wallet_file.string() + ".keys", contents));
        return contents;
    }
}

TEST(wallet_origin, rejects_monero_wallet_keys)
{
    const path wallet_file = scratch_keys_copy("wallet_00fd416a", "wallet_origin_monero");
    const std::string before = keys_file_contents(wallet_file);

    tools::wallet2 w;
    EXPECT_THROW(w.load(wallet_file.string(), "beepbeep"), tools::error::not_monzero_wallet);
    EXPECT_EQ(before, keys_file_contents(wallet_file));
}

TEST(wallet_origin, rejects_monero_testnet_wallet_keys)
{
    const path wallet_file = scratch_keys_copy("wallet_9svHk1", "wallet_origin_monero_testnet");

    tools::wallet2 w(cryptonote::TESTNET);
    EXPECT_THROW(w.load(wallet_file.string(), "test"), tools::error::not_monzero_wallet);
}

TEST(wallet_origin, wrong_password_is_still_reported_as_such)
{
    const path wallet_file = scratch_keys_copy("wallet_00fd416a", "wallet_origin_monero_wrong_password");

    tools::wallet2 w;
    EXPECT_THROW(w.load(wallet_file.string(), "not the password"), tools::error::invalid_password);
}

TEST(wallet_origin, upgrades_prerelease_monzero_wallet)
{
    const path wallet_file = scratch_keys_copy("wallet_monzero_prerelease", "wallet_origin_prerelease");
    const std::string original = keys_file_contents(wallet_file);

    {
        tools::wallet2 w;
        w.load(wallet_file.string(), "beepbeep");
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, w.get_address_as_str());
    }
    const std::string upgraded = keys_file_contents(wallet_file);
    EXPECT_NE(original, upgraded);

    {
        tools::wallet2 w;
        w.load(wallet_file.string(), "beepbeep");
        EXPECT_EQ(WALLET_PRERELEASE_PRIMARY_ADDRESS, w.get_address_as_str());
    }
    // The marker was stored, so a second load has nothing to upgrade
    EXPECT_EQ(upgraded, keys_file_contents(wallet_file));
}

TEST(wallet_origin, resets_prerelease_display_unit_to_xmz)
{
    const path wallet_file = scratch_keys_copy("wallet_monzero_prerelease_dp8", "wallet_origin_prerelease_dp8");
    const std::string original = keys_file_contents(wallet_file);

    tools::wallet2 w;
    w.load(wallet_file.string(), "beepbeep");
    EXPECT_EQ(CRYPTONOTE_DISPLAY_DECIMAL_POINT, cryptonote::get_default_decimal_point());
    EXPECT_EQ("XMZ", cryptonote::get_unit());
    EXPECT_NE(original, keys_file_contents(wallet_file));
}

TEST(wallet_origin, rejects_atomic_unit_without_marker)
{
    // 0 decimals was also a Monero unit, so an unmarked file using it is not
    // provably a Monzero wallet.
    const path wallet_file = scratch_keys_copy("wallet_monzero_prerelease_dp0", "wallet_origin_prerelease_dp0");

    tools::wallet2 w;
    EXPECT_THROW(w.load(wallet_file.string(), "beepbeep"), tools::error::not_monzero_wallet);
}

TEST(wallet_origin, new_wallets_are_marked)
{
    const path wallet_file = unit_test::data_dir / "wallet_origin_new";
    if (is_file_exist(wallet_file.string()))
        remove(wallet_file);
    if (is_file_exist(wallet_file.string() + ".keys"))
        remove(wallet_file.string() + ".keys");

    std::string address;
    {
        tools::wallet2 w;
        w.generate(wallet_file.string(), "beepbeep");
        address = w.get_address_as_str();
    }
    const std::string stored = keys_file_contents(wallet_file);

    tools::wallet2 w;
    w.load(wallet_file.string(), "beepbeep");
    EXPECT_EQ(address, w.get_address_as_str());
    EXPECT_EQ(stored, keys_file_contents(wallet_file));
}
