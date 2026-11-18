# Monzero Phase 0 integration tests

These tests use temporary regtest directories and must not connect to or modify
the live Monzero network.

Run the three-node smoke test from the repository root:

```bash
python3 tests/phase0/monzero_multinode.py --build-dir build
```

The test starts three `monzerod` processes and one `monzero-wallet-rpc`
process on localhost. It generates a temporary wallet, creates blocks, checks
that every node reaches the same tip, restarts one node, and verifies that the
restarted node catches up. It then matures a coinbase output, sends one XMZ to
a second temporary wallet, verifies transaction-pool relay through all three
nodes, mines the transaction on a different node, and verifies the receiver's
confirmed balance. Before the transfer, it isolates the two edge nodes, creates
competing three- and five-block forks, reconnects them, and verifies all nodes
adopt the longer fork and identical tip. Finally, it restores the receiving
wallet from its temporary mnemonic seed, rescans from height zero, and checks
the recovered address and balance.

Ports 26174-26205 must be available. All child processes are terminated on
normal completion and on test failure. Failed runs retain temporary databases
and logs automatically; pass `--keep-data` to retain successful runs too.
RPC timeouts identify the method and port without logging wallet parameters.
Node RPC calls have a five-second timeout; wallet RPC calls have a bounded
60-second timeout to allow password derivation, scanning, and signing on busy
builders. State-changing calls are not automatically retried.
Temporary data defaults to `build/test-tmp`
so LMDB mappings do not exhaust small dedicated `/tmp` filesystems.

This is a smoke test, not the complete consensus qualification suite.
Double-spend, deeper/adversarial reorg, and adversarial asset tests remain
required before activating a new protocol version.

## User-facing branding gate

Run the source-level release gate after changing executable names, runtime log
defaults, network ports, or shell completions:

```bash
tests/phase0/test-user-facing-branding.sh
```

It rejects inherited executable names and ports on the active command-line
surfaces it covers. Translation catalogs and technical documentation require
separate contextual review because they intentionally preserve some upstream
source strings, protocol terminology, citations, and attribution.

## Release archive safety gate

Run the malicious-archive regression test after changing binary package
construction or verification:

```bash
tests/phase0/test-release-archive-safety.sh
```

It proves that normal tar and ZIP inputs pass pre-extraction validation while
links, traversal paths, and duplicate entries fail closed.

Release notes are also bound to the exact package label. Run the regression
after changing package naming, manifests, or release documentation:

```bash
tests/phase0/test-release-notes-version.sh
```

The binary package verifiers reject missing or stale `RELEASE_NOTES.md` files.

## Fast unit gate

Run the ordinary unit tests with a validating resolver and keep the expensive
pruning-boundary concurrency scenario in the separate stress-test gate:

```bash
tests/phase0/run-fast-unit-tests.sh build
```

Override `DNS_PUBLIC` with another validating TCP resolver when required. The
excluded `cryptonote_protocol_handler.race_condition` test must still pass in
the scheduled stress gate; exclusion here is not a waiver.
The shell test gates likewise default `TMPDIR` to `build/test-tmp`; set it
explicitly if another test filesystem is preferred.

## Stress gate

Run the pruning-boundary concurrency scenario separately:

```bash
tests/phase0/run-stress-tests.sh build
```

The default timeout is 1,800 seconds. Override it with
`MONZERO_STRESS_TIMEOUT=<seconds>` when running on slower hardware. A timeout
is a failed gate and must not be treated as a skipped test.

## Adversarial core gate

Build the native core-test executable and run the focused double-spend and
key-image suite:

```bash
cmake --build build --target core_tests -j2
tests/phase0/run-adversarial-core-tests.sh build
```

This covers conflicting key images in the transaction pool; double spends in
one transaction, one block, separate blocks, competing chains, and alternate
chains; and both kept-by-block modes. The default timeout is 1,800 seconds and
can be overridden with `MONZERO_ADVERSARIAL_TIMEOUT`.
