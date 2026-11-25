package com.m2049r.xmrwallet.dex;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;

import java.math.BigInteger;
import java.util.List;

/** The Swap screen's plan: the same cases as the desktop wallet's Exchange page tests (SAMPLE offers). */
public class DexSwapPlanTest {
    private static final long X = DexSwapPlan.XMZ;

    private static JSONObject offer(String id, String side, long lamports, long xmz, long minXmz, String maker) throws Exception {
        final JSONObject o = new JSONObject().put("id", id).put("pair", "SOL/XMZ").put("side", side).put("valueWei", String.valueOf(lamports))
                .put("xmzAtoms", String.valueOf(xmz * X)).put("maker", maker).put("expiresAt", 9_000_000_000L);
        if (minXmz > 0) o.put("minXmzAtoms", String.valueOf(minXmz * X));
        return o;
    }

    private static JSONArray book() throws Exception {
        return new JSONArray()
                .put(offer("0xa1", "sell-xmz", 215_039_000L, 1500, 0, "0xb0"))
                .put(offer("0xa2", "sell-xmz", 300_000_000L, 2000, 100, "0xb0"))
                .put(offer("0xa3", "sell-xmz", 400_000_000L, 1000, 0, "0xme"))
                .put(offer("0xb1", "buy-xmz", 180_000_000L, 1500, 100, "0xb0"))
                .put(offer("0xb2", "buy-xmz", 50_000_000L, 500, 0, "0xb0"));
    }

    @Test
    public void buyFillsFromTheBestOffersAndSkipsOurOwn() throws Exception {
        final List<DexSwapPlan.Row> asks = DexSwapPlan.book(book(), "SOL", "buy", "0xME", 1, 9);
        assertEquals(2, asks.size());
        final DexSwapPlan.Plan p = DexSwapPlan.plan(asks, 2500 * X);
        assertEquals(2, p.legs.size());
        assertEquals("0xa1", p.legs.get(0).row.id);
        assertEquals("", p.legs.get(0).takeText());
        assertEquals("0xa2", p.legs.get(1).row.id);
        assertEquals("1000", p.legs.get(1).takeText());
        assertEquals(BigInteger.valueOf(215_039_000L + 150_000_000L), p.coin);
        assertFalse(p.shortOf);
    }

    @Test
    public void aWholeOfferIsSkippedWhenItDoesNotFit() throws Exception {
        final DexSwapPlan.Plan p = DexSwapPlan.plan(DexSwapPlan.book(book(), "SOL", "buy", "0xme", 1, 9), 500 * X);
        assertEquals(1, p.legs.size());
        assertEquals("0xa2", p.legs.get(0).row.id);
        assertEquals("500", p.legs.get(0).takeText());
    }

    @Test
    public void notEnoughOfferedSaysHowMuchThereIs() throws Exception {
        final DexSwapPlan.Plan p = DexSwapPlan.plan(DexSwapPlan.book(book(), "SOL", "buy", "0xme", 1, 9), 100_000 * X);
        assertTrue(p.shortOf);
        assertEquals(3500 * X, p.atoms);
    }

    @Test
    public void sellGoesToTheBestBidAndCountsTheLockFees() throws Exception {
        final DexSwapPlan.Plan p = DexSwapPlan.plan(DexSwapPlan.book(book(), "SOL", "sell", "0xme", 1, 9), 1500 * X);
        assertEquals("0xb1", p.legs.get(0).row.id);
        assertEquals(1515 * X, DexSwapPlan.sellNeed(p.atoms, p.legs.size()));
    }

    @Test
    public void stepsMatchTheDesktopTicks() {
        assertEquals(2, DexSwapPlan.step("owner", "Locked")[0]);
        assertEquals(4, DexSwapPlan.step("owner", "Done(Swapped)")[0]);
        assertEquals(1, DexSwapPlan.step("owner", "Done(Refunded)")[1] == 1 ? 1 : 0);
        assertEquals("SOL", DexSwapPlan.feeCoin("USDT"));
        assertEquals("BNB", DexSwapPlan.feeCoin("USDT-BSC"));
    }
}
