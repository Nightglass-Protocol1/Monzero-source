# Solana tools

## smoke-mint.js — live creation smoke test
Creates a fixed-supply SPL token and a 1/1 NFT with the Studio engine
(`website/app/sol-mint.js`) on a real cluster and verifies the on-chain result (incl. the
Metaplex metadata). Not in CI — it needs a validator, the Metaplex program, and network.

```bash
# Local validator (no faucet needed) — in one shell:
solana-test-validator --reset --quiet --url https://api.mainnet-beta.solana.com \
  --clone-upgradeable-program metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s
# in another:
npm run smoke:solana

# Devnet (fund the printed payer first; devnet RPC airdrop is closed — use faucet.solana.com):
npm run smoke:solana -- --rpc https://api.devnet.solana.com --keypair payer.json
```
Verified on devnet 2026-11-06 (NFT mint 4Mmvrkzg…dTHXT).
