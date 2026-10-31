// Writes engine/tests/vectors.json: values computed by the deployed contract itself, so the
// Rust engine's hashing and commitments are checked against Solidity, not against itself.
//   npx hardhat run scripts/vectors.js
const { ethers } = require("hardhat");
const fs = require("fs");
const path = require("path");

async function main() {
  const swap = await (await ethers.getContractFactory("XmzSwap")).deploy();
  const contract = await swap.getAddress();
  const { chainId } = await ethers.provider.getNetwork();

  const secrets = [
    "0x0000000000000000000000000000000000000000000000000000000000000001",
    "0x4c0883a69102937d6231471b5dbb6204fe5129617082792ae468d01a3f362318",
    "0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364140",
  ];
  const commitments = secrets.map((s) => ({ secret: s, commitment: new ethers.Wallet(s).address }));
  for (const c of commitments) {
    if (!(await swap.secretMatches(c.secret, c.commitment))) throw new Error("contract rejects " + c.secret);
  }

  const s = [
    "0x1111111111111111111111111111111111111111",
    "0x2222222222222222222222222222222222222222",
    commitments[1].commitment,
    commitments[0].commitment,
    1793300000n,
    1793321600n,
    ethers.ZeroAddress,
    ethers.parseEther("1.5"),
    42n,
  ];
  const id = await swap.swapId(s);
  const fee = ethers.parseEther("0.001");
  const relayer = "0x3333333333333333333333333333333333333333";
  const inner = ethers.keccak256(
    ethers.AbiCoder.defaultAbiCoder().encode(
      ["string", "uint256", "address", "bytes32", "uint256", "address"],
      ["XMZ-SWAP-RELAY-V1", chainId, contract, id, fee, relayer],
    ),
  );
  const out = {
    note: "Computed by XmzSwap.sol under Hardhat; regenerate with npx hardhat run scripts/vectors.js",
    commitments,
    swap: s.map(String),
    swapId: id,
    relay: { chainId: chainId.toString(), contract, fee: fee.toString(), relayer, digest: ethers.hashMessage(ethers.getBytes(inner)) },
  };
  const file = path.join(__dirname, "..", "engine", "tests", "vectors.json");
  fs.writeFileSync(file, JSON.stringify(out, null, 2) + "\n");
  console.log("wrote", file);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
