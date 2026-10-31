// Read-only signature verifier. Never opens a wallet or connects to a node.
#include <iostream>
#include "wallet/wallet2.h"
#include "rapidjson/document.h"
int main() {
  try {
    std::string input;
    for (char c; std::cin.get(c);) { if (input.size() >= 8192) return 2; input += c; }
    rapidjson::Document d; d.Parse(input.c_str());
    if (!d.IsObject()) return 2;
    for (auto k : {"address", "message", "signature"})
      if (!d.HasMember(k) || !d[k].IsString()) return 2;
    cryptonote::address_parse_info a;
    if (!cryptonote::get_account_address_from_str(a, cryptonote::MAINNET, d["address"].GetString()) || a.is_subaddress || a.has_payment_id) return 2;
    tools::wallet2 verifier(cryptonote::MAINNET);
    const auto result = verifier.verify(d["message"].GetString(), a.address, d["signature"].GetString());
    const bool good = result.valid && result.version == 2 && result.type == tools::wallet2::sign_with_spend_key;
    std::cout << (good ? "true\n" : "false\n");
    return good ? 0 : 1;
  } catch (...) { return 2; }
}
