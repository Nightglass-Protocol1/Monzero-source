package com.m2049r.xmrwallet.dex;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.math.BigInteger;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;

/**
 * Offers on one chain (the phone's version of the desktop wallet's EvmMarketJob): the open offers
 * from the relays, and our own offers and takes, each a folder under tradesDir that survives a
 * restart. One helper request at a time. Runs on the engine's thread.
 */
final class DexMarket {
    interface Listener {
        void marketChanged();

        void agreed(DexMarket market, String tradeDir);
    }

    private static final Pattern HASH = Pattern.compile("\\A0x[0-9a-f]{64}\\z"), DIGITS = Pattern.compile("\\A[0-9]{1,78}\\z"),
            HEX_QTY = Pattern.compile("\\A0x[0-9a-f]{1,64}\\z");

    private static final class Folder {
        String path, kind, state, detail = "", listingId;
        boolean renew;
        JSONObject listing;
    }

    final long chainId;
    /** A Monero market (BNB/XMR, ...): its private coin is XMR (12 places) in the Monero wallets */
    final boolean xmr;
    /** Monero nodes that must also see a lock (Monero main network); empty for XMZ */
    final List<String> witnesses;
    final String rpc;
    final File tradesDir;
    private final List<String> relays;
    private final String localContract;
    private final DexHelper helper;
    private final Listener listener;
    private JSONObject mainWallet;
    private final List<Folder> folders = new ArrayList<>();
    private final Map<String, JSONObject> signed = new HashMap<>();
    private JSONArray listings = new JSONArray();
    private JSONObject withdrawal = new JSONObject();
    private String state = "ready", error = "", command = "", folder = "", solAccount = "";
    private boolean busy;
    private int next;

    DexMarket(long chainId, String rpc, File tradesDir, List<String> relays, String localContract, DexHelper helper, Listener listener) {
        this(chainId, false, new ArrayList<>(), rpc, tradesDir, relays, localContract, helper, listener);
    }

    DexMarket(long chainId, boolean xmr, List<String> witnesses, String rpc, File tradesDir, List<String> relays, String localContract,
              DexHelper helper, Listener listener) {
        this.chainId = chainId;
        this.xmr = xmr;
        this.witnesses = witnesses;
        this.rpc = rpc;
        this.tradesDir = tradesDir;
        this.relays = relays;
        this.localContract = localContract;
        this.helper = helper;
        this.listener = listener;
        DexFiles.privateDir(tradesDir);
        scanFolders();
    }

    void setMainWallet(JSONObject endpoint) {
        mainWallet = endpoint;
    }

    boolean busy() {
        return busy;
    }

    /** This wallet's trade account on Solana (where SOL is paid in), once the engine knows it. */
    void setSolAccount(String account) {
        solAccount = account;
    }

    String coin() {
        return DexConfig.coin(chainId);
    }

    /** "BNB/XMZ" or "BNB/XMR" */
    String pairName() {
        return xmr ? DexConfig.xmrPair(chainId) : DexConfig.pair(chainId);
    }

    /** The private coin's decimal places: 11 for XMZ, 12 for XMR */
    int privatePlaces() {
        return xmr ? 12 : 11;
    }

    private boolean listingValid(JSONObject signedListing) {
        return listingValid(signedListing, xmr);
    }

    private static boolean listingValid(JSONObject signedListing, boolean xmr) {
        if (signedListing == null || signedListing.length() != 2) return false;
        final JSONObject l = signedListing.optJSONObject("listing");
        if (l == null) return false;
        final String pair = l.optString("pair"), side = l.optString("side");
        final String base = xmr ? "/XMR" : "/XMZ";
        final boolean known = pair.endsWith(base) && Arrays.asList("ETH", "BNB", "SOL", "USDT", "USDT-BSC").contains(pair.substring(0, pair.length() - 4));
        return HASH.matcher(l.optString("id")).matches() && known
                && (side.equals("sell-xmz") || side.equals("buy-xmz")) && HEX_QTY.matcher(l.optString("value")).matches()
                && amountText(l.opt("xmz")) != null && DexConfig.evmAddress(l.optString("maker"))
                && l.opt("expiresAt") instanceof Number && l.opt("chainId") instanceof Number;
    }

    /** A whole amount the helper writes as a number or as decimal text. */
    static String amountText(Object value) {
        if (value instanceof Integer || value instanceof Long) return ((Number) value).longValue() < 0 ? null : String.valueOf(((Number) value).longValue());
        if (value instanceof String && DIGITS.matcher((String) value).matches()) return (String) value;
        return null;
    }

