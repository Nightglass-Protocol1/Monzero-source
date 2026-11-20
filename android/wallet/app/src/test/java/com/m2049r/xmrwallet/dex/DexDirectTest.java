package com.m2049r.xmrwallet.dex;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The phone's direct (coin <-> coin) market against a stand-in helper: offers shown in book terms,
 * a take that agrees, the approval of exactly the terms shown, steps until it swapped, the payout on
 * the chain the coin came in on, and our own offer posted and withdrawn. SAMPLE values only.
 */
public class DexDirectTest {
    @Rule
    public TemporaryFolder tmp = new TemporaryFolder();

    private static final String MAKER = "0x5138775820628775ce6d90b7fee72c854d18cbf6", SEED = "11".repeat(32);
    private final List<JSONObject> requests = new ArrayList<>();

    private static JSONObject coin(long chain, String hex) throws Exception {
        return new JSONObject().put("chainId", chain).put("contract", "0x" + "11".repeat(20)).put("asset", "0x" + "00".repeat(20)).put("value", hex);
    }

    private static JSONObject listing(String id) throws Exception {
        // 0.5 BNB (testnet 97) offered for 0.01 ETH (Sepolia)
        final JSONObject l = new JSONObject().put("schema", "monzero-dex-direct-listing/1").put("id", id).put("pair", "BNB/ETH").put("maker", MAKER)
                .put("mailbox", "0x" + "bb".repeat(32)).put("gives", coin(97, "0x6f05b59d3b20000")).put("wants", coin(11155111, "0x2386f26fc10000"))
                .put("createdAt", 1800000000L).put("expiresAt", 4000000000L);
        return new JSONObject().put("listing", l).put("signature", "0x00");
    }

    private static JSONObject trade(String stage, boolean done) throws Exception {
        return new JSONObject().put("ok", true).put("direct", true).put("role", "taker").put("stage", stage).put("done", done)
                .put("commitment", "0x" + "cc".repeat(32)).put("tradeId", "0x" + "dd".repeat(32))
                .put("gives", new JSONObject().put("chainId", 11155111).put("coin", "ETH").put("value", "10000000000000000"))
                .put("gets", new JSONObject().put("chainId", 97).put("coin", "BNB").put("value", "500000000000000000"));
    }

    /** Answers like the helper; makes the folder a post or take makes. */
    private JSONObject answer(JSONObject r) throws Exception {
        requests.add(r);
        final String dir = r.optString("tradeDir");
        switch (r.getString("command")) {
            case "directListings":
                return new JSONObject().put("ok", true).put("listings", new JSONArray()
                        .put(new JSONObject().put("signed", listing("0x" + "aa".repeat(32))).put("relays", new JSONArray().put("r1").put("r2"))));
            case "directTake":
                new File(dir).mkdirs();
                DexFiles.write(new File(dir, "direct-take.json"), new JSONObject().put("listing", listing("0x" + "aa".repeat(32))).toString());
                DexFiles.write(new File(dir, "direct-journal.json"), "{}");
                return trade("Start", false).put("agreed", true);
            case "directApprove":
                return trade("Start", false);
            case "directTick":
                return trade("Done(Swapped)", true);
            case "directPayout":
                return new JSONObject().put("ok", true).put("payout", new JSONObject().put("to", MAKER).put("tx", "0x" + "ee".repeat(32)).put("amount", "500000000000000000"));
            case "directPost":
                new File(dir).mkdirs();
                DexFiles.write(new File(dir, "direct-listing.json"), new JSONObject().put("listing", listing("0x" + "ff".repeat(32))).put("closed", false).toString());
                return new JSONObject().put("ok", true).put("posted", 1);
            case "directCancel":
                return new JSONObject().put("ok", true).put("cancelled", true);
            case "directServe":
                return new JSONObject().put("ok", true).put("taken", false);
        }
        return new JSONObject().put("ok", false).put("error", "unknown");
    }

    private DexDirect market(File dir) {
        final Map<Long, String> rpcs = new LinkedHashMap<>();
        rpcs.put(11155111L, "https://sepolia.example");
        rpcs.put(97L, "https://bsc-testnet.example");
        return new DexDirect(dir, rpcs, Arrays.asList("https://relay.example"), false, "", (request, done) -> {
            try {
                done.done(answer(request));
            } catch (Exception e) {
                done.done(null);
            }
        }, () -> { });
    }

    private static JSONObject mine(DexDirect d, String kind) {
        final JSONArray all = d.status("", true).optJSONArray("mine");
        for (int i = 0; i < all.length(); i++) if (kind.equals(all.optJSONObject(i).optString("kind"))) return all.optJSONObject(i);
        return null;
    }

