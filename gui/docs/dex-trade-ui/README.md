# DEX trade screen: pictures

| File | What it shows |
| --- | --- |
| `app-trade-page-finished-trade-no-offers.png` | The real app (testnet): a finished BNB trade on top, the BTC/XMZ market with no offers loaded yet |
| `sample-1-exchange-candles-1h-buy.png` | Exchange layout: pair tabs, stats bar, 1-hour candles with volume, order book, Buy form with quote |
| `sample-2-exchange-candles-4h-sell.png` | 4-hour candles and the Sell form |
| `sample-3-exchange-depth-buy-review.png` | Depth chart and the Buy review step |
| `sample-4-exchange-active-trade-approval.png` | An active trade waiting for its one approval, above the market |

**`sample-*` pictures use a made-up order book and trade history** (in `tests/qml/tst_DexMarket.qml`)
so the candles, chart and book have something to show; they are not market prices. The sample levels sit
around the starting point chosen on 2026-10-31, **$0.001 per XMZ**: with BTC at about $77,000 that
is about 77,000,000 XMZ per BTC (and with BNB at about $700, 700,000 XMZ per BNB). They are rendered by
the app's own components. Regenerate them with the QML tests (`--test-qml`), which save them to
`build/dex-trade-ui/`.