    static String hexToDecimal(String hex) {
        if (hex == null || !HEX_QTY.matcher(hex).matches() || hex.length() > 2 + 32) return null;
        return new BigInteger(hex.substring(2), 16).toString();
    }

    /** Coin per XMZ, for display only: plain decimals, six significant digits. */
    static String priceText(String valueWei, String xmzAtoms, int decimals) {
        return priceText(valueWei, xmzAtoms, decimals, 11);
    }

    /** Coin per XMZ (places 11) or per XMR (places 12) */
    static String priceText(String valueWei, String xmzAtoms, int decimals, int places) {
        try {
            final double wei = Double.parseDouble(valueWei), atoms = Double.parseDouble(xmzAtoms);
            if (!(wei > 0) || !(atoms > 0)) return "";
            final double price = wei / Math.pow(10.0, decimals) / (atoms / Math.pow(10.0, places));
            final int shown = Math.max(0, Math.min(18, 5 - (int) Math.floor(Math.log10(price))));
            String text = new java.math.BigDecimal(price).setScale(shown, java.math.RoundingMode.HALF_UP).toPlainString();
            if (text.contains(".")) {
                text = text.replaceAll("0+\\z", "");
                if (text.endsWith(".")) text = text.substring(0, text.length() - 1);
            }
            return text;
        } catch (Exception e) {
            return "";
        }
    }

    /** Our offers and takes survive a restart: they are folders under tradesDir. */
    void scanFolders() {
        folders.clear();
        final String[] names = tradesDir.list();
        if (names == null) return;
        Arrays.sort(names);
        for (String name : names) {
            final File path = new File(tradesDir, name);
            if (!path.isDirectory()) continue;
            final boolean maker = new File(path, "listing.json").isFile(), taker = new File(path, "take.json").isFile();
            if (!maker && !taker) continue;
            final JSONObject o = DexFiles.readJson(new File(path, maker ? "listing.json" : "take.json"), 65536);
            if (o == null || !listingValid(o.optJSONObject("listing"))) continue;
            final Folder f = new Folder();
            f.path = path.getPath();
            f.kind = maker ? "offer" : "take";
            f.listing = o.optJSONObject("listing");
            f.listingId = f.listing.optJSONObject("listing").optString("id");
            f.state = new File(path, "journal.json").isFile() ? "agreed" : (maker && !o.isNull("takenBy") && o.has("takenBy") ? "agreed" : "open");
            if (new File(path, "cancelled").exists() || new File(path, "abandoned").exists()) f.state = "cancelled";
            f.renew = maker && new File(path, "renew").exists();
            // Renewed (kept open): the new offer has its own folder; this one is retired
            if (new File(path, "renewed").exists()) f.state = "renewed";
            // Taken off the board by our own helper: our side could no longer pay for it
            final File withdrawn = new File(path, "withdrawn");
            if ("open".equals(f.state) && withdrawn.isFile()) {
                f.state = "withdrawn";
                final byte[] why = DexFiles.read(withdrawn, 400);
                final String text = why == null ? "" : new String(why, java.nio.charset.StandardCharsets.UTF_8);
                f.detail = text.length() > 200 ? text.substring(0, 200) : text;
            }
            // A take the maker refused: it ended there, nothing was locked
            final File refused = new File(path, "refused");
            if ("open".equals(f.state) && !maker && refused.isFile()) {
                f.state = "refused";
                final byte[] why = DexFiles.read(refused, 400);
                final String text = why == null ? "" : new String(why, java.nio.charset.StandardCharsets.UTF_8);
                f.detail = text.length() > 200 ? text.substring(0, 200) : text;
            }
            folders.add(f);
        }
    }

    /** Agreed trades the wallet has not seen finish: reopened after a restart. */
    List<String> agreedFolders() {
        final List<String> out = new ArrayList<>();
        for (Folder f : folders) if (f.state.equals("agreed") && !new File(f.path, "finished").exists()) out.add(f.path);
        return out;
    }

    boolean hasOpenOffer() {
        for (Folder f : folders) if (f.state.equals("open")) return true;
        return false;
    }

