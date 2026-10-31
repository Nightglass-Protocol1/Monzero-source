require("@nomicfoundation/hardhat-ethers");
const fs = require("fs");
const os = require("os");
const path = require("path");

// BNB testnet only. The test accounts' phrase stays outside the repository (mode 600).
const TESTNET_ACCOUNTS = path.join(os.homedir(), ".config", "monzero-dex", "bsc-testnet-accounts.json");
const networks = {};
if (fs.existsSync(TESTNET_ACCOUNTS)) {
  const { mnemonic } = JSON.parse(fs.readFileSync(TESTNET_ACCOUNTS, "utf8"));
  networks.bscTestnet = {
    url: process.env.BSC_TESTNET_RPC || "https://bsc-testnet-rpc.publicnode.com",
    chainId: 97,
    accounts: { mnemonic, count: 2 },
  };
}

/** @type import('hardhat/config').HardhatUserConfig */
module.exports = {
  solidity: {
    version: "0.8.28",
    settings: { optimizer: { enabled: true, runs: 200 }, evmVersion: "cancun" },
  },
  networks,
};
