package com.m2049r.xmrwallet.dex;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.MathContext;
import java.security.SecureRandom;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Pattern;

/**
 * Direct (coin <-> coin) trades between ETH, BNB and SOL: the phone's version of the desktop
 * wallet's DirectMarketJob. The relays' direct offers, our own offers, takes and trades (each a
 * folder under tradesDir that survives a restart), every one stepped by the helper's direct
 * commands until it swapped or refunded, then paid out to the user's own wallet. Requests run one
 * after another from a queue; any number of trades run at once. Runs on the engine's thread.
 */
final class DexDirect {
    interface Listener {
        void directChanged();
    }

    /** Runs one helper request; the answer (null when there is none) comes back on the engine's thread. */
    interface Runner {
        void run(JSONObject request, DexHelper.Done done);
    }

    private static final Pattern HASH = Pattern.compile("\\A0x[0-9a-f]{64}\\z"), DIGITS = Pattern.compile("\\A[0-9]{1,78}\\z"),
            HEX_QTY = Pattern.compile("\\A0x[0-9a-f]{1,64}\\z"), ADDRESS = Pattern.compile("\\A0x[0-9a-fA-F]{40}\\z"),
            PAIR = Pattern.compile("\\A(BNB|ETH|SOL)/(BNB|ETH|SOL)\\z");

    private static final class Folder {
        String path, kind, state, detail = "", listingId;
        JSONObject listing;                       // the signed listing
        JSONObject trade = new JSONObject();      // the helper's last view of the trade
    }

    private static final class Run {
        final JSONObject request;
        final String folder;

        Run(JSONObject request, String folder) {
            this.request = request;
            this.folder = folder;
        }
    }

    final File tradesDir;
    private final Map<Long, String> rpcs;
    private final List<String> relays;
    private final boolean mainnet;
    private final String localSolProgram;
    private final Runner runner;
    private final Listener listener;
    private final List<Folder> folders = new ArrayList<>();
    private final Map<String, JSONObject> signed = new HashMap<>();
    private final Deque<Run> queue = new ArrayDeque<>();
    private JSONArray listings = new JSONArray();
    private JSONObject lastPayout = new JSONObject();
    private String state = "ready", error = "", command = "", folder = "";
    private boolean busy;

    DexDirect(File tradesDir, Map<Long, String> rpcs, List<String> relays, boolean mainnet, String localSolProgram, Runner runner, Listener listener) {
        this.tradesDir = tradesDir;
        this.rpcs = new TreeMap<>(rpcs);
        this.relays = relays;
        this.mainnet = mainnet;
        this.localSolProgram = localSolProgram == null ? "" : localSolProgram;
        this.runner = runner;
        this.listener = listener;
        DexFiles.privateDir(tradesDir);
        scanFolders();
    }

    // ---- coins and amounts ---------------------------------------------------------------------

    static boolean directChain(long id) {
        return id == 1 || id == 56 || id == 11155111 || id == 97 || id == 31337
                || id == DexConfig.SOL_MAINNET || id == DexConfig.SOL_DEVNET || id == DexConfig.SOL_LOCAL;
    }

    static String ticker(long id) {
        return DexConfig.solana(id) ? "SOL" : (id == 56 || id == 97) ? "BNB" : "ETH";
    }

    static int decimals(long id) {
        return DexConfig.solana(id) ? 9 : 18;
    }

    static String pairOf(long a, long b) {
        final String x = ticker(a), y = ticker(b);
        return x.compareTo(y) <= 0 ? x + "/" + y : y + "/" + x;
    }

    static String hexToDecimal(String hex) {
        if (hex == null || !HEX_QTY.matcher(hex).matches()) return null;
        return new BigInteger(hex.substring(2), 16).toString();
    }

    /** Price of a pair's base in its quote coin, six significant figures, for display only. */
    static String priceText(String baseUnits, int baseDecimals, String quoteUnits, int quoteDecimals) {
        try {
            final BigDecimal base = new BigDecimal(new BigInteger(baseUnits), baseDecimals), quote = new BigDecimal(new BigInteger(quoteUnits), quoteDecimals);
            if (base.signum() <= 0 || quote.signum() <= 0) return "";
            return quote.divide(base, new MathContext(6)).stripTrailingZeros().toPlainString();
        } catch (Exception e) {
            return "";
        }
    }

