const { expect } = require("chai");
const { ethers } = require("hardhat");
const { XmzSwapClient, makeSecret, ZERO } = require("../connectors/XmzSwapClient");

// Proves the wallet-agnostic client drives the contract. The Hardhat signers stand in
// for any connected wallet (MetaMask / Ledger / WalletConnect) — all ethers Signers.
describe("XmzSwapClient (wallet-agnostic connector)", function () {
  let swap, owner, claimer;
  beforeEach(async function () {
    [owner, claimer] = await ethers.getSigners();
    swap = await (await ethers.getContractFactory("XmzSwap")).deploy();
    await swap.waitForDeployment();
  });

  it("drives a full native swap: lock -> confirm -> claim", async function () {
    const addr = await swap.getAddress();
    const ownerClient = new XmzSwapClient(addr, owner);
    const claimerClient = new XmzSwapClient(addr, claimer);
    const c = makeSecret(), r = makeSecret();
    const value = ethers.parseEther("1");

    const { swap: s, id } = await ownerClient.lock({
      claimer: await claimer.getAddress(), claimCommitment: c.commitment, refundCommitment: r.commitment,
      duration1: 3 * 3600, duration2: 6 * 3600, asset: ZERO, value, nonce: 1n,
    });
    expect(id).to.match(/^0x[0-9a-f]{64}$/);
    expect(await ownerClient.stage(s)).to.equal(1); // PENDING
    await ownerClient.confirm(s);
    expect(await ownerClient.stage(s)).to.equal(2); // READY

    const before = await ethers.provider.getBalance(await claimer.getAddress());
    await claimerClient.claim(s, c.secret);
    expect(await ownerClient.stage(s)).to.equal(3); // COMPLETED
    expect((await ethers.provider.getBalance(await claimer.getAddress())) > before).to.equal(true);
  });

  it("refunds via the client (early, while pending)", async function () {
    const addr = await swap.getAddress();
    const ownerClient = new XmzSwapClient(addr, owner);
    const c = makeSecret(), r = makeSecret();
    const { swap: s } = await ownerClient.lock({
      claimer: await claimer.getAddress(), claimCommitment: c.commitment, refundCommitment: r.commitment,
      duration1: 3 * 3600, duration2: 6 * 3600, asset: ZERO, value: ethers.parseEther("1"), nonce: 2n,
    });
    await ownerClient.refund(s, r.secret);
    expect(await ownerClient.stage(s)).to.equal(3); // COMPLETED
  });
});