    private JSONObject base(String cmd, String dir) throws Exception {
        final JSONObject r = new JSONObject().put("schema", DexHelper.REQUEST_SCHEMA).put("command", cmd)
                .put("tradeDir", dir == null || dir.isEmpty() ? tradesDir.getPath() : dir).put("relays", new JSONArray(relays));
        if (!localContract.isEmpty()) r.put("localContract", localContract);
        // Monero main-network offers are refused unless the request says the app trades them
        if (xmr && DexConfig.mainnet()) r.put("xmrMainnet", true);
        return r;
    }

    /** What every keyed helper request carries: the node, the keys and the wallet services. */
    JSONObject tickConfig(String seedHex, JSONObject swapWallet) throws Exception {
        if (seedHex == null || mainWallet == null) return null;
        final JSONObject t = new JSONObject().put("evmRpc", rpc).put("walletSeedHex", seedHex).put("accountIndex", 0)
                .put("mainWallet", mainWallet).put("swapWallet", swapWallet == null ? mainWallet : swapWallet);
        if (!witnesses.isEmpty()) t.put("witnessDaemons", new JSONArray(witnesses));
        return t;
    }

    boolean refreshListings(String pair) {
        if (!pair.equals(pairName())) return false;
        try {
            // Partial offers too: this wallet can take part of one
            return run(base("listings", null).put("pair", pair).put("partial", true), "");
        } catch (Exception e) {
            return false;
        }
    }

    /** minXmzAtoms: the least one take may be (a partial offer), or null for a whole offer. */
    boolean post(String seedHex, String side, String valueWei, String xmzAtoms, String minXmzAtoms, long lifetimeSecs, boolean renew) {
        if (busy || (!side.equals("sell-xmz") && !side.equals("buy-xmz")) || !DIGITS.matcher(valueWei).matches()
                || !DIGITS.matcher(xmzAtoms).matches() || lifetimeSecs <= 0 || lifetimeSecs > 31 * 86400L
                || (minXmzAtoms != null && !DIGITS.matcher(minXmzAtoms).matches())) return false;
        try {
            final JSONObject t = tickConfig(seedHex, null);
            if (t == null) return false;
            final String dir = new File(tradesDir, "offer-" + Long.toHexString(new SecureRandom().nextLong())).getPath();
            // The helper trades the market on its chain; a token market (USDT) names its pair
            final JSONObject offer = new JSONObject().put("chainId", DexConfig.helperChain(chainId)).put("side", side).put("valueWei", valueWei)
                    .put("xmzAtoms", xmzAtoms).put("lifetimeSecs", lifetimeSecs);
            if (minXmzAtoms != null) offer.put("minXmzAtoms", minXmzAtoms);
            if (renew) offer.put("renew", true);
            if (xmr) offer.put("pair", pairName()).put("coin", "xmr");
            else if (DexConfig.token(chainId)) offer.put("pair", DexConfig.pair(chainId));
            return run(base("post", dir).put("tick", t).put("offer", offer), dir);
        } catch (Exception e) {
            return false;
        }
    }

    /** Keep one of our open offers open (posted again shortly before it expires) or not: a mark in its folder. */
    boolean setRenew(String dir, boolean on) {
        for (Folder f : folders)
            if (f.path.equals(dir) && "offer".equals(f.kind) && "open".equals(f.state)) {
                final File mark = new File(dir, "renew");
                final boolean ok = on ? DexFiles.write(mark, "") : (!mark.exists() || mark.delete());
                if (ok) { f.renew = on; listener.marketChanged(); }
                return ok;
            }
        return false;
    }

    /** The amounts of a listing the relays showed (for the limits check); null when unknown. */
    JSONObject listing(String listingId) {
        for (int i = 0; i < listings.length(); i++) {
            final JSONObject l = listings.optJSONObject(i);
            if (l != null && listingId.equals(l.optString("id"))) return l;
        }
        return null;
    }

    /** xmzAtoms: how much of a partial offer to take, or null for all of it. */
    boolean take(String seedHex, String listingId, String xmzAtoms) {
        final JSONObject signedListing = signed.get(listingId);
        if (xmzAtoms != null && !DIGITS.matcher(xmzAtoms).matches()) return false;
        if (busy || signedListing == null) return false;
        for (Folder f : folders) if (listingId.equals(f.listingId)) return false;
        try {
            final JSONObject t = tickConfig(seedHex, null);
            if (t == null) return false;
            final String dir = new File(tradesDir, "take-" + listingId.substring(2, 18)).getPath();
            final JSONObject r = base("take", dir).put("tick", t).put("listing", signedListing);
            if (xmzAtoms != null) r.put("xmzAtoms", xmzAtoms);
            return run(r, dir);
        } catch (Exception e) {
            return false;
        }
    }