    List<Long> chains() {
        return new ArrayList<>(rpcs.keySet());
    }

    private static boolean coinValid(JSONObject c) {
        return c != null && c.length() == 4 && c.opt("chainId") instanceof Number && directChain(c.optLong("chainId"))
                && ADDRESS.matcher(c.optString("contract")).matches() && ADDRESS.matcher(c.optString("asset")).matches()
                && HEX_QTY.matcher(c.optString("value")).matches();
    }

    static boolean listingValid(JSONObject signedListing) {
        if (signedListing == null || signedListing.length() != 2) return false;
        final JSONObject l = signedListing.optJSONObject("listing");
        return l != null && "monzero-dex-direct-listing/1".equals(l.optString("schema")) && HASH.matcher(l.optString("id")).matches()
                && PAIR.matcher(l.optString("pair")).matches() && ADDRESS.matcher(l.optString("maker")).matches()
                && coinValid(l.optJSONObject("gives")) && coinValid(l.optJSONObject("wants"))
                && l.opt("expiresAt") instanceof Number && l.opt("createdAt") instanceof Number;
    }

    // ---- folders -------------------------------------------------------------------------------

    private static String small(File f) {
        final byte[] b = DexFiles.read(f, 400);
        final String t = b == null ? "" : new String(b, java.nio.charset.StandardCharsets.UTF_8).trim();
        return t.length() > 200 ? t.substring(0, 200) : t;
    }

    void scanFolders() {
        final List<Folder> kept = new ArrayList<>();
        final String[] names = tradesDir.list();
        if (names != null) {
            Arrays.sort(names);
            final long now = System.currentTimeMillis() / 1000;
            for (String name : names) {
                final File path = new File(tradesDir, name);
                if (!path.isDirectory()) continue;
                final boolean maker = new File(path, "direct-listing.json").isFile(), taker = new File(path, "direct-take.json").isFile();
                if (!maker && !taker) continue;
                final JSONObject o = DexFiles.readJson(new File(path, maker ? "direct-listing.json" : "direct-take.json"), 65536);
                if (o == null || !listingValid(o.optJSONObject("listing"))) continue;
                final Folder f = new Folder();
                f.path = path.getPath();
                f.kind = maker ? "offer" : "take";
                f.listing = o.optJSONObject("listing");
                f.listingId = f.listing.optJSONObject("listing").optString("id");
                final boolean expired = f.listing.optJSONObject("listing").optLong("expiresAt") <= now;
                if (new File(path, "direct-journal.json").isFile()) f.state = "agreed";
                else if (maker && o.optBoolean("closed")) f.state = "cancelled";
                else if (new File(path, "cancelled").exists() || new File(path, "abandoned").exists()) f.state = "cancelled";
                else if (!maker && new File(path, "refused").isFile()) { f.state = "refused"; f.detail = small(new File(path, "refused")); }
                else if (maker && expired) f.state = "expired";
                else f.state = "open";
                for (Folder old : folders) if (old.path.equals(f.path)) { f.trade = old.trade; if (f.detail.isEmpty() && f.state.equals(old.state)) f.detail = old.detail; }
                if (f.state.equals("agreed") && f.trade.optBoolean("done")) f.state = "done";
                kept.add(f);
            }
        }
        folders.clear();
        folders.addAll(kept);
    }

    private Folder find(String path) {
        for (Folder f : folders) if (f.path.equals(path)) return f;
        return null;
    }

    /** A trade the app was running when it closed and still needs the keys to go on. */
    boolean hasRunningTrade() {
        for (Folder f : folders) if (f.state.equals("agreed")) return true;
        return false;
    }

    // ---- requests ------------------------------------------------------------------------------

    private JSONObject base(String cmd, String dir, String seedHex) throws Exception {
        final JSONObject direct = new JSONObject();
        final JSONObject nodes = new JSONObject();
        for (Map.Entry<Long, String> e : rpcs.entrySet()) nodes.put(String.valueOf(e.getKey()), e.getValue());
        direct.put("rpcs", nodes);
        if (seedHex != null) direct.put("walletSeedHex", seedHex).put("accountIndex", 0);
        final JSONObject r = new JSONObject().put("schema", DexHelper.REQUEST_SCHEMA).put("command", cmd)
                .put("tradeDir", dir == null || dir.isEmpty() ? tradesDir.getPath() : dir).put("relays", new JSONArray(relays)).put("direct", direct);
        if (mainnet) r.put("directMainnet", true);
        if (!localSolProgram.isEmpty()) r.put("localSolProgram", localSolProgram);
        return r;
    }

