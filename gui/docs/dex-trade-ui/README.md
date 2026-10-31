# DEX trade screen: pictures

| File | What it shows |
| --- | --- |
| `app-trade-page-finished-trade-no-offers.png` | The real app (testnet): a finished BNB trade on top, the BTC/XMZ market with no offers loaded yet |
| `sample-1-depth-chart-order-book-and-sell-quote.png` | Market panel (depth chart, order book) and a Sell XMZ quote |
| `sample-2-price-chart.png` | The price chart of completed trades |
| `sample-3-buy-review.png` | Buy XMZ, review step |
| `sample-4-active-trade-approval.png` | An active trade waiting for the one approval |
| `sample-5-active-trade-in-progress.png` | An active trade part-way through (seller's view) |

**`sample-*` pictures use a made-up order book and trade history** (in `tests/qml/tst_DexMarket.qml`)
so the chart and book have something to show; they are not market prices. The sample levels sit
around the starting point chosen on 2026-10-31, **$0.001 per XMZ**: with BTC at about $77,000 that
is about 77,000,000 XMZ per BTC (and with BNB at about $700, 700,000 XMZ per BNB). They are rendered by
the app's own components. Regenerate them with the QML tests (`--test-qml`), which save them to
`build/dex-trade-ui/`.
