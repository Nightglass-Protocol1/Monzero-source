#!/usr/bin/env python3
"""Print the sign-control.py commands for the DEX opening limits at today's prices.

  opening-limits.py [--per-trade-usd 25] [--per-day-usd 250] [--xmz-usd 0.001] \
      [--sequence N] [--days 7] [--key KEYFILE] [--networks eth-mainnet,bnb-mainnet]

--networks opens only those pairs (for example the two MetaMask coins first); every other
network has no document, so the public app starts no trade there.

The owner's decision of 8 Nov 2026: $25 per trade and $250 per day for each pair, set in the
signed control document of each network (btc-mainnet, eth-mainnet, bnb-mainnet). Prices come
from monzero.org/markets/api.php (CoinGecko); XMZ uses the DEX opening rate. Run the printed
commands with the owner's control key, then publish each output as
monzero.org/dex/control/<network>.json, where the public app edition fetches it.

Nothing is signed or sent by this script.
"""
import argparse, json, urllib.request
from decimal import Decimal, ROUND_DOWN

NETWORKS = [  # network, coin symbol, atomic units per coin
    ("btc-mainnet", "BTC", 10**8),
    ("eth-mainnet", "ETH", 10**18),
    ("bnb-mainnet", "BNB", 10**18),
]
XMZ_ATOMS = 10**11


def prices():
    req = urllib.request.Request("https://monzero.org/markets/api.php?only=coins", headers={"User-Agent": "monzero-opening-limits"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return {c["symbol"]: Decimal(str(c["usd"])) for c in json.load(r)["coins"] if c.get("usd")}


def atoms(usd, coin_usd, per_coin):
    return int((Decimal(usd) / coin_usd * per_coin).to_integral_value(rounding=ROUND_DOWN))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--per-trade-usd", type=Decimal, default=Decimal(25))
    p.add_argument("--per-day-usd", type=Decimal, default=Decimal(250))
    p.add_argument("--xmz-usd", type=Decimal, default=Decimal("0.001"))
    p.add_argument("--sequence", type=int, required=True, help="higher than every document published before")
    p.add_argument("--days", type=float, default=7)
    p.add_argument("--networks", default="", help="comma-separated networks to open, e.g. eth-mainnet,bnb-mainnet "
                   "for the MetaMask coins only; a network without a published document stays closed (default: all)")
    p.add_argument("--key", default="~/monzero-private/dex-control-btc-mainnet", help="the owner's control key file")
    a = p.parse_args()
    usd = prices()
    xmz_per_trade = atoms(a.per_trade_usd, a.xmz_usd, XMZ_ATOMS)
    print(f"# ${a.per_trade_usd} per trade, ${a.per_day_usd} per day; XMZ at ${a.xmz_usd} -> at most "
          f"{Decimal(xmz_per_trade) / XMZ_ATOMS} XMZ per trade")
    wanted = [n for n in a.networks.split(",") if n]
    unknown = [n for n in wanted if n not in [x[0] for x in NETWORKS]]
    if unknown:
        p.error("unknown network: " + ", ".join(unknown))
    for network, coin, per_coin in NETWORKS:
        if wanted and network not in wanted:
            print(f"# {network}: not opened (no document; the app refuses new {coin} trades)")
            continue
        if coin not in usd:
            print(f"# {network}: no {coin} price; skipped")
            continue
        trade, day = atoms(a.per_trade_usd, usd[coin], per_coin), atoms(a.per_day_usd, usd[coin], per_coin)
        print(f"# {network}: {coin} at ${usd[coin]} -> {Decimal(trade) / per_coin} {coin} per trade, {Decimal(day) / per_coin} {coin} per day")
        print(f"python3 gui/tools/dex-control/sign-control.py {a.key} --network {network} --sequence {a.sequence} "
              f"--days {a.days:g} --enabled --max-ltc-per-trade {trade} --max-xmz-per-trade {xmz_per_trade} "
              f"--max-ltc-per-day {day} > {network}.json")


if __name__ == "__main__":
    main()
