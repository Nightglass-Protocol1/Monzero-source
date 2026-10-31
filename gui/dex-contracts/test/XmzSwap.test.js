const { expect } = require("chai");
const { ethers } = require("hardhat");

const N = 0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141n;
const D1 = 3 * 3600; // timeout1 = start + 3 h
const D2 = 6 * 3600; // timeout2 = timeout1 + 6 h
const ZERO = ethers.ZeroAddress;

function secret() {
  const wallet = ethers.Wallet.createRandom();
  return { secret: wallet.privateKey, commitment: wallet.address };
}

async function increaseTo(timestamp) {
  await ethers.provider.send("evm_setNextBlockTimestamp", [Number(timestamp)]);
  await ethers.provider.send("evm_mine", []);
}

// The next transaction's block gets exactly this timestamp
async function nextBlockAt(timestamp) {
  await ethers.provider.send("evm_setNextBlockTimestamp", [Number(timestamp)]);
}

async function revertsWith(promise, contract, error) {
  try {
    const tx = await promise;
    if (tx?.wait) await tx.wait();
  } catch (e) {
    const data = e.data ?? e.error?.data ?? e.info?.error?.data;
    const parsed = data ? contract.interface.parseError(data) : null;
    expect(parsed?.name ?? e.message, `expected ${error}`).to.equal(error);
    return;
  }
  expect.fail(`expected revert ${error}, but it succeeded`);
}

// Resolves the transaction and checks that the swap contract emitted `name` (with `args`, if given)
async function emits(promise, contract, name, args) {
  const tx = await promise;
  const receipt = await tx.wait();
  const log = receipt.logs.map((l) => { try { return contract.interface.parseLog(l); } catch { return null; } }).find((l) => l?.name === name);
  expect(log, `expected event ${name}`).to.not.equal(undefined);
  if (args) expect([...log.args].map(String)).to.deep.equal(args.map(String));
  return tx;
}

