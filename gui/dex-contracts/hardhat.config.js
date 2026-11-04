require("@nomicfoundation/hardhat-ethers");
const fs = require("fs");
const os = require("os");
const path = require("path");

// Deployer accounts stay OUTSIDE the repo (keep the file mode 600):
//   ~/.config/monzero-dex/<network>-accounts.json   ->   { "mnemonic": "..." }
// A network only becomes available when its accounts file exists, so an
// un-configured chain can never be selected by accident.
const ACCOUNTS_DIR = path.join(os.homedir(), ".config", "monzero-dex");
function accounts(name) {
  const f = path.join(ACCOUNTS_DIR, `${name}-accounts.json`);
  if (!fs.existsSync(f)) return undefined;
  const { mnemonic } = JSON.parse(fs.readFileSync(f, "utf8"));
  return { mnemonic, count: 2 };
}

// [network name, chainId, default public RPC, env var to override the RPC]
// Testnets first, then mainnets (deploying to a mainnet also needs the opt-in
// env var in scripts/deploy.js).
const CHAINS = [
  ["bscTestnet",      97,       "https://bsc-testnet-rpc.publicnode.com",       "BSC_TESTNET_RPC"],
  ["sepolia",         11155111, "https://ethereum-sepolia-rpc.publicnode.com",  "SEPOLIA_RPC"],
  ["baseSepolia",     84532,    "https://base-sepolia-rpc.publicnode.com",      "BASE_SEPOLIA_RPC"],
  ["arbitrumSepolia", 421614,   "https://arbitrum-sepolia-rpc.publicnode.com",  "ARBITRUM_SEPOLIA_RPC"],
  ["polygonAmoy",     80002,    "https://polygon-amoy-bor-rpc.publicnode.com",  "POLYGON_AMOY_RPC"],
  ["bsc",             56,       "https://bsc-rpc.publicnode.com",               "BSC_RPC"],
  ["base",            8453,     "https://base-rpc.publicnode.com",              "BASE_RPC"],
  ["arbitrum",        42161,    "https://arbitrum-one-rpc.publicnode.com",      "ARBITRUM_RPC"],
  ["polygon",         137,      "https://polygon-bor-rpc.publicnode.com",       "POLYGON_RPC"],
  ["ethereum",        1,        "https://ethereum-rpc.publicnode.com",          "ETH_RPC"],
];

const networks = {};
for (const [name, chainId, rpc, env] of CHAINS) {
  const acct = accounts(name);
  if (!acct) continue; // no key file for this chain -> not configured (safe)
  networks[name] = { url: process.env[env] || rpc, chainId, accounts: acct };
}

/** @type import('hardhat/config').HardhatUserConfig */
module.exports = {
  solidity: {
    version: "0.8.28",
    settings: { optimizer: { enabled: true, runs: 200 }, evmVersion: "cancun" },
  },
  networks,
};
