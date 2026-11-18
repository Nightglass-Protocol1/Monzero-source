// Copyright (c) 2026, The Monzero Project

#include "include_base_utils.h"
#include "cryptonote_basic/asset_wire.h"
#include "cryptonote_basic/blobdatatype.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "fuzzer.h"

BEGIN_INIT_SIMPLE_FUZZER()
END_INIT_SIMPLE_FUZZER()

BEGIN_SIMPLE_FUZZER()
  if (len == 0)
    return 0;

  std::string error;
  if ((buf[0] & 1) == 0)
  {
    cryptonote::assets::asset_transaction_payload payload;
    const std::vector<uint8_t> encoded(buf + 1, buf + len);
    if (cryptonote::assets::decode_asset_transaction_payload(encoded, payload, &error))
    {
      std::vector<uint8_t> round_trip;
      cryptonote::assets::encode_asset_transaction_payload(payload, round_trip, nullptr);
    }
  }
  else
  {
    cryptonote::transaction tx = AUTO_VAL_INIT(tx);
    if (parse_and_validate_tx_from_blob(
          std::string(reinterpret_cast<const char *>(buf + 1), len - 1), tx))
    {
      boost::optional<cryptonote::assets::asset_transaction_payload> payload;
      cryptonote::assets::parse_native_asset_transaction(tx,
        HF_VERSION_MONZERO_ASSETS, cryptonote::MAINNET, payload, &error);
    }
  }
END_SIMPLE_FUZZER()
