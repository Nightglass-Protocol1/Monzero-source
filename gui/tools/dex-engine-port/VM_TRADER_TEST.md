# LTC/XMZ trader in the lab VM

The first VM fixture runs Bob (the Litecoin funder) in Debian and Alice (the
XMZ funder) on the host. Bob creates his own identity and signs/configures the
trade inside the VM. His encrypted identity, passphrase and TLS private key
stay there. The host receives his public card and quote signature only.

The baseline uses host test-only node and wallet RPC services. With the
VM-wallet flags below, Bob keeps both wallet types on the VM; test node RPCs
are still forwarded onto VM loopback by SSH. This verifies separate traders
and local custody, but not independent nodes or Internet rendezvous. The peer connection also
uses an SSH tunnel for the regtest dry run. No RPC listener is opened publicly.

Use only the `mz-dexlab` VM; shut it down after tests. Never use production
cookies, wallet RPCs, identities or funds with this fixture.

## Build the same Qt test target on Debian

VM packages: `build-essential cmake ninja-build pkg-config qtbase5-dev
libsodium-dev libboost-dev python3-nacl python3-requests`.

From the GUI checkout:

```
python3 tools/dex-engine-port/prepare_vm_fixture.py --out /path/to/new-source-bundle
```

Copy the source bundle to the VM. It contains the existing DEX source files,
a standalone CMake target derived from `src/CMakeLists.txt`, and a SHA-256
source manifest. On the VM:

```
cmake -S /path/to/source-bundle -B /path/to/source-bundle/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/source-bundle/build -j2
```

Copy the verified `native-ltc-journal` helper to the VM as an owner-executable
file. Build it for a generic CPU, including its native dependencies: the first
VM run found that the development host's helper required x86-64-v4. The driver
verifies its digest against the host helper and checks that it actually runs
before creating any test-chain funding. Use the same source revision for both Qt
test binaries. This standalone target is a test fixture, not a release GUI.

## Run

Add these flags to the existing `test_litecoin_live_swap.py` invocation:

```
--quote-kit --split-processes --kit-config --keep
--remote-host monzero-dexlab-vm
--remote-binary /home/mztest/monzero-dex-build-20261018/build/dex_tests
--remote-helper /home/mztest/monzero-dex-build-20261018/journal-helper
```

Start with `--stage regtest --mode success`, a 25-second LTC clock and a
20-second XMZ clock, then refund and interruption cases. Testnet uses the
existing explicit testnet ports/cookie and signed test control document.
The remote driver supports node funding or VM-local light-wallet funding
with the flags below. Do not pass peer-address override flags. Remote trade directories under
`~/monzero-dex-trades/` are retained for recovery and inspection.

Before calling a run accepted, inspect the final evidence report for exact
settlement amounts and full-chain checks. A successful SSH connection, source
build or offline transport test alone is not evidence of a completed swap.

## VM-owned native wallet services

`remote_native_wallets.py` provisions claim, observer and recipient wallet RPCs
inside the VM. Wallet files, login files, ring databases and generated wallet
passwords remain under that VM's private trade directory. Only ports, paths
and allowlisted recipient observations return to the driver. Digest requests
must reuse the challenge connection; the component uses a `requests.Session`
with environment proxies and redirects disabled.

The component has a separate real-node smoke test: create and reopen an empty
recipient wallet using VM-local credentials. The combined VM-native/light-wallet success run also passed on 18 October.
Driver integration is available with:

```
--remote-wallet-rpc /home/mztest/monzero-dex-build-20261018/monzero-wallet-rpc
```

This starts Bob's native claim/observer/payout wallets on the VM and generates
his payout address there. The daemon and Litecoin funding still use host test
nodes. Funded success acceptance passed. The success report must show
`remoteNativeWallets`, exact settlement accounting, and `cleanupVerified`.
A failed cleanup produces a failure report even if settlement completed.

Wallet services register their PID and start time durably before executing.
Cleanup locks the role directory and cancels a delayed launch when its record
is absent; it checks process identity before signalling. Cleanup attempts all
services even if one SSH operation fails.


## VM-owned Litecoin light wallet

Add `--light-helper /host/path/to/verified-portable-light-helper`,
`--remote-light-helper /home/mztest/monzero-dex-build-20261018/light-helper`
and `--light-wallet-tls /host/path/to/fresh-test-tls-fixture`. Combine with
`--remote-wallet-rpc` to keep both Bob wallet types in the VM.

The helper is verified against the host reference and checked for CPU/loader
compatibility before chain setup. Only the public root certificate is copied.
The password, seed vault and launcher are created inside the VM. The host
receives public setup fields and filtered balances. An empty-wallet sync and
balance check validates the Electrum route before the host fixture funds the
new address. Signing and broadcast still require separate app approvals.

The test TLS server certificate must be current and cover `localhost`.
The standalone empty-wallet smoke and full light-funded VM settlement pass. Node RPC and Electrum infrastructure remain host-backed test services.
Test workspaces are retained for recovery, including the encrypted vault.

## Verified cases and remaining acceptance

Evidence is under `core/build/` in the parent project:

| Case | Evidence | Result |
| --- | --- | --- |
| VM light/native wallets, full success | `codex-vm-light-regtest-success-a2-20261018.json` | Passed; exact amounts/fees, cleanup, unchanged sources |
| VM Bob killed after signing, reopened, full success | `codex-vm-light-restart-signed-20261018.json` | Passed; same signed transaction, wallet/metadata hashes, fresh approval, Alice remains running |
| VM wallets, refund | `codex-vm-light-regtest-refund-20261018.json` | Passed; both refunds, exact final light-wallet balance, cleanup, unchanged sources |
| Kill during accepted broadcast, reply lost | `codex-vm-light-restart-accepted-20261018.json` | Passed through settlement; one accepted broadcast, retained state unchanged, no retransmission |
| Power loss and active-trade backup restore | Pending | App-process restart does not cover these |
| VM-wallet public-testnet success/refund/recovery | Pending | Local fakechain/regtest results do not cover these |

For the signed-boundary case, add `--vm-bob-restart-stage signed` to the
combined VM-light/native invocation. The helper verifies Bob's process group
before killing it, reopens the saved app, moves its password inside the VM,
and checks retained funding and protected metadata. Alice must keep her PID
and reconnect. The prior approval must be absent and a fresh review is needed.
Neither successful run proves retransmission: the transaction was included
before retry completed (`lightRetryVerified=false`). No replacement signing
is permitted to resolve an uncertain send.

Run only one funded fixture at a time. Keep source files unchanged until its
runner reaches a terminal state and verifies hashes. VM/headless management
must outlive the interactive terminal; inspect existing process handles before
restarting anything after a client interruption.

For interruption after node acceptance, use `--vm-bob-restart-stage accepted`.
The concurrent TLS peer records the accepted signed-byte digest and holds
exactly one reply. The VM helper verifies the app and its active funding
helper, snapshots durable state after acceptance, kills their owned group,
and releases the reply only after the kill. Reopening must preserve retained
funding and discard approval; the driver observes before reviewing any retry.
A barrier timeout leaves the outcome uncertain and does not prove no send.
Live acceptance of this case passed in 845 seconds. The VM run used a 45-second
LTC clock and a 40-block cancellation window to accommodate recovery latency;
both chains continue independently and every chain/expiry check remains enabled.
