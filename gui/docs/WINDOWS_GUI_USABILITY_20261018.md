# Windows GUI usability candidate — 18 October 2026

Built from GUI `5b45b03670db266152a762b1b5a6ee8b40646d94`, with unchanged v1.0.0 core `c4165784db57d8ae410fc8af58861e4178ee1a42`. This isolated release worktree contains no unfinished DEX implementation. No release was published.

## Changes

- Check local-node startup independently of wallet initialization; a Connecting notification before Disconnected no longer skips the check. Reuse an existing node, start only for the current local-node selection, and ignore stale callbacks. Remove the extra countdown and five-second post-start delay.
- Close input and bound the lifetime of one-shot daemon commands; a stuck status client cannot leave the startup watcher waiting forever.
- Expose Mining / tools, CPU thread selection (minimum one), asynchronous explicit Start/Stop, sync/connection guidance, live hashrate and the actual mining reward address. Requests use a private loopback-bound RPC client and do not follow changes to remote-node selection. No mining starts automatically.
- Refresh mining status across pages. Failed observations clear the mining badge without claiming the miner has stopped. Stop remains available when connected even if the node becomes unsynchronized.
- Apply the supplied XMZ artwork and teal palette, including the account card and mining badge. Load bundled outline fonts before creating controls.

## Verification

- Windows x64 static cross-build passed using the existing Windows build image and v1.0.0 core.
- 27 QML test results passed: 14 mining and 13 startup, including fixture setup/cleanup; 23 behavior cases. Syntax, resource enumeration and whitespace checks passed.
- The final Windows executable ran under Wine 11.19 against a native daemon in an isolated network namespace, offline testnet, zero public peers. Start with one thread and Stop were independently confirmed through node RPC. The GUI displayed hashrate and reward address. An earlier isolated run showed newly mined funds and reward transactions.
- With Receive open, external mining start appeared as a live hashrate; after external stop, the badge returned to Connected. Screenshots were inspected. The repeat used difficulty 100000 to prevent flooding the wallet.
- The earlier difficulty-one stress fixture generated over 1,400 rewards and made the GUI unresponsive. It was excluded from background-status acceptance. Large reward-history refresh performance remains a separate finding.

Evidence: `core/build/codex-gui-wine-smoke-20261018/ACCEPTANCE.json`, screenshots and RPC observations; `core/build/codex-gui-{mining,startup}-qml-20261018.log`; Windows build logs under `core/build/codex-gui-usability-windows-*`.

## Limits and remaining work

Native Windows 10/11 testing remains required. Wine cannot initialize the Windows daemon/shared-ring LMDB databases in this environment; the unchanged published v1.0.0 daemon reproduces the same error. Automatic launch was observed, but this does not qualify a successful Windows-daemon startup. Successful mining integration used a native daemon.

The reported user's actual mining state cannot be determined from the supplied log alone. It records temporary node-connection errors, font-rendering warnings and a restore-height fallback to scanning from zero. The old GUI could retain a stale mining badge when another page was open. No reward transactions by itself does not prove solo mining stopped: rewards require finding blocks.

Keep the candidate unpublished until native Windows testing and release approval. Port the verified GUI fixes into the DEX development worktree separately, preserving its active testnet trade and source-hash checks. These GUI checks do not close DEX desktop integration or beta-readiness gates.
