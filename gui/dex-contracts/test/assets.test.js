const { expect } = require("chai");
const { ethers } = require("hardhat");
const assets = require("../assets.json");

// chainIds must match hardhat.config.js / the recommended EVM set.
const EXPECTED = { bsc: 56, ethereum: 1, base: 8453, arbitrum: 42161, polygon: 137 };

describe("asset registry (assets.json)", function () {
  it("native sentinel is the zero address", function () {
    expect(assets.native).to.equal("0x0000000000000000000000000000000000000000");
  });

  for (const [name, chain] of Object.entries(assets.chains)) {
    describe(name, function () {
      it("has the expected chainId", function () {
        expect(chain.chainId).to.equal(EXPECTED[name]);
      });
      it("native symbol + decimals are sane", function () {
        expect(chain.nativeSymbol).to.be.a("string").and.not.equal("");
        expect(chain.nativeDecimals).to.be.a("number").that.is.greaterThan(0);
      });
      for (const t of chain.tokens) {
        it(`${t.symbol}: EIP-55 address, sane decimals, not yet verified`, function () {
          expect(t.address).to.equal(ethers.getAddress(t.address)); // throws/fails on bad checksum
          expect(t.address).to.not.equal(assets.native);
          expect(t.decimals).to.be.a("number").that.is.greaterThan(0);
          expect(t.verified, "stays false until confirmed off-line + pentest").to.equal(false);
        });
      }
    });
  }
});
