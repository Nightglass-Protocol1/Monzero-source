// Approximate expected rewards, not a prediction of actual pool payouts.
export function estimateMining(input) {
  const number = (name, minimum, maximum) => {
    const value = input[name];
    if (typeof value !== 'number' || !Number.isFinite(value) || value < minimum || value > maximum)
      throw new RangeError(`Invalid ${name}`);
    return value;
  };
  const units = { 'H/s': 1, 'KH/s': 1000, 'MH/s': 1000000 };
  if (!Object.hasOwn(units, input.unit)) throw new RangeError('Invalid hashrate unit');
  const hashrate = number('hashrate', 0, 1e15) * units[input.unit];
  const difficulty = number('difficulty', Number.MIN_VALUE, Number.MAX_VALUE);
  const reward = number('rewardXmz', 0, 100000000);
  const watts = number('watts', 0, 1e9);
  const electricityPrice = number('electricityPrice', 0, 1e6);
  const fee = number('poolFeePercent', 0, 100) / 100;
  // H/s * seconds / expected hashes per block. This assumes unchanged difficulty.
  const xmzPerHour = hashrate * 3600 / difficulty * reward * (1 - fee);
  const kwhPerDay = watts * 24 / 1000;
  const electricityPerDay = kwhPerDay * electricityPrice;
  const market = input.market ?? null;
  let revenuePerDay = null, netPerDay = null, breakEvenDays = null;
  if (market !== null) {
    // Only a validated source adapter may supply a market object to this module.
    if (market.verified !== true || typeof market.source !== 'string' || !market.source.trim()
        || typeof market.currency !== 'string' || !market.currency.trim()
        || typeof market.observedAt !== 'number' || !Number.isFinite(market.observedAt) || market.observedAt <= 0
        || typeof market.price !== 'number' || !Number.isFinite(market.price) || market.price <= 0)
      throw new RangeError('A sourced market price is required');
    if (input.electricityCurrency !== market.currency) throw new RangeError('Currency mismatch');
    revenuePerDay = xmzPerHour * 24 * market.price;
    netPerDay = revenuePerDay - electricityPerDay;
  }
  if (input.hardwareCost !== undefined && input.hardwareCost !== null) {
    const cost = number('hardwareCost', 0, 1e15);
    if (netPerDay !== null && netPerDay > 0 && cost > 0) breakEvenDays = cost / netPerDay;
  }
  const result = { xmzPerHour, xmzPerDay: xmzPerHour * 24, xmzPerWeek: xmzPerHour * 168,
    xmzPerMonth: xmzPerHour * 720, kwhPerDay, electricityPerDay,
    revenuePerDay, netPerDay, breakEvenDays };
  if (Object.values(result).some(value => value !== null && !Number.isFinite(value)))
    throw new RangeError('Estimate outside supported range');
  return result;
}
