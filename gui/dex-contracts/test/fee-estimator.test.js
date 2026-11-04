const { expect } = require("chai");
const { dynamicSpread, spreadLabel, minTradeSats } = require("../connectors/fee");

// Must agree with the Rust engine's DynamicSpread tests (floor/cap/coverage/zero).
describe("fee estimator (canonical dynamic spread)", function () {
  it("large trade hits the 0.1% floor", function () {
    expect(dynamicSpread(1e8)).to.equal(0.001); // 1 BTC: 8000/1e8 = 0.00008 < min
  });
  it("small trade hits the 0.5% cap", function () {
    expect(dynamicSpread(1e6)).to.equal(0.005); // 0.01 BTC: 8000/1e6 = 0.008 > max
  });
  it("mid trade covers the on-chain cost", function () {
    expect(dynamicSpread(4e6)).to.be.closeTo(0.002, 1e-9); // 0.04 BTC: 8000/4e6
  });
  it("zero/negative uses the cap, never throws", function () {
    expect(dynamicSpread(0)).to.equal(0.005);
    expect(dynamicSpread(-5)).to.equal(0.005);
  });
  it("label formats as a percent", function () {
    expect(spreadLabel(1e8)).to.equal("0.10%");
    expect(spreadLabel(1e6)).to.equal("0.50%");
  });
  it("minimum trade size = where the cap just covers on-chain cost", function () {
    expect(minTradeSats()).to.equal(Math.ceil((4000 * 2) / 0.005)); // 1,600,000 sats = 0.016 BTC
  });
});
