// Read-only: checks each token in assets.json against its chain's RPC (ERC-20
// symbol() + decimals()) and reports mismatches. Changes nothing. Run before
// flipping any token's "verified" flag to true.
//   node scripts/verify-assets.js
const { ethers } = require("ethers");
const assets = require("../assets.json");
const RPC = {
  bsc: process.env.BSC_RPC || "https://bsc-rpc.publicnode.com",
  ethereum: process.env.ETH_RPC || "https://ethereum-rpc.publicnode.com",
  base: process.env.BASE_RPC || "https://base-rpc.publicnode.com",
  arbitrum: process.env.ARBITRUM_RPC || "https://arbitrum-one-rpc.publicnode.com",
  polygon: process.env.POLYGON_RPC || "https://polygon-bor-rpc.publicnode.com",
};
const ERC20 = ["function symbol() view returns (string)", "function decimals() view returns (uint8)"];
(async () => {
  let ok = 0, bad = 0;
  for (const [name, chain] of Object.entries(assets.chains)) {
    const url = RPC[name]; if (!url) { console.log(`- ${name}: no RPC configured`); continue; }
    const provider = new ethers.JsonRpcProvider(url, chain.chainId);
    for (const t of chain.tokens) {
      try {
        const c = new ethers.Contract(t.address, ERC20, provider);
        const [sym, dec] = await Promise.all([c.symbol(), c.decimals()]);
        const match = sym === t.symbol && Number(dec) === t.decimals;
        console.log(`${match ? "OK      " : "MISMATCH"} ${name}/${t.symbol}  on-chain: ${sym} / ${dec}dp  registry: ${t.symbol} / ${t.decimals}dp`);
        match ? ok++ : bad++;
      } catch (e) { console.log(`ERROR   ${name}/${t.symbol} @ ${t.address}: ${e.shortMessage || e.message}`); bad++; }
    }
  }
  console.log(`\n${ok} verified on-chain, ${bad} mismatch/error`);
})();
