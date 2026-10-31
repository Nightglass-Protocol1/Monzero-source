# Monzero LTC/XMZ trade kit

Sets up one side of a two-party LTC/XMZ swap. Each party runs it on their own
machine; only public files are exchanged. Testnet only while live trading is
disabled.

Roles: the **LTC funder** pays Litecoin and receives XMZ; the **XMZ funder**
pays XMZ and receives Litecoin.

## Steps

1. **Each party creates an identity** (once per trade):

   ```
   python3 trade_kit.py identity --role ltc --dir my-trade      # or --role xmz
   ```

   This asks for a passphrase and writes, into `my-trade/` (keep it private):
   the encrypted identity key, a TLS key and certificate, and `card.json`.
   Send only `card.json` to the other party.

2. **One party proposes the terms** from both cards and a parameters file
   (amounts, payout and refund addresses, fees, confirmations, cancel window,
   expiry; see `test_litecoin_live_swap.py` for a complete example):

   ```
   python3 trade_kit.py terms --ltc-card ltc-card.json --xmz-card xmz-card.json \
       --params params.json --out terms.json
   ```

   Both parties read `terms.json` before signing.

3. **Each party signs** with their own identity:

   ```
   python3 trade_kit.py sign --dir my-trade --terms terms.json --out my.sig
   ```

   It shows the amounts and refuses if the terms name a different key for your
   role. Send `my.sig` to the other party.

4. **Either party assembles** the signed quote:

   ```
   python3 trade_kit.py assemble --terms terms.json --ltc-sig ltc.sig --xmz-sig xmz.sig \
       --out start-quote.json
   ```

   Both signatures are checked; the app checks them again when the trade starts.

5. **Each party writes their own app configuration**:

   ```
   python3 trade_kit.py configure --dir my-trade --quote start-quote.json \
       --peer-card their-card.json --policy policy.json --local local.json --out my-app
   ```

   `policy.json` is the agreed trade policy (same file for both). `local.json`
   describes only this machine's services:

   ```json
   {"journalHelper": "/path/to/native-ltc-journal",
    "litecoin": {"rpcPort": 19332, "wallet": "trade", "anchor": "tltc1q...", "cookieFile": "/path/.cookie"},
    "xmz": {"daemonPort": 28181, "claimWalletPort": 28190, "claimLoginFile": "...",
            "sourceWalletPort": 28191, "sourceLoginFile": "...", "sourceAddress": "...",
            "observerWalletPort": 28192, "observerLoginFile": "...", "walletPasswordFile": "..."},
    "peer": {"address": "192.168.0.27", "port": 41000}}
   ```

   The XMZ funder listens on `peer.address`; the LTC funder connects to it.
   The source wallet is only needed by the XMZ funder, the observer wallet only
   by the LTC funder.

   The XMZ funder may skip its Bitcoin/Litecoin node and use Electrum servers
   instead: replace the `litecoin` object with
   `{"light": {"servers": [{"host": "a.example", "port": 50002}, {"host": "b.example", "port": 50002}], "maximumTipAge": 5400}}`.
   Use 2–8 independently operated servers; every one must agree before the app
   releases anything, and the best header must be newer than `maximumTipAge`
   seconds (600–10800). See `tools/dex-engine-port/SELLER_LIGHT_REDEEM.md` for
   the rules and the remaining risk. The LTC funder still needs its node.

   The command verifies both signatures over the canonical
   quote terms and checks that the quote names both parties' keys before
   creating `my-app/app.json` and the files it points to.
   Start the app with `--dex-ltc-network testnet --dex-ltc-app my-app/app.json`.

## Format

`start-quote.json` is swap-machine's signed-quote envelope
(`monzero-ltc-signed-quote-envelope-v1`, terms schema
`monzero-native-ltc-xmz-quote-v2`), with one Ed25519 signature per role. The
identity file uses the app's Argon2id + XChaCha20-Poly1305 identity format.

## Verified

`test_litecoin_live_swap.py --quote-kit` builds both identities and the quote
with this kit (as separate runs of `trade_kit.py`), then completes a swap with
each trader in its own app process (`--split-processes`). With
`--kit-config`, each process is started only from the app folder `configure`
wrote for that side.

## Configuration publication

On Linux, `configure` builds a private sibling directory and syncs its files
and directories before atomically publishing it without replacing an existing
file, directory or symlink. The destination parent must already exist. Config
paths refer to the final directory; local identity files remain in place.
Unsupported platforms/filesystems fail closed instead of using an unsafe
check-then-rename fallback. Other platform support remains a release gate.

An interrupted attempt before publication leaves no partial app at the final
path. A process killed without cleanup may leave its private staging directory.
After publication, a parent-directory sync error is reported explicitly and the
complete destination is preserved. Inspect the exact reported paths; do not
delete other attempts' staging folders or overwrite an existing destination.
These checks establish publication integrity, not complete policy/service
validation or active-trade backup restoration.
