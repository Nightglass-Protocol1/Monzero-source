package com.m2049r.xmrwallet.dex;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.json.JSONObject;
import org.junit.Test;

import java.math.BigInteger;

/** Monero pairs on the phone: prices per XMR (12 places), pair names, parts, the trade's own amounts. */
public class DexXmrTest {
    @Test
    public void pricesAreCoinPerXmrWithTwelvePlaces() {
        // 0.51 BNB for 1 XMR, and 0.0306 BNB for 0.06 XMR: both 0.51 BNB per XMR
        assertEquals("0.51", DexMarket.priceText("510000000000000000", "1000000000000", 18, 12));
        assertEquals("0.51", DexMarket.priceText("30600000000000000", "60000000000", 18, 12));
        // XMZ keeps 11 places: 0.0000121 SOL per XMZ
        assertEquals("0.0000121", DexMarket.priceText("24200000", "200000000000000", 9, 11));
    }

    @Test
    public void moneroPairsAreNamedByTheirCoin() {
        assertEquals("BNB/XMR", DexConfig.xmrPair(56));
        assertEquals("BNB/XMZ", DexConfig.pair(56));
        assertTrue(DexConfig.xmrChains().length >= 2);
        for (long c : DexConfig.xmrChains()) assertTrue(DexConfig.xmrPair(c).endsWith("/XMR"));
    }

    @Test
    public void aPartOfAMoneroOfferIsPricedLikeTheEngine() throws Exception {
        // 0.06 XMR for 0.0306 BNB, parts from 0.006 XMR: 0.03 XMR is 0.0153 BNB; less than the smallest part is refused
        final JSONObject l = new JSONObject().put("xmzAtoms", "60000000000").put("valueWei", "30600000000000000")
                .put("minXmzAtoms", "6000000000").put("side", "sell-xmz");
        assertEquals("15300000000000000", DexEngine.partValue(l, new BigInteger("30000000000")));
        assertNull(DexEngine.partValue(l, new BigInteger("5000000000")));
    }

    @Test
    public void aMoneroTradeShowsItsAmountInXmr() throws Exception {
        final JSONObject xmr = new JSONObject().put("xmzAmount", "60000000000").put("privateCoin", "XMR").put("privateDecimals", 12);
        assertEquals("0.06 XMR", DexText.xmzText(xmr));
        final JSONObject xmz = new JSONObject().put("xmzAmount", "200000000000000");
        assertEquals("2000 XMZ", DexText.xmzText(xmz));
    }
}
