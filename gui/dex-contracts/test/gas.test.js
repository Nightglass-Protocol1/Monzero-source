// Gas profile for the core native-coin operations. Informs the per-chain minimum
// trade size (the maker's gas cost must stay well under the spread). Run with:
//   npx hardhat test test/gas.test.js
const { ethers } = require("hardhat");
const ZERO = "0x0000000000000000000000000000000000000000";
const D1 = 3 * 3600, D2 = 6 * 3600;
function secret() { const w = ethers.Wallet.createRandom(); return { secret: w.privateKey, commitment: w.address }; }

describe("XmzSwap gas profile", function () {
  it("reports gas for the core native-coin operations", async function () {
    const [owner, claimer] = await ethers.getSigners();
    const swap = await (await ethers.getContractFactory("XmzSwap")).deploy();
    await swap.waitForDeployment();
    const deployGas = (await swap.deploymentTransaction().wait()).gasUsed;
    const value = ethers.parseEther("1");

    async function create(nonce) {
      const c = secret(), r = secret();
      const receipt = await (await swap.connect(owner)
        .newSwap(await claimer.getAddress(), c.commitment, r.commitment, D1, D2, ZERO, value, nonce, { value })).wait();
      const ev = receipt.logs.map((l) => { try { return swap.interface.parseLog(l); } catch { return null; } }).find((l) => l?.name === "New");
      const s = [await owner.getAddress(), await claimer.getAddress(), c.commitment, r.commitment, ev.args.timeout1, ev.args.timeout2, ZERO, value, nonce];
      return { s, claimSecret: c.secret, refundSecret: r.secret, newGas: receipt.gasUsed };
    }

    const a = await create(1n);
    const setReadyGas = (await (await swap.setReady(a.s)).wait()).gasUsed;
    const claimGas = (await (await swap.connect(claimer).claim(a.s, a.claimSecret)).wait()).gasUsed;
    const b = await create(2n);
    const refundGas = (await (await swap.refund(b.s, b.refundSecret)).wait()).gasUsed;

    const g = (n) => n.toString().padStart(8);
    console.log("\n  XmzSwap gas (native coin):");
    console.log("    deploy    " + g(deployGas));
    console.log("    newSwap   " + g(a.newGas));
    console.log("    setReady  " + g(setReadyGas));
    console.log("    claim     " + g(claimGas));
    console.log("    refund    " + g(refundGas));
    console.log("    maker per-swap (newSwap+setReady): " + g(a.newGas + setReadyGas));
    console.log("    full happy path (newSwap+setReady+claim): " + g(a.newGas + setReadyGas + claimGas));
  });
});
