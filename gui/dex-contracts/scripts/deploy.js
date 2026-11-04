// General XmzSwap deployer for any configured EVM chain.
//   npx hardhat run --network <name> scripts/deploy.js
// It records deployments/<network>.json and verifies the on-chain runtime
// bytecode equals the compiled artifact. It refuses to overwrite an existing
// record. Mainnet chains additionally require:
//   MONZERO_DEX_EVM_MAINNET=I_ACCEPT_REAL_FUNDS_RISK
// (same shape as the Bitcoin mainnet gate). The contract has no owner/admin/
// pause, so each deployment is independent and immutable.
const { ethers, artifacts, network } = require("hardhat");
const fs = require("fs");
const path = require("path");

const MAINNET = { 1: "ethereum", 56: "bsc", 8453: "base", 42161: "arbitrum", 137: "polygon" };

async function main() {
  const id = Number((await ethers.provider.getNetwork()).chainId);
  const isMainnet = id in MAINNET;
  if (isMainnet && process.env.MONZERO_DEX_EVM_MAINNET !== "I_ACCEPT_REAL_FUNDS_RISK") {
    throw new Error(
      `chain ${id} (${MAINNET[id]}) is mainnet with real funds; set ` +
      `MONZERO_DEX_EVM_MAINNET=I_ACCEPT_REAL_FUNDS_RISK to opt in`);
  }

  const out = path.join(__dirname, "..", "deployments", `${network.name}.json`);
  if (fs.existsSync(out)) throw new Error(`${out} exists: already deployed (delete it to deploy a new contract)`);

  const [deployer] = await ethers.getSigners();
  const balance = await ethers.provider.getBalance(deployer);
  console.log(`network ${network.name} (chain ${id}) deployer ${deployer.address} balance ${ethers.formatEther(balance)}`);

  const swap = await (await ethers.getContractFactory("XmzSwap", deployer)).deploy();
  const tx = swap.deploymentTransaction();
  console.log("deploy tx", tx.hash);
  await tx.wait(isMainnet ? 5 : 3);
  const address = await swap.getAddress();

  // The runtime code on chain must be exactly our compiled runtime code.
  const onChain = await ethers.provider.getCode(address);
  const artifact = await artifacts.readArtifact("XmzSwap");
  if (onChain !== artifact.deployedBytecode) throw new Error("runtime code on chain differs from the compiled artifact");

  const record = {
    network: network.name,
    chainId: id,
    contract: address,
    deployer: deployer.address,
    deployTx: tx.hash,
    deployedAt: new Date().toISOString(),
  };
  fs.writeFileSync(out, JSON.stringify(record, null, 2) + "\n");
  console.log("deployed + verified:\n" + JSON.stringify(record, null, 2));
}

main().catch((e) => { console.error(e); process.exit(1); });