    boolean cancel(String seedHex, String dir) {
        // A take still waiting for the maker's answer: nothing of it is locked, so we just stop
        // waiting (no helper step runs meanwhile: the market is idle). Its folder keeps a mark.
        for (Folder f : folders)
            if (f.path.equals(dir) && f.kind.equals("take") && f.state.equals("open")) {
                if (busy || new File(dir, "journal.json").exists()) return false;
                if (!DexFiles.write(new File(dir, "abandoned"), "stopped waiting for the maker\n")) return false;
                f.state = "cancelled";
                listener.marketChanged();
                return true;
            }
        boolean ours = false;
        for (Folder f : folders) ours = ours || (f.path.equals(dir) && f.kind.equals("offer") && f.state.equals("open"));
        if (busy || !ours) return false;
        try {
            final JSONObject t = tickConfig(seedHex, null);
            return t != null && run(base("cancel", dir).put("tick", t), dir);
        } catch (Exception e) {
            return false;
        }
    }

    boolean withdraw(String seedHex, String address) {
        if (busy || !DexConfig.address(chainId, address)) return false;
        try {
            final JSONObject t = tickConfig(seedHex, null);
            if (t == null) return false;
            withdrawal = new JSONObject();
            return run(base("withdraw", null).put("tick", t).put("payoutAddress", address).put("chainId", DexConfig.helperChain(chainId)), "");
        } catch (Exception e) {
            return false;
        }
    }

    boolean report(String dir) {
        if (busy || !dir.startsWith(tradesDir.getPath() + "/")) return false;
        try {
            return run(base("report", dir), dir);
        } catch (Exception e) {
            return false;
        }
    }

    /** One step for one of our open offers or takes (in turn): answer takes, or collect the answer. */
    boolean step(String seedHex) {
        final List<Folder> open = new ArrayList<>();
        for (Folder f : folders) if (f.state.equals("open")) open.add(f);
        if (busy || open.isEmpty()) return false;
        final Folder f = open.get(next++ % open.size());
        try {
            final JSONObject t = tickConfig(seedHex, null);
            return t != null && run(base(f.kind.equals("offer") ? "serve" : "take", f.path).put("tick", t), f.path);
        } catch (Exception e) {
            return false;
        }
    }

    private boolean run(JSONObject request, String dir) {
        if (busy || !helper.available()) return false;
        command = request.optString("command");
        folder = dir;
        busy = true;
        state = "running";
        error = "";
        helper.run(request, 120000, 8 * 1024 * 1024, this::finish);
        listener.marketChanged();
        return true;
    }

