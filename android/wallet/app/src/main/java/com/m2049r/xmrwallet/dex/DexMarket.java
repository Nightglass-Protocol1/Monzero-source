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
        JSONObject listing;
    }

    final long chainId;
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
        this.chainId = chainId;
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

    private static boolean listingValid(JSONObject signedListing) {
        if (signedListing == null || signedListing.length() != 2) return false;
        final JSONObject l = signedListing.optJSONObject("listing");
        if (l == null) return false;
        final String pair = l.optString("pair"), side = l.optString("side");
        return HASH.matcher(l.optString("id")).matches() && (pair.equals("ETH/XMZ") || pair.equals("BNB/XMZ") || pair.equals("SOL/XMZ"))
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
        try {
            final double wei = Double.parseDouble(valueWei), atoms = Double.parseDouble(xmzAtoms);
            if (!(wei > 0) || !(atoms > 0)) return "";
            final double price = wei / Math.pow(10.0, decimals) / (atoms / 1e11);
            final int places = Math.max(0, Math.min(18, 5 - (int) Math.floor(Math.log10(price))));
            String text = new java.math.BigDecimal(price).setScale(places, java.math.RoundingMode.HALF_UP).toPlainString();
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
        return r;
    }

    /** What every keyed helper request carries: the node, the keys and the wallet services. */
    JSONObject tickConfig(String seedHex, JSONObject swapWallet) throws Exception {
        if (seedHex == null || mainWallet == null) return null;
        return new JSONObject().put("evmRpc", rpc).put("walletSeedHex", seedHex).put("accountIndex", 0)
                .put("mainWallet", mainWallet).put("swapWallet", swapWallet == null ? mainWallet : swapWallet);
    }

    boolean refreshListings(String pair) {
        if (!pair.equals(coin() + "/XMZ")) return false;
        try {
            return run(base("listings", null).put("pair", pair), "");
        } catch (Exception e) {
            return false;
        }
    }

    boolean post(String seedHex, String side, String valueWei, String xmzAtoms, long lifetimeSecs) {
        if (busy || (!side.equals("sell-xmz") && !side.equals("buy-xmz")) || !DIGITS.matcher(valueWei).matches()
                || !DIGITS.matcher(xmzAtoms).matches() || lifetimeSecs <= 0 || lifetimeSecs > 86400) return false;
        try {
            final JSONObject t = tickConfig(seedHex, null);
            if (t == null) return false;
            final String dir = new File(tradesDir, "offer-" + Long.toHexString(new SecureRandom().nextLong())).getPath();
            final JSONObject offer = new JSONObject().put("chainId", chainId).put("side", side).put("valueWei", valueWei)
                    .put("xmzAtoms", xmzAtoms).put("lifetimeSecs", lifetimeSecs);
            return run(base("post", dir).put("tick", t).put("offer", offer), dir);
        } catch (Exception e) {
            return false;
        }
    }

    /** The amounts of a listing the relays showed (for the limits check); null when unknown. */
    JSONObject listing(String listingId) {
        for (int i = 0; i < listings.length(); i++) {
            final JSONObject l = listings.optJSONObject(i);
            if (l != null && listingId.equals(l.optString("id"))) return l;
        }
        return null;
    }

    boolean take(String seedHex, String listingId) {
        final JSONObject signedListing = signed.get(listingId);
        if (busy || signedListing == null) return false;
        for (Folder f : folders) if (listingId.equals(f.listingId)) return false;
        try {
            final JSONObject t = tickConfig(seedHex, null);
            if (t == null) return false;
            final String dir = new File(tradesDir, "take-" + listingId.substring(2, 18)).getPath();
            return run(base("take", dir).put("tick", t).put("listing", signedListing), dir);
        } catch (Exception e) {
            return false;
        }
    }

    boolean cancel(String seedHex, String dir) {
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
            return run(base("withdraw", null).put("tick", t).put("payoutAddress", address).put("chainId", chainId), "");
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
            final String e = r == null ? "no valid answer from the helper" : r.optString("error");
            error = e.length() > 300 ? e.substring(0, 300) : e;
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
                        listings.put(new JSONObject().put("id", id).put("pair", l.optString("pair")).put("side", l.optString("side"))
                                .put("chainId", l.optLong("chainId")).put("maker", l.optString("maker")).put("valueWei", wei).put("xmzAtoms", xmz)
                                .put("coin", DexFiles.units(wei, places)).put("xmz", DexFiles.units(xmz, 11)).put("price", priceText(wei, xmz, places))
                                .put("expiresAt", l.optLong("expiresAt")).put("relays", at == null ? 0 : at.length()));
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
                    final boolean done = r.optBoolean("taken") || r.optBoolean("agreed");
                    for (Folder f : folders)
                        if (f.path.equals(folder)) {
                            if (done) f.state = "agreed";
                            else if (r.optBoolean("expired")) f.state = "expired";
                            final String refused = r.opt("refused") instanceof String ? r.optString("refused") : "";
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
                        .put("xmz", xmz == null ? "" : DexFiles.units(xmz, 11)).put("price", wei == null || xmz == null ? "" : priceText(wei, xmz, places))
                        .put("expiresAt", l.optLong("expiresAt")));
            }
            return new JSONObject().put("configured", true).put("busy", busy).put("state", state).put("error", error).put("command", command)
                    .put("listings", listings).put("mine", mine).put("withdrawal", withdrawal).put("chainId", chainId).put("coin", coin())
                    .put("decimals", places).put("solAccount", solAccount).put("message", message);
        } catch (Exception e) {
            return new JSONObject();
        }
    }
}
