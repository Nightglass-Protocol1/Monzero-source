// Deploys XmzSwap to BNB testnet (chain 97) and records it in deployments/bsc-testnet.json.
//   npx hardhat run --network bscTestnet scripts/deploy-testnet.js
// Refuses any other chain: mainnet deployment is not part of this script.
const { ethers, artifacts } = require("hardhat");
const fs = require("fs");
const path = require("path");

const OUT = path.join(__dirname, "..", "deployments", "bsc-testnet.json");

async function main() {
  const { chainId } = await ethers.provider.getNetwork();
  if (chainId !== 97n) throw new Error(`refusing to deploy on chain ${chainId}: BNB testnet (97) only`);
  if (fs.existsSync(OUT)) throw new Error(`${OUT} exists: already deployed (delete it to deploy a new contract)`);

  const [deployer] = await ethers.getSigners();
  console.log("deployer", deployer.address, "balance", ethers.formatEther(await ethers.provider.getBalance(deployer)), "tBNB");
  const swap = await (await ethers.getContractFactory("XmzSwap", deployer)).deploy();
  const tx = swap.deploymentTransaction();
  console.log("deploy tx", tx.hash);
  const receipt = await tx.wait(3);
  const address = await swap.getAddress();

  // The code on chain must be exactly our compiled runtime code
  const onChain = await ethers.provider.getCode(address);
  const artifact = await artifacts.readArtifact("XmzSwap");
  if (onChain !== artifact.deployedBytecode) throw new Error("runtime code on chain differs from the compiled artifact");

  const record = {
    chainId: 97,
    contract: address,
    deployTx: tx.hash,
    block: receipt.blockNumber,
    deployer: deployer.address,
    gasUsed: receipt.gasUsed.toString(),
    runtimeCodeKeccak: ethers.keccak256(onChain),
    compiler: "solc 0.8.28, optimizer 200 runs, evm cancun",
    deployedAt: new Date().toISOString(),
  };
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(record, null, 2) + "\n");
  console.log(JSON.stringify(record, null, 2));
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