    boolean refreshListings() {
        for (Run q : queue) if ("directListings".equals(q.request.optString("command"))) return true;
        if (busy && command.equals("directListings")) return true;
        try {
            return enqueue(base("directListings", null, null), "", false);
        } catch (Exception e) {
            return false;
        }
    }

    boolean post(String seedHex, long givesChain, String givesValue, long wantsChain, String wantsValue, long lifetimeSecs) {
        if (seedHex == null || !rpcs.containsKey(givesChain) || !rpcs.containsKey(wantsChain) || ticker(givesChain).equals(ticker(wantsChain))
                || !DIGITS.matcher(givesValue).matches() || !DIGITS.matcher(wantsValue).matches() || lifetimeSecs < 600 || lifetimeSecs > 31 * 86400L) return false;
        try {
            final String dir = new File(tradesDir, "offer-" + Long.toHexString(new SecureRandom().nextLong())).getPath();
            final JSONObject offer = new JSONObject().put("givesChainId", givesChain).put("givesValue", givesValue).put("wantsChainId", wantsChain)
                    .put("wantsValue", wantsValue).put("lifetimeSecs", lifetimeSecs);
            return enqueue(base("directPost", dir, seedHex).put("directOffer", offer), dir, true);
        } catch (Exception e) {
            return false;
        }
    }

    boolean take(String seedHex, String listingId) {
        final JSONObject signedListing = signed.get(listingId);
        if (seedHex == null || signedListing == null) return false;
        for (Folder f : folders) if (listingId.equals(f.listingId)) return false;
        final JSONObject l = signedListing.optJSONObject("listing");
        if (!rpcs.containsKey(l.optJSONObject("gives").optLong("chainId")) || !rpcs.containsKey(l.optJSONObject("wants").optLong("chainId"))) return false;
        try {
            final String dir = new File(tradesDir, "take-" + listingId.substring(2, 18)).getPath();
            return enqueue(base("directTake", dir, seedHex).put("directListing", signedListing), dir, true);
        } catch (Exception e) {
            return false;
        }
    }

    boolean cancel(String seedHex, String dir) {
        final Folder f = find(dir);
        if (f == null || !f.state.equals("open") || seedHex == null) return false;
        if (f.kind.equals("take")) {
            // Nothing of a take is locked until its keys are agreed: we simply stop waiting
            if ((busy && folder.equals(dir)) || new File(dir, "direct-journal.json").exists()) return false;
            queue.removeIf(q -> q.folder.equals(dir));
            if (!DexFiles.write(new File(dir, "abandoned"), "stopped waiting for the maker\n")) return false;
            f.state = "cancelled";
            listener.directChanged();
            return true;
        }
        try {
            return enqueue(base("directCancel", dir, seedHex), dir, true);
        } catch (Exception e) {
            return false;
        }
    }

    boolean approve(String dir, String commitment) {
        final Folder f = find(dir);
        if (f == null || !f.state.equals("agreed") || commitment == null || !HASH.matcher(commitment).matches()
                || !commitment.equals(f.trade.optString("commitment"))) return false;
        try {
            return enqueue(base("directApprove", dir, null).put("commitment", commitment), dir, true);
        } catch (Exception e) {
            return false;
        }
    }

    /** The chain a finished trade brought its coin in on (what we got, or ours back), or 0. */
    long payoutChain(String dir) {
        final Folder f = find(dir);
        if (f == null || !f.state.equals("done")) return 0;
        final String stage = f.trade.optString("stage");
        final JSONObject leg = stage.equals("Done(Swapped)") ? f.trade.optJSONObject("gets") : stage.equals("Done(Refunded)") ? f.trade.optJSONObject("gives") : null;
        return leg == null ? 0 : leg.optLong("chainId");
    }

