.pragma library

// Display-only fixture scaling. Never use floating-point demo prices for signing,
// settlement, fee calculation or a live price source.
function price(lastPrice, fixturePrice) {
    var anchor = Number(lastPrice);
    var sample = Number(fixturePrice);
    if (!isFinite(anchor) || anchor <= 0 || !isFinite(sample) || sample <= 0)
        return "—";
    return (anchor * sample / 0.08421).toFixed(5);
}