    @Test
    public void offersTakesTradesAndPayouts() throws Exception {
        final File dir = tmp.newFolder("direct");
        final DexDirect d = market(dir);
        assertEquals("BNB/ETH", d.status("", true).getJSONArray("pairs").getString(0));
        assertTrue(d.refreshListings());
        final JSONObject l = d.status("", true).getJSONArray("listings").getJSONObject(0);
        assertEquals("sell", l.getString("side"));   // it gives the pair's base (BNB)
        assertEquals("0.5", l.getString("base"));
        assertEquals("0.01", l.getString("quote"));
        assertEquals("0.02", l.getString("price"));
        assertEquals(2, l.getInt("relays"));
        // Nothing that spends runs without the keys
        assertFalse(d.take(null, l.getString("id")));
        assertFalse(d.step(null));
        assertTrue(d.take(SEED, l.getString("id")));
        assertFalse(d.take(SEED, l.getString("id")));   // once
        JSONObject t = mine(d, "take");
        assertEquals("agreed", t.getString("state"));
        assertEquals("ETH", t.getJSONObject("gives").getString("coin"));   // we give what the maker wants
        // Only the terms shown are approved, and approval carries no keys
        assertFalse(d.approve(t.getString("folder"), "0x" + "99".repeat(32)));
        assertTrue(d.approve(t.getString("folder"), "0x" + "cc".repeat(32)));
        final JSONObject approve = requests.get(requests.size() - 1);
        assertEquals("directApprove", approve.getString("command"));
        assertFalse(approve.getJSONObject("direct").has("walletSeedHex"));
        assertFalse(d.payout(SEED, t.getString("folder"), MAKER));   // not finished yet
        assertTrue(d.step(SEED));
        t = mine(d, "take");
        assertEquals("done", t.getString("state"));
        assertFalse(d.step(SEED));   // a finished trade needs no more steps
        // The BNB came in on chain 97: an EVM address there, not a Solana one
        assertEquals(97, d.payoutChain(t.getString("folder")));
        assertFalse(d.payout(SEED, t.getString("folder"), "7xKXtg2CW87d97TXJSDpbD5jBkheTqA83TZRuJosgAsU"));
        assertTrue(d.payout(SEED, t.getString("folder"), MAKER));
        final JSONObject pay = requests.get(requests.size() - 1);
        assertEquals(97, pay.getLong("chainId"));
        assertEquals("500000000000000000", d.status("", true).getJSONObject("payout").getString("amount"));
        // Our own offer: posted, served, withdrawn
        assertFalse(d.post(SEED, 97, "1000", 97, "2000", 3600));          // one coin for itself
        assertFalse(d.post(SEED, 97, "1000", DexConfig.SOL_DEVNET, "2000", 3600));   // no node for that chain here
        assertTrue(d.post(SEED, 97, "1000", 11155111, "2000", 3600));
        final JSONObject offer = mine(d, "offer");
        assertEquals("open", offer.getString("state"));
        assertTrue(d.step(SEED));
        assertTrue(d.cancel(SEED, offer.getString("folder")));
        assertEquals("cancelled", mine(d, "offer").getString("state"));
        // Every keyed request carried both nodes and the keys; none switched real coins on
        for (JSONObject r : requests) {
            assertFalse(r.has("directMainnet"));
            assertEquals(2, r.getJSONObject("direct").getJSONObject("rpcs").length());
            final String c = r.getString("command");
            if (!c.equals("directListings") && !c.equals("directApprove")) assertEquals(SEED, r.getJSONObject("direct").getString("walletSeedHex"));
        }
        // After a restart: the trade and the withdrawn offer are found again
        final DexDirect again = market(dir);
        assertEquals("agreed", mine(again, "take").getString("state"));   // its next step reads it as finished
        assertEquals("cancelled", mine(again, "offer").getString("state"));
        assertTrue(again.hasRunningTrade());
    }

    @Test
    public void usdtCoinsByMarketId() throws Exception {
        final Map<Long, String> rpcs = new LinkedHashMap<>();
        rpcs.put(11155111L, "https://sepolia.example");
        rpcs.put(97L, "https://bsc-testnet.example");
        final List<JSONObject> sent = new ArrayList<>();
        final DexDirect d = new DexDirect(tmp.newFolder("usdt"), rpcs, Arrays.asList(DexConfig.BSC_USDT_TESTNET, DexConfig.SOL_USDT_DEVNET), Arrays.asList("https://relay.example"), false, "",
                (request, done) -> { sent.add(request); try { done.done(new JSONObject().put("ok", true).put("posted", 0)); } catch (Exception e) { done.done(null); } }, () -> { });
        // Solana's USDT has no node here, so only BNB Chain's is traded
        assertEquals(Arrays.asList(97L, 11155111L, DexConfig.BSC_USDT_TESTNET), d.chains());
        assertEquals("[\"BNB/ETH\",\"BNB/USDT-BSC\",\"ETH/USDT-BSC\"]", d.status("", true).getJSONArray("pairs").toString());
        // A USDT offer as the relays carry it: shown as its own coin, with its decimals
        final JSONObject l = listing("0x" + "ab".repeat(32));
        l.getJSONObject("listing").put("pair", "ETH/USDT-BSC").getJSONObject("gives").put("asset", "0x55d398326f99059ff775485246999027b3197955").put("value", "0x15af1d78b58c40000");
        final JSONObject shown = DexDirect.shown(l, 1);
        assertEquals("USDT-BSC", shown.getJSONObject("gives").getString("coin"));
        assertEquals(DexConfig.BSC_USDT_TESTNET, shown.getJSONObject("gives").getLong("chainId"));
        assertEquals("25", shown.getJSONObject("gives").getString("amount"));
        assertEquals("2500", shown.getString("price"));
        // Posting it goes to the helper as its chain plus the token
        assertTrue(d.post(SEED, DexConfig.BSC_USDT_TESTNET, "1000", 97, "2000", 3600));
        JSONObject offer = null;
        for (JSONObject r : sent) if (r.getString("command").equals("directPost")) offer = r.getJSONObject("directOffer");
        assertEquals(97, offer.getLong("givesChainId"));
        assertEquals("usdt", offer.getString("givesToken"));
        assertFalse(offer.has("wantsToken"));
    }

    @Test
    public void amountsAndPairs() {
        assertEquals("ETH/SOL", DexDirect.pairOf(DexConfig.SOL_MAINNET, 1));
        assertEquals("BNB/ETH", DexDirect.pairOf(56, 1));
        assertEquals("500000000000000000", DexDirect.hexToDecimal("0x6f05b59d3b20000"));
        assertEquals("0.05", DexDirect.priceText("2000000000", 9, "100000000000000000", 18));   // 2 SOL for 0.1 ETH
        assertEquals("", DexDirect.priceText("0", 9, "1", 18));
    }
}