describe("XmzSwap", function () {
  let swap, owner, claimer, relayer, stranger;

  beforeEach(async function () {
    [owner, claimer, relayer, stranger] = await ethers.getSigners();
    swap = await (await ethers.getContractFactory("XmzSwap")).deploy();
  });

  // Creates a swap and returns its struct (as the contract hashes it), id and secrets
  async function create({ asset = ZERO, value = ethers.parseEther("1"), from = owner, to = claimer, nonce = 1n, msgValue } = {}) {
    const c = secret();
    const r = secret();
    const toAddress = typeof to === "string" ? to : await to.getAddress();
    const tx = await swap
      .connect(from)
      .newSwap(toAddress, c.commitment, r.commitment, D1, D2, asset, value, nonce, {
        value: msgValue ?? (asset === ZERO ? value : 0n),
      });
    const receipt = await tx.wait();
    const ev = receipt.logs.map((l) => swap.interface.parseLog(l)).find((l) => l?.name === "New");
    const s = [await from.getAddress(), toAddress, c.commitment, r.commitment, ev.args.timeout1, ev.args.timeout2, asset, value, nonce];
    return { s, id: ev.args.swapId, claimSecret: c.secret, refundSecret: r.secret, t1: ev.args.timeout1, t2: ev.args.timeout2 };
  }

  describe("secret commitments", function () {
    it("accepts the secret behind an address and rejects any other", async function () {
      const a = secret();
      const b = secret();
      expect(await swap.secretMatches(a.secret, a.commitment)).to.equal(true);
      expect(await swap.secretMatches(b.secret, a.commitment)).to.equal(false);
    });

    it("rejects zero and out-of-range secrets", async function () {
      const a = secret();
      expect(await swap.secretMatches(ethers.ZeroHash, a.commitment)).to.equal(false);
      expect(await swap.secretMatches(ethers.toBeHex(N, 32), a.commitment)).to.equal(false);
      expect(await swap.secretMatches(ethers.toBeHex(N + 1n, 32), a.commitment)).to.equal(false);
    });

    it("matches secrets at the edges of the group", async function () {
      for (const k of [1n, 2n, N - 1n]) {
        const w = new ethers.Wallet(ethers.toBeHex(k, 32));
        expect(await swap.secretMatches(ethers.toBeHex(k, 32), w.address), `k=${k}`).to.equal(true);
      }
    });
  });

  describe("newSwap", function () {
    it("locks the native coin and records the swap as pending", async function () {
      const { id, s } = await create();
      expect(await swap.swaps(id)).to.equal(1n);
      expect(await swap.swapId(s)).to.equal(id);
      expect(await ethers.provider.getBalance(await swap.getAddress())).to.equal(ethers.parseEther("1"));
    });

    it("rejects zero value, zero addresses, zero durations and a wrong native amount", async function () {
      const c = secret().commitment;
      const v = ethers.parseEther("1");
      const ca = await claimer.getAddress();
      await revertsWith(swap.newSwap(ca, c, c, D1, D2, ZERO, 0, 1), swap, "ZeroValue");
      await revertsWith(swap.newSwap(ZERO, c, c, D1, D2, ZERO, v, 1, { value: v }), swap, "ZeroAddress");
      await revertsWith(swap.newSwap(ca, ZERO, c, D1, D2, ZERO, v, 1, { value: v }), swap, "ZeroAddress");
      await revertsWith(swap.newSwap(ca, c, ZERO, D1, D2, ZERO, v, 1, { value: v }), swap, "ZeroAddress");
      await revertsWith(swap.newSwap(ca, c, c, 0, D2, ZERO, v, 1, { value: v }), swap, "ZeroDuration");
      await revertsWith(swap.newSwap(ca, c, c, D1, 0, ZERO, v, 1, { value: v }), swap, "ZeroDuration");
      await revertsWith(swap.newSwap(ca, c, c, D1, D2, ZERO, v, 1, { value: v - 1n }), swap, "WrongNativeValue");
    });

    it("rejects an identical swap in the same block", async function () {
      const c = secret().commitment;
      const v = ethers.parseEther("1");
      const ca = await claimer.getAddress();
      await ethers.provider.send("evm_setAutomine", [false]);
      const first = await swap.newSwap(ca, c, c, D1, D2, ZERO, v, 7, { value: v, gasLimit: 500000 });
      const second = await swap.newSwap(ca, c, c, D1, D2, ZERO, v, 7, { value: v, gasLimit: 500000 });
      await ethers.provider.send("evm_mine", []);
      await ethers.provider.send("evm_setAutomine", [true]);
      expect((await first.wait()).status).to.equal(1);
      let secondFailed = false;
      try {
        await second.wait();
      } catch {
        secondFailed = true;
      }
      expect(secondFailed, "second identical swap must fail").to.equal(true);
    });
  });

  describe("happy path", function () {
    it("pays the claimer after setReady and publishes the secret", async function () {
      const { s, id, claimSecret } = await create();
      await emits(swap.setReady(s), swap, "Ready", [id]);
      const before = await ethers.provider.getBalance(await claimer.getAddress());
      const tx = await swap.connect(claimer).claim(s, claimSecret);
      const receipt = await tx.wait();
      const gas = receipt.gasUsed * receipt.gasPrice;
      expect(await ethers.provider.getBalance(await claimer.getAddress())).to.equal(before + ethers.parseEther("1") - gas);
      await emits(tx, swap, "Claimed", [id, claimSecret]);
      expect(await swap.swaps(id)).to.equal(3n);
    });

    it("lets the claimer claim after timeout1 when the owner never called setReady", async function () {
      const { s, claimSecret, t1 } = await create();
      await increaseTo(t1);
      await emits(swap.connect(claimer).claim(s, claimSecret), swap, "Claimed");
    });
  });

  describe("claim rules", function () {
    it("refuses a claim before setReady and before timeout1", async function () {
      const { s, claimSecret } = await create();
      await revertsWith(swap.connect(claimer).claim(s, claimSecret), swap, "TooEarly");
    });

    it("refuses a claim at timeout2 but allows one just before", async function () {
      const a = await create({ nonce: 1n });
      const b = await create({ nonce: 2n });
      await swap.setReady(a.s);
      await swap.setReady(b.s);
      await nextBlockAt(a.t2 - 1n);
      await emits(swap.connect(claimer).claim(a.s, a.claimSecret), swap, "Claimed");
      await nextBlockAt(b.t2 > a.t2 ? b.t2 : a.t2 + 1n);
      await revertsWith(swap.connect(claimer).claim(b.s, b.claimSecret), swap, "TooLate");
    });

    it("refuses a wrong secret, the refund secret, and anyone but the claimer", async function () {
      const { s, claimSecret, refundSecret } = await create();
      await swap.setReady(s);
      await revertsWith(swap.connect(claimer).claim(s, secret().secret), swap, "InvalidSecret");
      await revertsWith(swap.connect(claimer).claim(s, refundSecret), swap, "InvalidSecret");
      await revertsWith(swap.connect(stranger).claim(s, claimSecret), swap, "NotClaimer");
    });

    it("refuses a second claim and a claim on a changed swap", async function () {
      const { s, claimSecret } = await create();
      await swap.setReady(s);
      await swap.connect(claimer).claim(s, claimSecret);
      await revertsWith(swap.connect(claimer).claim(s, claimSecret), swap, "WrongStage");
      const altered = [...s];
      altered[7] = ethers.parseEther("2");
      await revertsWith(swap.connect(claimer).claim(altered, claimSecret), swap, "WrongStage");
    });
  });

  describe("setReady rules", function () {
    it("is owner-only, once, and only before timeout1", async function () {
      const a = await create({ nonce: 1n });
      await revertsWith(swap.connect(stranger).setReady(a.s), swap, "NotOwner");
      await swap.setReady(a.s);
      await revertsWith(swap.setReady(a.s), swap, "WrongStage");
      const b = await create({ nonce: 2n });
      await nextBlockAt(b.t1);
      await revertsWith(swap.setReady(b.s), swap, "TooLate");
    });
  });

  describe("refund rules", function () {
    it("refunds early while pending and publishes the secret", async function () {
      const { s, id, refundSecret } = await create();
      await emits(swap.refund(s, refundSecret), swap, "Refunded", [id, refundSecret]);
      expect(await ethers.provider.getBalance(await swap.getAddress())).to.equal(0n);
    });

    it("refuses an early refund once ready, and any refund between the timeouts", async function () {
      const ready = await create({ nonce: 1n });
      const pending = await create({ nonce: 2n });
      await swap.setReady(ready.s);
      await revertsWith(swap.refund(ready.s, ready.refundSecret), swap, "NotTimeToRefund");
      await increaseTo(pending.t1);
      await revertsWith(swap.refund(pending.s, pending.refundSecret), swap, "NotTimeToRefund");
    });

    it("refunds from timeout2 on, ready or not", async function () {
      const ready = await create({ nonce: 1n });
      const pending = await create({ nonce: 2n });
      await swap.setReady(ready.s);
      const t2 = ready.t2 > pending.t2 ? ready.t2 : pending.t2;
      await increaseTo(t2);
      await emits(swap.refund(ready.s, ready.refundSecret), swap, "Refunded");
      await emits(swap.refund(pending.s, pending.refundSecret), swap, "Refunded");
    });

    it("refuses a wrong secret, the claim secret, a stranger, and a refund after a claim", async function () {
      const { s, claimSecret, refundSecret } = await create();
      await revertsWith(swap.refund(s, secret().secret), swap, "InvalidSecret");
      await revertsWith(swap.refund(s, claimSecret), swap, "InvalidSecret");
      await revertsWith(swap.connect(stranger).refund(s, refundSecret), swap, "NotOwner");
      await swap.setReady(s);
      await swap.connect(claimer).claim(s, claimSecret);
      await revertsWith(swap.refund(s, refundSecret), swap, "WrongStage");
    });

    it("refuses a claim after a refund", async function () {
      const { s, claimSecret, refundSecret, t1 } = await create();
      await swap.refund(s, refundSecret);
      await increaseTo(t1);
      await revertsWith(swap.connect(claimer).claim(s, claimSecret), swap, "WrongStage");
    });
  });

  describe("tokens", function () {
    async function token(name) {
      const t = await (await ethers.getContractFactory(name)).deploy();
      await t.mint(await owner.getAddress(), 1000n);
      await t.approve(await swap.getAddress(), 1000n);
      return t;
    }

    it("swaps a standard token both ways", async function () {
      const t = await token("TestToken");
      const a = await create({ asset: await t.getAddress(), value: 100n, nonce: 1n });
      await swap.setReady(a.s);
      await swap.connect(claimer).claim(a.s, a.claimSecret);
      expect(await t.balanceOf(await claimer.getAddress())).to.equal(100n);
      const b = await create({ asset: await t.getAddress(), value: 50n, nonce: 2n });
      await swap.refund(b.s, b.refundSecret);
      expect(await t.balanceOf(await owner.getAddress())).to.equal(900n);
    });

    it("works with a token that returns nothing (USDT style)", async function () {
      const t = await token("NoReturnToken");
      const a = await create({ asset: await t.getAddress(), value: 100n });
      await swap.setReady(a.s);
      await swap.connect(claimer).claim(a.s, a.claimSecret);
      expect(await t.balanceOf(await claimer.getAddress())).to.equal(100n);
    });

    it("rejects a fee-on-transfer token and native coin sent with a token swap", async function () {
      const fee = await token("FeeToken");
      const c = secret().commitment;
      const ca = await claimer.getAddress();
      await revertsWith(swap.newSwap(ca, c, c, D1, D2, await fee.getAddress(), 100n, 1), swap, "FeeOnTransferToken");
      const t = await token("TestToken");
      await revertsWith(swap.newSwap(ca, c, c, D1, D2, await t.getAddress(), 100n, 1, { value: 1n }), swap, "WrongNativeValue");
    });

    it("rejects an asset address that is not a contract", async function () {
      const c = secret().commitment;
      await revertsWith(
        swap.newSwap(await claimer.getAddress(), c, c, D1, D2, await stranger.getAddress(), 100n, 1),
        swap,
        "TransferFailed",
      );
    });
  });

  describe("relayed claims", function () {
    async function relaySignature(s, fee, relayerAddress, signer = claimer) {
      const id = await swap.swapId(s);
      const { chainId } = await ethers.provider.getNetwork();
      const inner = ethers.keccak256(
        ethers.AbiCoder.defaultAbiCoder().encode(
          ["string", "uint256", "address", "bytes32", "uint256", "address"],
          ["XMZ-SWAP-RELAY-V1", chainId, await swap.getAddress(), id, fee, relayerAddress],
        ),
      );
      return ethers.Signature.from(await signer.signMessage(ethers.getBytes(inner)));
    }

    it("pays the named relayer its fee and the claimer the rest", async function () {
      const { s, claimSecret } = await create();
      await swap.setReady(s);
      const fee = ethers.parseEther("0.01");
      const sig = await relaySignature(s, fee, await relayer.getAddress());
      const claimerBefore = await ethers.provider.getBalance(await claimer.getAddress());
      const tx = await swap.connect(relayer).claimRelayer(s, claimSecret, fee, sig.v, sig.r, sig.s);
      await emits(tx, swap, "Claimed");
      expect(await ethers.provider.getBalance(await claimer.getAddress())).to.equal(claimerBefore + ethers.parseEther("1") - fee);
    });

    it("refuses a front-runner, a changed fee, a non-claimer signature and a fee above the value", async function () {
      const { s, claimSecret } = await create();
      await swap.setReady(s);
      const fee = ethers.parseEther("0.01");
      const sig = await relaySignature(s, fee, await relayer.getAddress());
      await revertsWith(swap.connect(stranger).claimRelayer(s, claimSecret, fee, sig.v, sig.r, sig.s), swap, "InvalidRelaySignature");
      await revertsWith(swap.connect(relayer).claimRelayer(s, claimSecret, fee * 2n, sig.v, sig.r, sig.s), swap, "InvalidRelaySignature");
      const bad = await relaySignature(s, fee, await relayer.getAddress(), stranger);
      await revertsWith(swap.connect(relayer).claimRelayer(s, claimSecret, fee, bad.v, bad.r, bad.s), swap, "InvalidRelaySignature");
      const big = ethers.parseEther("2");
      const sigBig = await relaySignature(s, big, await relayer.getAddress());
      await revertsWith(swap.connect(relayer).claimRelayer(s, claimSecret, big, sigBig.v, sigBig.r, sigBig.s), swap, "FeeTooHigh");
    });

    it("refuses the high-s twin of a valid signature", async function () {
      const { s, claimSecret } = await create();
      await swap.setReady(s);
      const sig = await relaySignature(s, 0n, await relayer.getAddress());
      const highS = ethers.toBeHex(N - BigInt(sig.s), 32);
      const flippedV = sig.v === 27 ? 28 : 27;
      await revertsWith(swap.connect(relayer).claimRelayer(s, claimSecret, 0n, flippedV, sig.r, highS), swap, "InvalidRelaySignature");
    });
  });

  describe("hostile receivers", function () {
    it("pays a re-entering claimer exactly once", async function () {
      const attacker = await (await ethers.getContractFactory("ReentrantClaimer")).deploy();
      const { s, claimSecret } = await create({ to: await attacker.getAddress() });
      await swap.setReady(s);
      const call = swap.interface.encodeFunctionData("claim", [s, claimSecret]);
      await attacker.arm(await swap.getAddress(), call);
      await attacker.call(await swap.getAddress(), call);
      expect(await attacker.reentered()).to.equal(true);
      expect(await attacker.reentryOk()).to.equal(false);
      expect(await ethers.provider.getBalance(await attacker.getAddress())).to.equal(ethers.parseEther("1"));
    });

    it("reverts a claim to an address that refuses the native coin, keeping the swap open", async function () {
      const rejecting = await (await ethers.getContractFactory("RejectEther")).deploy();
      const { s, id, claimSecret } = await create({ to: await rejecting.getAddress() });
      await swap.setReady(s);
      await revertsWith(
        rejecting.call(await swap.getAddress(), swap.interface.encodeFunctionData("claim", [s, claimSecret])),
        swap,
        "TransferFailed",
      );
      expect(await swap.swaps(id)).to.equal(2n);
    });
  });
});
