package com.m2049r.xmrwallet.dex;

import org.json.JSONArray;
import org.json.JSONObject;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The Swap screen's arithmetic (the same rules as the desktop wallet's dexExchange.js planSwap):
 * buy or sell an amount of XMZ across as many offers as it takes, best price first. A whole offer
 * is taken only when all of it fits what is left; a partial one from its minimum, leaving none or
 * at least the minimum. Amounts in atoms (XMZ: 1e11), coin in its smallest unit.
 */
final class DexSwapPlan {
    static final long XMZ = 100_000_000_000L;

    static final class Row {
        final String id;
        final long atoms, minAtoms;     // minAtoms 0: whole offers only
        final BigInteger value;         // the coin for all of it
        final double price;             // coin per XMZ (in whole coins)
        final String maker;

        Row(String id, long atoms, long minAtoms, BigInteger value, double price, String maker) {
            this.id = id;
            this.atoms = atoms;
            this.minAtoms = minAtoms;
            this.value = value;
            this.price = price;
            this.maker = maker;
        }

        boolean partial() {
            return minAtoms > 0;
        }

        /** The engine's rule for a take */
        boolean fillOk(long take) {
            if (take <= 0 || take > atoms) return false;
            if (take == atoms) return true;
            return partial() && take >= minAtoms && atoms - take >= minAtoms;
        }

        /** The coin for `take` atoms, rounded down (what the screen shows; the engine rounds for the maker) */
        BigInteger coinFor(long take) {
            return take == atoms ? value : value.multiply(BigInteger.valueOf(take)).divide(BigInteger.valueOf(atoms));
        }
    }

    static final class Leg {
        final Row row;
        final long atoms;
        final BigInteger coin;

        Leg(Row row, long atoms, BigInteger coin) {
            this.row = row;
            this.atoms = atoms;
            this.coin = coin;
        }

        /** The amount the engine's take wants: "" for all of a whole offer, else whole XMZ text */
        String takeText() {
            return atoms == row.atoms ? "" : DexFiles.units(String.valueOf(atoms), 11);
        }
    }

    static final class Plan {
        final List<Leg> legs = new ArrayList<>();
        long atoms;                      // what the legs fill
        BigInteger coin = BigInteger.ZERO;
        boolean shortOf;                 // the offers do not fill all of it

        double coinWhole(int decimals) {
            return new java.math.BigDecimal(coin).movePointLeft(decimals).doubleValue();
        }
    }

    /**
     * One side of a pair's book from the market's listings ("buy": the asks, offers selling XMZ;
     * "sell": the bids), best price first, without our own offers and expired ones.
     */
    static List<Row> book(JSONArray listings, String coin, String side, String myAccount, long now, int decimals) {
        final List<Row> rows = new ArrayList<>();
        final String pair = coin + "/XMZ", wanted = side.equals("buy") ? "sell-xmz" : "buy-xmz";
        final String me = myAccount == null ? "" : myAccount.toLowerCase();
        for (int i = 0; listings != null && i < listings.length(); i++) {
            final JSONObject l = listings.optJSONObject(i);
            if (l == null || !pair.equals(l.optString("pair").replace(" ", "")) || !wanted.equals(l.optString("side"))) continue;
            if (now > 0 && l.optLong("expiresAt") <= now) continue;
            if (!me.isEmpty() && me.equals(l.optString("maker").toLowerCase())) continue;
            try {
                final long atoms = Long.parseLong(l.optString("xmzAtoms"));
                final BigInteger value = new BigInteger(l.optString("valueWei"));
                if (atoms <= 0 || value.signum() <= 0) continue;
                long min = l.has("minXmzAtoms") ? Long.parseLong(l.optString("minXmzAtoms")) : 0;
                if (min >= atoms) min = 0;
                final double price = new java.math.BigDecimal(value).movePointLeft(decimals).doubleValue() / ((double) atoms / XMZ);
                rows.add(new Row(l.optString("id"), atoms, min, value, price, l.optString("maker")));
            } catch (Exception ignored) {
            }
        }
        final boolean buy = side.equals("buy");
        Collections.sort(rows, (a, b) -> {
            final int byPrice = buy ? Double.compare(a.price, b.price) : Double.compare(b.price, a.price);
            return byPrice != 0 ? byPrice : Long.compare(b.atoms, a.atoms);
        });
        return rows;
    }

    static Plan plan(List<Row> rows, long atoms) {
        final Plan p = new Plan();
        long left = atoms;
        for (Row r : rows) {
            if (left <= 0) break;
            long take = Math.min(left, r.atoms);
            if (!r.fillOk(take)) {
                // A partial offer that would be left with less than its minimum: take less of it
                if (r.partial() && take < r.atoms && r.atoms - take < r.minAtoms && r.fillOk(r.atoms - r.minAtoms)) take = r.atoms - r.minAtoms;
                else continue;
            }
            final BigInteger c = r.coinFor(take);
            p.legs.add(new Leg(r, take, c));
            p.coin = p.coin.add(c);
            p.atoms += take;
            left -= take;
        }
        p.shortOf = left > 0;
        return p;
    }

    /** The coin a buyer's trade account keeps aside per trade for network fees (as the desktop wallet): {coin, amount} */
    static String feeCoin(String coin) {
        return coin.equals("SOL") || coin.equals("USDT") ? "SOL" : coin.equals("BNB") || coin.equals("USDT-BSC") ? "BNB" : coin;
    }

    static double feeReserve(String coin) {
        final String c = feeCoin(coin);
        return c.equals("SOL") ? 0.0045 : c.equals("BNB") ? 0.0015 : c.equals("ETH") ? 0.0045 : 0;
    }

    /** XMZ the trading wallet must hold unlocked to sell `atoms` in `legs` trades (1%, at least 1 XMZ per trade, for the lock fees) */
    static long sellNeed(long atoms, int legs) {
        return atoms + Math.max(atoms / 100, legs * XMZ);
    }

    /** Where a trade is, as four steps (agreed, first lock, second lock, coin arrives): {step 0..4, refund 0/1, finished 0/1} */
    static int[] step(String role, String stage) {
        final boolean owner = "owner".equals(role);
        switch (stage == null ? "" : stage) {
            case "Start": return owner ? new int[]{1, 0, 0} : new int[]{0, 0, 0};
            case "LockSent": return new int[]{1, 0, 0};
            case "Locked": return new int[]{2, 0, 0};
            case "ReadySent": case "Ready": case "Sweeping": return new int[]{3, 0, 0};
            case "WaitingForEvmLock": return new int[]{1, 0, 0};
            case "XmzLockSent": case "XmzLocked": return new int[]{2, 0, 0};
            case "ClaimSent": return new int[]{3, 0, 0};
            case "Done(Swapped)": return new int[]{4, 0, 1};
            case "RefundSent": return new int[]{2, 1, 0};
            case "WaitingForRefund": case "SweepingBack": return new int[]{3, 1, 0};
            case "Done(Refunded)": return new int[]{owner ? 2 : 3, 1, 1};
            case "Done(Aborted)": return new int[]{1, 1, 1};
            default: return new int[]{0, 0, 0};
        }
    }
}