    boolean payout(String seedHex, String dir, String address) {
        final long chain = payoutChain(dir);
        if (seedHex == null || chain == 0 || address == null || !DexConfig.address(chain, address.trim())) return false;
        try {
            return enqueue(base("directPayout", dir, seedHex).put("chainId", chain).put("payoutAddress", address.trim()), dir, true);
        } catch (Exception e) {
            return false;
        }
    }

    /** One helper run for each folder with work (serve, take, tick); false when there is none. */
    boolean step(String seedHex) {
        if (seedHex == null) return false;
        boolean any = false;
        for (Folder f : new ArrayList<>(folders)) {
            final String cmd = f.state.equals("open") ? (f.kind.equals("offer") ? "directServe" : "directTake") : f.state.equals("agreed") ? "directTick" : "";
            if (cmd.isEmpty()) continue;
            boolean queued = busy && folder.equals(f.path);
            for (Run q : queue) queued = queued || q.folder.equals(f.path);
            if (queued) continue;
            try {
                any = enqueue(base(cmd, f.path, seedHex), f.path, false) || any;
            } catch (Exception ignored) {
            }
        }
        return any;
    }

    private boolean enqueue(JSONObject request, String dir, boolean urgent) {
        if (queue.size() > 64) return false;
        if (urgent) queue.addFirst(new Run(request, dir)); else queue.addLast(new Run(request, dir));
        if (!busy) next(); else listener.directChanged();
        return true;
    }

    private void next() {
        if (busy || queue.isEmpty()) return;
        final Run run = queue.pollFirst();
        command = run.request.optString("command");
        folder = run.folder;
        busy = true;
        state = "running";
        listener.directChanged();
        runner.run(run.request, this::finish);
    }

    private void finish(JSONObject r) {
        busy = false;
        if (r == null || !r.optBoolean("ok")) {
            state = r == null ? "failed" : "refused";
            final String e = r == null ? "no valid answer from the helper" : r.optString("error");
            error = e.length() > 300 ? e.substring(0, 300) : e;
            final Folder f = find(folder);
            if (f != null) f.detail = error;
            if (command.equals("directPost") || command.equals("directTake")) scanFolders();
        } else {
            state = "ok";
            error = "";
            try {
                handle(r);
            } catch (Exception ignored) {
            }
        }
        listener.directChanged();
        next();
    }

    private void handle(JSONObject r) throws Exception {
        switch (command) {
            case "directListings": {
                signed.clear();
                listings = new JSONArray();
                final JSONArray found = r.optJSONArray("listings");
                for (int i = 0; found != null && i < found.length(); i++) {
                    final JSONObject entry = found.optJSONObject(i);
                    final JSONObject s = entry == null ? null : entry.optJSONObject("signed");
                    if (!listingValid(s)) continue;
                    final JSONArray from = entry.optJSONArray("relays");
                    final JSONObject shown = shown(s, from == null ? 0 : from.length());
                    if (shown == null) continue;
                    signed.put(shown.getString("id"), s);
                    listings.put(shown);
                }
                return;
            }
            case "directPost":
            case "directCancel":
                if (command.equals("directCancel") && r.optBoolean("cancelled")) DexFiles.write(new File(folder, "cancelled"), "withdrawn by the user\n");
                scanFolders();
                if (command.equals("directPost")) refreshListings();
                return;
            case "directPayout": {
                lastPayout = r.optJSONObject("payout") == null ? new JSONObject() : r.optJSONObject("payout");
                lastPayout.put("folder", folder);
                final Folder f = find(folder);
                if (f != null) f.trade.put("payout", r.optJSONObject("payout"));
                return;
            }
        }
        if (find(folder) == null) scanFolders();
        final Folder f = find(folder);
        if (f == null) return;
        f.detail = "";
        if (command.equals("directServe")) {
            if (r.optBoolean("taken")) f.state = "agreed";
            else if (r.optBoolean("expired")) f.state = "expired";
            return;
        }
        if (command.equals("directTake")) {
            if (r.optBoolean("agreed")) { f.state = "agreed"; f.trade = r; }
            else if (r.optBoolean("refusedFinal")) { f.state = "refused"; f.detail = r.optString("refused"); }
            else if (r.optBoolean("expired")) f.state = "expired";
            else if (r.has("refused") && !r.isNull("refused")) f.detail = r.optString("refused");
            return;
        }
        // directStatus / directApprove / directTick: the trade as the helper sees it now
        final JSONObject trade = new JSONObject(r.toString());
        trade.remove("schema");
        trade.remove("ok");
        if (trade.isNull("payout") && f.trade.has("payout")) trade.put("payout", f.trade.opt("payout"));
        f.trade = trade;
        f.state = trade.optBoolean("done") ? "done" : "agreed";
    }

