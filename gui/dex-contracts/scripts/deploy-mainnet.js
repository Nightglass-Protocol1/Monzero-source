// Deploys XmzSwap with a key the owner types in, and records it in deployments/<name>.json, on:
//   npx hardhat run --network bscMainnet scripts/deploy-mainnet.js    BNB Smart Chain (56)
//   npx hardhat run --network ethSepolia scripts/deploy-mainnet.js    Ethereum Sepolia testnet
//   npx hardhat run --network ethMainnet scripts/deploy-mainnet.js    Ethereum mainnet (1)
// Rehearsal on a local node first (key: any funded local account):
//   npx hardhat node &  npx hardhat run --network localhost scripts/deploy-mainnet.js
//
// The key is read from a hidden prompt on the terminal: never from the environment or the command
// line, and never printed or saved. Or, with DEPLOYER=test-phrase, the real-funds test's buyer
// account (account 0 of the private test phrase file made by new-test-phrase.js) pays instead,
// so no key has to be exported from a wallet. XmzSwap has no owner, admin or upgrade path, so
// the deploying account gets no control over the contract or over anyone's swap.
const { ethers, artifacts, network } = require("hardhat");
const fs = require("fs");
const path = require("path");
const readline = require("readline");
const os = require("os");

const CHAINS = {
  56n: { name: "bsc-mainnet", coin: "BNB", confirmations: 15, real: true },
  1n: { name: "eth-mainnet", coin: "ETH", confirmations: 96, real: true },
  11155111n: { name: "eth-sepolia", coin: "SepoliaETH", confirmations: 96, real: false },
};
const LOCAL = { name: "local-rehearsal", coin: "local ETH", confirmations: 1, real: false };

function ask(question, hidden) {
  return new Promise((resolve) => {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout, terminal: true });
    if (hidden) rl._writeToOutput = (s) => { if (s.includes(question)) rl.output.write(s); };
    rl.question(question, (answer) => { rl.close(); if (hidden) process.stdout.write("\n"); resolve(answer.trim()); });
  });
}

async function main() {
  const { chainId } = await ethers.provider.getNetwork();
  // Decided by the network, not the chain id: a local copy of mainnet also reports chain 1, and
  // must never write the real chain's record
  const local = ["localhost", "hardhat"].includes(network.name);
  const chain = local ? LOCAL : CHAINS[chainId];
  if (!chain) throw new Error(`refusing chain ${chainId}: ${Object.keys(CHAINS).join(", ")} or a local rehearsal only`);
  const { name, coin } = chain;
  const out = path.join(__dirname, "..", "deployments", `${name}.json`);
  if (!local && fs.existsSync(out)) throw new Error(`${out} exists: already deployed`);

  console.log(`Network: ${network.name} (chain ${chainId})${chain.real ? `  ** REAL ${coin} **` : ""}`);
  let deployer;
  if (process.env.DEPLOYER === "test-phrase") {
    const file = process.env.ACCOUNTS_FILE || path.join(os.homedir(), ".config", "monzero-dex", `${name}-accounts.json`);
    if ((fs.statSync(file).mode & 0o077) !== 0) throw new Error(`${file} must be private (chmod 600)`);
    const { mnemonic } = JSON.parse(fs.readFileSync(file, "utf8"));
    deployer = ethers.HDNodeWallet.fromPhrase(mnemonic, undefined, "m/44'/60'/0'/0/0").connect(ethers.provider);
    console.log("Paying with the test phrase's buyer account (account 0)");
  } else {
    let hex = await ask("Deployer private key (hidden; 64 hex, 0x optional): ", true);
    if (!/^(0x)?[0-9a-fA-F]{64}$/.test(hex)) throw new Error("that is not a 32-byte hex private key");
    deployer = new ethers.Wallet(hex.startsWith("0x") ? hex : "0x" + hex, ethers.provider);
    hex = null;
  }

  const balance = await ethers.provider.getBalance(deployer.address);
  const factory = await ethers.getContractFactory("XmzSwap", deployer);
  const gas = await ethers.provider.estimateGas({ ...(await factory.getDeployTransaction()), from: deployer.address });
  const fee = await ethers.provider.getFeeData();
  const gasPrice = fee.gasPrice ?? fee.maxFeePerGas;
  const cost = gas * gasPrice;
  console.log(`Deployer: ${deployer.address}`);
  console.log(`Balance:  ${ethers.formatEther(balance)} ${coin}`);
  console.log(`Estimate: ${gas} gas x ${ethers.formatUnits(gasPrice, "gwei")} gwei = ${ethers.formatEther(cost)} ${coin}`);
  if (balance < (cost * 3n) / 2n) throw new Error("the balance does not cover the estimate with a 50% margin");
  if ((await ask('Type DEPLOY to deploy XmzSwap with this account: ', false)) !== "DEPLOY") {
    console.log("Not deployed.");
    return;
  }

  const swap = await factory.deploy({ gasLimit: (gas * 6n) / 5n });
  const tx = swap.deploymentTransaction();
  console.log("deploy tx", tx.hash);
  const receipt = await tx.wait(chain.confirmations);
  const address = await swap.getAddress();

  const onChain = await ethers.provider.getCode(address);
  const artifact = await artifacts.readArtifact("XmzSwap");
  if (onChain !== artifact.deployedBytecode) throw new Error("runtime code on chain differs from the compiled artifact");

  const record = {
    chainId: Number(chainId),
    contract: address,
    deployTx: tx.hash,
    block: receipt.blockNumber,
    deployer: deployer.address,
    gasUsed: receipt.gasUsed.toString(),
    runtimeCodeKeccak: ethers.keccak256(onChain),
    compiler: "solc 0.8.28, optimizer 200 runs, evm cancun",
    deployedAt: new Date().toISOString(),
  };
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(record, null, 2) + "\n");
  console.log(JSON.stringify(record, null, 2));
  console.log(`Saved ${path.relative(process.cwd(), out)}. Same code as the BNB deployments: ${
    record.runtimeCodeKeccak === "0xe2368d8f82649ecd46a19e882e70c9535752dbc9f4c9499a22af141877f21f1a" ? "yes" : "NO - check"}`);
}

main().catch((e) => {
  console.error(e.message || e);
  process.exit(1);
});
