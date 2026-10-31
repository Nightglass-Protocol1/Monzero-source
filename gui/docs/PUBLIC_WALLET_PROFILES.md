# Opt-in public wallet profiles

Private by default. The final wallet-creation screen can save an address-only or address-and-balance preference; it clearly explains that publication is not automatic. The owner confirms publication after opening the wallet using Settings → Wallet → Public wallet profile. Every update is a manual snapshot. Nothing is sent merely by opening the panel or changing its selection.

The wallet signs a domain-separated challenge with its existing spend-key message-signing API. The challenge commits to the primary address, exact atomic balance string, wallet height, visibility, public name, origin, nonce, expiry and revision. No seed, private view key or spending key leaves the wallet. No transaction is created. Balances include all accounts and are explicitly wallet-reported, not cryptographically proven balances.

Stop sharing reports removal only after the server acknowledges it. A timeout or failed response warns that the public profile may still be visible. Server revisions reject replay of previous publication after removal. Existing copied data cannot be erased. GenesisWallet1 is permanently public by project-owner policy; its initial directory value is the existing historical founder disclosure, not a live reading.

Read-only/watch-only or unsupported signing wallets cannot authorize changes. Testnet wallets cannot publish. Balance publication requires a connected, synchronized wallet and uses decimal strings to preserve atomic precision.

Tests: `--test-qml -input tests/qml/tst_PublicWalletProfile.qml`. Server and real-signature evidence is under `core/build/public-wallets-*`; server implementation and deployment instructions are in `core-wt-live/utils/public-wallets`.

Transport: a scoped C++ HTTPS client targets only the challenge/update routes, rejects redirects, bounds responses/timeouts and uses the wallet proxy when configured. The global QML network blocker remains unchanged.

Validation (18 October): all 44 QML results passed, including wallet creation/restore, startup, mining and public-profile controls. The existing wizard test signal name was corrected to `onUseMonzeroClicked` so those tests actually load. Thirteen server tests passed. A disposable mainnet wallet's real spend signature was accepted; modified messages and view signatures were rejected. The native GUI published a zero-balance snapshot to the live directory and then removed it with Stop sharing; a separate HTTPS search confirmed absence. No funds moved. Screenshots and receipts: `core/build/public-profiles-preview-20261018/`.

Windows compatibility: the static Qt build is configured without OpenSSL, so profile requests use the wallet's existing core HTTPS client with system-CA verification. Requests are asynchronous, fixed to the two profile endpoints, limited to 15 seconds and 64 KiB responses, and use the configured wallet proxy without fallback. General QML networking remains blocked. The native GUI publish/remove flow and all 44 QML results passed after this transport change.
