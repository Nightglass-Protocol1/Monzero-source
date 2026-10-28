# Isolated DEX lab preparation

Status: preparation and fixture tests only. No live adapters, funds verification,
atomic settlement or deployment are supplied by this kit.

## Tomorrow's VM

- Debian/Ubuntu server, 4 vCPU, 8 GB RAM (16 GB for builds), 100 GB disk.
- Dedicated unprivileged SSH account with key authentication.
- Management reachable only from your LAN; no public port forwarding.
- Separate lab from production nodes and wallets. Never import real wallet seeds.
- Take a clean VM snapshot before node installation. Snapshots are not a substitute
  for the DEX's future durable refund/recovery records.

Run `bash utils/dex-lab/preflight.sh` inside the VM. It changes nothing.
Run `python3 -m unittest discover -s utils/dex-lab -p 'test_*.py' -v`
on either machine to run the response validation fixtures without nodes.

## Deployment gates

1. Inspect available storage, bridge and VM IDs before creating any VM.
2. Verify chosen node binary versions and checksums before installing. Do not use
   arbitrary install scripts or copy production configuration/credentials.
3. Use dedicated directories for Bitcoin regtest and Monzero fakechain; validate
   the selected Monzero binary's isolation flags before launching it. Do not fall
   back to mainnet or publicly peered testnet when a flag is unsupported.
   Monzero must include `--keep-fakechain`; without it the daemon deliberately
   deletes the fakechain database at startup, invalidating recovery fixtures.
4. Bind RPC and P2P to loopback, disable external peers/discovery and automatic
   port mapping. Confirm listeners and outbound isolation before creating wallets.
5. Pin expected genesis hashes from the selected source/configuration independently
   of RPC responses. Checking a node against its own untrusted hash proves nothing.
6. Generate only disposable test wallets. Keep RPC secrets in local restricted
   files, never in Git, chat, shell arguments or logs.
7. Connect read-only RPC transport with strict response IDs, authentication,
   timeouts, size bounds and method allowlists. Feed results to `health.py`.
8. Obtain wallet-owned unlocked balances separately. Node health alone is not
   balance verification, a reservation or permission to broadcast.
9. Create an explicit disposable settlement fixture and persist its expected txid,
   destination, atomic amount and originating block before running read-only
   transaction verification. Do not derive expected values from the same response
   being validated.
10. For XMZ, distinguish daemon `height` (block count) from the current tip height.
    Wallet confirmations for a transfer at height H are checked as
    `daemonHeight - H`; the normalized tip passed to recovery tracking is
    `daemonHeight - 1`.

`create-bitcoin-settlement-fixture.py` constructs and signs one disposable regtest
payment, atomically saves its signed transaction and expected destination/amount
before broadcast, and resumes safely after interruption. It removes the signed
transaction from the manifest after confirmation. Run it only as the isolated
`monzero` lab account; it must never point at a production wallet or network.

`create-bitcoin-lock-psbt-fixture.py` creates a separate unsigned and unbroadcast
wallet-funded PSBT paying the exact ordered two-key COMIT P2WSH program. It locks
the selected test-wallet inputs, persists the PSBT and independently recomputable
template commitment with mode 0600, and is idempotent. `dex_rpc_live_probe`
decodes this real artifact through Bitcoin Core, independently queries every live
prevout at a pinned tip, validates the script, exact amount, template and fee
ceiling, and remains `executable=false`. Delete the disposable wallet/fixture or
explicitly unlock its inputs when resetting the lab; never copy this workflow to a
production wallet.

`create-monzero-settlement-fixture.py` follows the same recovery rule for one
fakechain XMZ payment: wallet transaction metadata and expectations are saved before
`relay_tx`, the receiving test wallet verifies the incoming payment after isolated
mining, and metadata is removed after confirmation. It is likewise lab-only.

The fakechain service must retain `--keep-fakechain`. Fixture setup calls the
daemon's `/save_bc` endpoint after generated blocks. Verify recovery by rebooting
the VM, rerunning both fixture scripts with strict error handling, confirming chain
heights did not change, and rerunning `dex_rpc_live_probe`.

The wallet RPC shared ring database is stored under the owner-only recovery tree.
Do not remove its explicit `--shared-ringdb-dir`: `ProtectHome=true` intentionally
blocks the default home-directory location.

Both chains' JSON health responses reject duplicate object keys, non-standard
NaN/Infinity values and floating-point exponent overflow, including nested fields.
This is parser validation, not proof that a node's claims are truthful.

`health.py` rejects wrong networks, missing/mismatched genesis, incomplete sync,
missing fields and invalid height types. Its fixtures are not end-to-end RPC tests.
Timeouts, authentication failures, reorg handling and wallet balances still need
transport/integration tests. Live trading must remain disabled throughout setup.

The live-lab checker now disables environment HTTP proxies and redirects for
loopback XMZ RPC, caps replies at 1 MiB, requires matching JSON-RPC version/ID,
rejects duplicate object keys and error-bearing replies, and places a ten-second
deadline on Bitcoin CLI checks. `test_live_lab_transport.py` tests these boundaries
with mocks and a disposable loopback HTTP server, including actual redirect
refusal and oversized replies. Passing it is not evidence that either VM node is
running. The Python socket timeout is an inactivity timeout. The normal checker
entry point therefore runs its probe worker in a separate POSIX process group
with a 20-second total deadline, terminating that group (including Bitcoin CLI
children) on timeout and returning status 124. Do not invoke the internal
`--probe-worker` mode directly when checking operational readiness.

## Deployed VM convention

The current lab uses Bitcoin Core regtest on loopback RPC port `18443` and
Monzero pre13 fakechain on loopback RPC port `6175`. Its disposable wallet RPC is
on loopback port `6176`. Systemd independently prevents these processes from
opening non-loopback IP connections. Run `verify-live-lab.py` as `monzero` to
verify the pinned Bitcoin regtest and Monzero genesis hashes, network types and
synchronization. These nodes contain worthless test funds only.
