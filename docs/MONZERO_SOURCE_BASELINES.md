# Monzero source baselines

This document records the recoverable Phase 0 source state created on
2026-08-15. It is a development baseline, not an activation or release of the
asset and NFT protocol.

## Revisions

| Repository | Commit | Annotated tag |
| --- | --- | --- |
| Core | `28f1919249465d3230f45ed21686c5a836d56df0` | `monzero-phase0-assets-prototype-20260815` |
| GUI | `cb8d4378527e9816abfadb9c0dbc9259a1ea1385` | `monzero-gui-phase0-20260815-r1` |

The GUI revision records the core repository as submodule commit
`28f1919249465d3230f45ed21686c5a836d56df0`.

## Recovery copies

The current workstation has local bare remotes at:

- `/home/pinhead/Projects/Monzero-Fork-backups/git-remotes/monzero-core.git`
- `/home/pinhead/Projects/Monzero-Fork-backups/git-remotes/monzero-gui.git`

Complete Git bundles are stored in
`/home/pinhead/Projects/Monzero-Fork-backups/source-baselines-20260815`:

| Bundle | SHA-256 |
| --- | --- |
| `monzero-core-phase0.bundle` | `dc574a818a53070981c06b5c8c8d5bcbbf06b5712693d662e77a0a3f108d01db` |
| `monzero-gui-phase0.bundle` | `da3adf8acff557adf191b19a62931649830ca4954d2b4ffbd64a96bafff6db7a` |

Run `utils/release/verify-source-baselines.sh` to check the commits, tags,
GUI submodule pin, bundles, and bundle hashes. The bundle directory and GUI
checkout may be supplied as its first and second arguments.

## Clone test

The sibling bare-remotes layout can be tested with:

```bash
git clone /home/pinhead/Projects/Monzero-Fork-backups/git-remotes/monzero-gui.git monzero-gui
git -c protocol.file.allow=always -C monzero-gui submodule update --init monero
```

The `protocol.file.allow` override is only needed for a local filesystem
remote. It is not needed after the projects move to HTTPS remotes.

## Validation state

- The staged core test snapshot passed 1,247 of 1,247 tests.
- The core and GUI compiled successfully on the development workstation.
- The GUI executable passed a command-line smoke test.
- Linux developer packages are deterministic at the archive level, but strict
  release validation intentionally rejects the current dynamic, unstripped,
  debug/unverified artifacts.
- No public Monzero-owned Git remote has been configured or pushed.
- Assets and NFTs remain inactive pending independent cryptographic and
  consensus review, activation design, reproducible release builds, and a
  public test network.