    private void finish(JSONObject r) {
        busy = false;
        if (r == null || !r.optBoolean("ok")) {
            state = r == null ? "failed" : "refused";
            final String e = r == null ? "no valid answer from the helper (" + helper.lastFailure() + ")" : r.optString("error");
            error = e.length() > 300 ? e.substring(0, 300) : e;
            // An offer that was written before the helper stopped is ours all the same: it shows as
            // open and the next step sends it to the relays
            if (r == null && "post".equals(command)) scanFolders();
            listener.marketChanged();
            return;
        }
        state = "ok";
        try {
            switch (command) {
                case "listings": {
                    signed.clear();
                    listings = new JSONArray();
                    final int places = DexConfig.decimals(chainId);
                    final JSONArray found = r.optJSONArray("listings");
                    for (int i = 0; found != null && i < found.length(); i++) {
                        final JSONObject entry = found.optJSONObject(i);
                        final JSONObject s = entry == null ? null : entry.optJSONObject("signed");
                        if (!listingValid(s)) continue;
                        final JSONObject l = s.optJSONObject("listing");
                        final String wei = hexToDecimal(l.optString("value")), xmz = amountText(l.opt("xmz"));
                        if (wei == null || xmz == null) continue;
                        final String id = l.optString("id");
                        signed.put(id, s);
                        final JSONArray at = entry.optJSONArray("relays");
                        final JSONObject shown = new JSONObject().put("id", id).put("pair", l.optString("pair")).put("side", l.optString("side"))
                                .put("chainId", l.optLong("chainId")).put("maker", l.optString("maker")).put("valueWei", wei).put("xmzAtoms", xmz)
                                .put("coin", DexFiles.units(wei, places)).put("xmz", DexFiles.units(xmz, privatePlaces())).put("price", priceText(wei, xmz, places, privatePlaces()))
                                .put("expiresAt", l.optLong("expiresAt")).put("relays", at == null ? 0 : at.length());
                        // A partial offer: any amount from its minimum may be taken
                        final String min = l.has("minXmz") ? amountText(l.opt("minXmz")) : null;
                        if (min != null) shown.put("minXmzAtoms", min).put("minXmz", DexFiles.units(min, privatePlaces()));
                        listings.put(shown);
                    }
                    break;
                }
                case "post":
                    scanFolders();
                    break;
                case "serve":
                case "take": {
                    boolean known = false;
                    for (Folder f : folders) known = known || f.path.equals(folder);
                    if (command.equals("take") && !known) scanFolders();   // a first take creates its folder
                    // Part of a partial offer of ours was taken: the rest is a new offer in its own folder
                    if (r.optJSONObject("remainder") != null) {
                        boolean have = false;
                        final String rest = r.optJSONObject("remainder").optString("folder");
                        for (Folder f : folders) have = have || f.path.equals(rest);
                        if (!have) scanFolders();
                    }
                    // A kept-open offer was posted again: serve the new folder from now on
                    final boolean renewed = r.optJSONObject("renewed") != null;
                    if (renewed) scanFolders();
                    final boolean done = r.optBoolean("taken") || r.optBoolean("agreed");
                    for (Folder f : folders)
                        if (f.path.equals(folder)) {
                            if (done) f.state = "agreed";
                            else if (renewed) f.state = "renewed";
                            else if (r.optBoolean("offerWithdrawn")) f.state = "withdrawn";
                            else if (r.optBoolean("expired")) f.state = "expired";
                            else if (r.optBoolean("refusedFinal")) f.state = "refused";
                            else if (r.optBoolean("stopped")) f.state = "cancelled";
                            final String refused = r.opt("refused") instanceof String ? r.optString("refused")
                                    : r.optBoolean("offerWithdrawn") ? r.optString("reason") : "";
                            f.detail = refused.length() > 200 ? refused.substring(0, 200) : refused;
                        }
                    if (done) {
                        final String dir = folder;
                        listener.marketChanged();
                        listener.agreed(this, dir);
                        return;
                    }
                    break;
                }
                case "withdraw":
                    if (r.optJSONObject("withdrawn") != null) withdrawal = r.optJSONObject("withdrawn");
                    else {
                        final String reason = r.optString("reason");
                        withdrawal = new JSONObject().put("sent", false).put("reason", reason.length() > 200 ? reason.substring(0, 200) : reason);
                    }
                    break;
                case "cancel":
                    DexFiles.write(new File(folder, "cancelled"), "");
                    scanFolders();
                    break;
                default:
                    break;
            }
        } catch (Exception e) {
            state = "failed";
            error = "the helper's answer could not be read";
        }
        listener.marketChanged();
    }

    /** Full status (with the board the relays showed). */
    JSONObject status(String message) {
        try {
            final JSONArray mine = new JSONArray();
            final int places = DexConfig.decimals(chainId);
            for (Folder f : folders) {
                final JSONObject l = f.listing.optJSONObject("listing");
                final String wei = hexToDecimal(l.optString("value")), xmz = amountText(l.opt("xmz"));
                mine.put(new JSONObject().put("folder", f.path).put("kind", f.kind).put("state", f.state).put("detail", f.detail).put("id", f.listingId)
                        .put("pair", l.optString("pair")).put("side", l.optString("side")).put("coin", wei == null ? "" : DexFiles.units(wei, places))
                        .put("xmz", xmz == null ? "" : DexFiles.units(xmz, privatePlaces())).put("price", wei == null || xmz == null ? "" : priceText(wei, xmz, places, privatePlaces()))
                        .put("expiresAt", l.optLong("expiresAt")).put("partial", l.has("minXmz")).put("renew", f.renew));
            }
            return new JSONObject().put("configured", true).put("busy", busy).put("state", state).put("error", error).put("command", command)
                    .put("listings", listings).put("mine", mine).put("withdrawal", withdrawal).put("chainId", chainId).put("coin", coin())
                    .put("decimals", places).put("solAccount", solAccount).put("message", message)
                    .put("pair", pairName()).put("privateCoin", xmr ? "XMR" : "XMZ").put("privateDecimals", privatePlaces());
        } catch (Exception e) {
            return new JSONObject();
        }
    }
}
