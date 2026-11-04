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

  it("swaps an ERC-20 token via the client (approve -> lock -> claim)", async function () {
    const addr = await swap.getAddress();
    const token = await (await ethers.getContractFactory("TestToken")).deploy();
    await token.waitForDeployment();
    const amount = 1000n;
    await (await token.mint(await owner.getAddress(), amount)).wait();

    const ownerClient = new XmzSwapClient(addr, owner);
    const claimerClient = new XmzSwapClient(addr, claimer);
    const c = makeSecret(), r = makeSecret();

    await ownerClient.approveToken(await token.getAddress(), amount);
    const { swap: sw } = await ownerClient.lock({
      claimer: await claimer.getAddress(), claimCommitment: c.commitment, refundCommitment: r.commitment,
      duration1: 3 * 3600, duration2: 6 * 3600, asset: await token.getAddress(), value: amount, nonce: 1n,
    });
    await ownerClient.confirm(sw);
    await claimerClient.claim(sw, c.secret);
    expect(await token.balanceOf(await claimer.getAddress())).to.equal(amount);
  });

  it("gasless relayed claim: claimer signs, relayer submits and takes the fee", async function () {
    const [, , relayer] = await ethers.getSigners();
    const addr = await swap.getAddress();
    const ownerClient = new XmzSwapClient(addr, owner);
    const claimerClient = new XmzSwapClient(addr, claimer);
    const relayerClient = new XmzSwapClient(addr, relayer);
    const c = makeSecret(), r = makeSecret();
    const value = ethers.parseEther("1"), fee = ethers.parseEther("0.01");

    const { swap: sw, id } = await ownerClient.lock({
      claimer: await claimer.getAddress(), claimCommitment: c.commitment, refundCommitment: r.commitment,
      duration1: 3 * 3600, duration2: 6 * 3600, asset: ZERO, value, nonce: 3n,
    });
    await ownerClient.confirm(sw);

    const signed = await claimerClient.signRelayClaim({ id, fee, relayer: await relayer.getAddress() });
    const claimerBefore = await ethers.provider.getBalance(await claimer.getAddress());
    const relayerBefore = await ethers.provider.getBalance(await relayer.getAddress());
    await relayerClient.claimRelayer(sw, c.secret, signed);

    // claimer (who signed but did not send a tx) gains value - fee; relayer nets ~fee minus gas
    expect((await ethers.provider.getBalance(await claimer.getAddress())) - claimerBefore).to.equal(value - fee);
    expect((await ethers.provider.getBalance(await relayer.getAddress())) > relayerBefore).to.equal(true);
    expect(await ownerClient.stage(sw)).to.equal(3);
  });
});