    // ---- what the screens show -----------------------------------------------------------------

    private static JSONObject coinShown(long chain, String units) throws Exception {
        return new JSONObject().put("chainId", chain).put("coin", ticker(chain)).put("units", units).put("amount", DexFiles.units(units, decimals(chain)));
    }

    static JSONObject shown(JSONObject s, int relays) throws Exception {
        final JSONObject l = s.getJSONObject("listing"), gives = l.getJSONObject("gives"), wants = l.getJSONObject("wants");
        final long gc = gives.getLong("chainId"), wc = wants.getLong("chainId");
        final String gv = hexToDecimal(gives.getString("value")), wv = hexToDecimal(wants.getString("value"));
        if (gv == null || wv == null || ticker(gc).equals(ticker(wc))) return null;
        final String pair = pairOf(gc, wc);
        // The book's view: an offer that gives the pair's base asks for the quote (a sell of the base)
        final boolean sellsBase = ticker(gc).equals(pair.substring(0, pair.indexOf('/')));
        final String baseUnits = sellsBase ? gv : wv, quoteUnits = sellsBase ? wv : gv;
        final long bc = sellsBase ? gc : wc, qc = sellsBase ? wc : gc;
        return new JSONObject().put("id", l.getString("id")).put("pair", pair).put("maker", l.getString("maker")).put("side", sellsBase ? "sell" : "buy")
                .put("gives", coinShown(gc, gv)).put("wants", coinShown(wc, wv))
                .put("base", DexFiles.units(baseUnits, decimals(bc))).put("quote", DexFiles.units(quoteUnits, decimals(qc)))
                .put("price", priceText(baseUnits, decimals(bc), quoteUnits, decimals(qc)))
                .put("expiresAt", l.getLong("expiresAt")).put("relays", relays);
    }

    private JSONObject shown(Folder f) throws Exception {
        final JSONObject l = shown(f.listing, 0);
        if (l == null) return null;
        // A take: we give what the maker wants, and get what it gives
        if (f.kind.equals("take")) {
            final Object g = l.get("gives");
            l.put("gives", l.get("wants")).put("wants", g);
        }
        return l.put("folder", f.path).put("kind", f.kind).put("state", f.state).put("detail", f.detail).put("trade", f.trade);
    }

    /** A listing the market holds (as shown), or null. */
    JSONObject listing(String id) {
        for (int i = 0; i < listings.length(); i++) {
            final JSONObject l = listings.optJSONObject(i);
            if (l != null && id.equals(l.optString("id"))) return l;
        }
        return null;
    }

    /** A folder's entry (as status() shows it), or null. */
    JSONObject folder(String path) {
        final Folder f = find(path);
        try {
            return f == null ? null : shown(f);
        } catch (Exception e) {
            return null;
        }
    }

    JSONObject status(String message, boolean keys) {
        final JSONObject out = new JSONObject();
        try {
            final JSONArray mine = new JSONArray();
            for (Folder f : folders) {
                final JSONObject s = shown(f);
                if (s != null) mine.put(s);
            }
            final JSONArray chainList = new JSONArray();
            for (long id : rpcs.keySet()) chainList.put(new JSONObject().put("chainId", id).put("coin", ticker(id)).put("decimals", decimals(id)));
            final List<String> pairs = new ArrayList<>();
            for (long a : rpcs.keySet()) for (long b : rpcs.keySet()) if (ticker(a).compareTo(ticker(b)) < 0 && !pairs.contains(pairOf(a, b))) pairs.add(pairOf(a, b));
            java.util.Collections.sort(pairs);
            out.put("configured", true).put("keys", keys).put("busy", busy).put("queued", queue.size()).put("state", state).put("error", error).put("command", command)
                    .put("message", message).put("listings", listings).put("mine", mine).put("payout", lastPayout).put("chains", chainList)
                    .put("pairs", new JSONArray(pairs)).put("mainnet", mainnet);
        } catch (Exception ignored) {
        }
        return out;
    }
}
