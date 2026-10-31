// Deploys XmzSwap to BNB Smart Chain mainnet (chain 56) with a key the owner types in, and
// records it in deployments/bsc-mainnet.json.
//   npx hardhat run --network bscMainnet scripts/deploy-mainnet.js
// Rehearsal on a local node first (key: any funded local account):
//   npx hardhat node &  npx hardhat run --network localhost scripts/deploy-mainnet.js
//
// The key is read from a hidden prompt on the terminal: never from a file, the environment or
// the command line, and never printed or saved. XmzSwap has no owner, admin or upgrade path, so
// the deploying account gets no control over the contract or over anyone's swap.
const { ethers, artifacts, network } = require("hardhat");
const fs = require("fs");
const path = require("path");
const readline = require("readline");

const CHAINS = { 56n: "bsc-mainnet", 31337n: "local-rehearsal" };

function ask(question, hidden) {
  return new Promise((resolve) => {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout, terminal: true });
    if (hidden) rl._writeToOutput = (s) => { if (s.includes(question)) rl.output.write(s); };
    rl.question(question, (answer) => { rl.close(); if (hidden) process.stdout.write("\n"); resolve(answer.trim()); });
  });
}

async function main() {
  const { chainId } = await ethers.provider.getNetwork();
  const name = CHAINS[chainId];
  if (!name) throw new Error(`refusing chain ${chainId}: BNB mainnet (56) or a local rehearsal (31337) only`);
  const out = path.join(__dirname, "..", "deployments", `${name}.json`);
  if (name !== "local-rehearsal" && fs.existsSync(out)) throw new Error(`${out} exists: already deployed`);

  console.log(`Network: ${network.name} (chain ${chainId})${chainId === 56n ? "  ** REAL BNB **" : ""}`);
  let hex = await ask("Deployer private key (hidden; 64 hex, 0x optional): ", true);
  if (!/^(0x)?[0-9a-fA-F]{64}$/.test(hex)) throw new Error("that is not a 32-byte hex private key");
  const deployer = new ethers.Wallet(hex.startsWith("0x") ? hex : "0x" + hex, ethers.provider);
  hex = null;

  const balance = await ethers.provider.getBalance(deployer.address);
  const factory = await ethers.getContractFactory("XmzSwap", deployer);
  const gas = await ethers.provider.estimateGas({ ...(await factory.getDeployTransaction()), from: deployer.address });
  const fee = await ethers.provider.getFeeData();
  const gasPrice = fee.gasPrice ?? fee.maxFeePerGas;
  const cost = gas * gasPrice;
  console.log(`Deployer: ${deployer.address}`);
  console.log(`Balance:  ${ethers.formatEther(balance)} BNB`);
  console.log(`Estimate: ${gas} gas x ${ethers.formatUnits(gasPrice, "gwei")} gwei = ${ethers.formatEther(cost)} BNB`);
  if (balance < (cost * 3n) / 2n) throw new Error("the balance does not cover the estimate with a 50% margin");
  if ((await ask('Type DEPLOY to deploy XmzSwap with this account: ', false)) !== "DEPLOY") {
    console.log("Not deployed.");
    return;
  }

  const swap = await factory.deploy({ gasLimit: (gas * 6n) / 5n });
  const tx = swap.deploymentTransaction();
  console.log("deploy tx", tx.hash);
  const receipt = await tx.wait(chainId === 56n ? 15 : 1);
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
  console.log(`Saved ${path.relative(process.cwd(), out)}. Same code as testnet: ${
    record.runtimeCodeKeccak === "0xe2368d8f82649ecd46a19e882e70c9535752dbc9f4c9499a22af141877f21f1a" ? "yes" : "NO - check"}`);
}

main().catch((e) => {
  console.error(e.message || e);
  process.exit(1);
});
