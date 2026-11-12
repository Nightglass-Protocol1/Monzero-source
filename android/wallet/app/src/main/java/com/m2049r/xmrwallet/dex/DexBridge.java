package com.m2049r.xmrwallet.dex;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Iterator;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;

/**
 * The link between the DEX web page (in this phone's browser) and the wallet: the same loopback
 * protocol as the desktop wallet's DexWebBridge. It listens on 127.0.0.1 only, answers only the
 * DEX page's own origin, and a page gets a token only after the user accepts the connection in the
 * wallet (from a notification), comparing a six-digit code.
 *
 * What a connected page can do: read the trading balance, the trade account, our offers and the
 * trade's state; ask to post, take or cancel an offer; ask for unused coin to be sent back. What it
 * cannot do: approve a trade, name a payout address, read a key or a password.
 *
 * Requests are read on socket threads and answered on the engine's thread.
 */
final class DexBridge implements DexEngine.Listener {
    interface Listener {
        /** Something needs the user, in the wallet: raise a notification. On the engine's thread. */
        void attention(String kind, String title, String text);

        /** Connection or pending requests changed. On the engine's thread. */
        void bridgeChanged();

        /** The page asked to add XMZ for trading: open the wallet's own Send screen. */
        void xmzAddRequested(String address, String xmz);
    }

    static final class Pairing {
        String id, origin, account, solAccount, code;
    }

    private static final int MAX_REQUEST = 64 * 1024;
    private static final Pattern LOCAL_ORIGIN = Pattern.compile("\\Ahttp://(localhost|127\\.0\\.0\\.1)(:[0-9]{1,5})?\\z");
    private static final Pattern LISTING_ID = Pattern.compile("\\A0x[0-9a-fA-F]{64}\\z");
    private static final Pattern XMZ = Pattern.compile("\\A[0-9]{1,9}(\\.[0-9]{1,8})?\\z");

    private static final class Request {
        String method = "", path = "", query = "", origin = "", host = "", token = "";
        byte[] body = new byte[0];
        boolean preflightPrivate;
    }

    private final DexEngine engine;
    private final File settings;
    private final Listener listener;
    private final SecureRandom random = new SecureRandom();
    private final ExecutorService pool = Executors.newFixedThreadPool(4);
    private ServerSocket server;
    private volatile boolean closed;
    private byte[] tokenHash;
    private String origin = "", account = "", solAccount = "";
    private Pairing pairing;
    private long pairExpires, lastAsk;
    private String pairResult = "", pairResultId = "", pairResultToken = "";
    private boolean keysWanted, returnWanted, warnedLocked;
    private String announcedTrade = "", paidTrade = "";

    DexBridge(DexEngine engine, File settings, Listener listener) {
        this.engine = engine;
        this.settings = settings;
        this.listener = listener;
    }

    // ---- state the wallet's screens read (on the engine's thread) ----------------------------------

    int port() {
        return server == null ? 0 : server.getLocalPort();
    }

    boolean connected() {
        return tokenHash != null;
    }

    String origin() {
        return origin;
    }

    String account() {
        return account;
    }

    String solAccount() {
        return solAccount;
    }

    /** The connection request waiting for the user, or null. */
    Pairing pairing() {
        if (pairing != null && System.currentTimeMillis() / 1000 > pairExpires) pairing = null;
        return pairing;
    }

    boolean keysWanted() {
        return keysWanted;
    }

    boolean returnWanted() {
        return returnWanted;
    }

    void dismissKeys() {
        if (keysWanted) {
            keysWanted = false;
            listener.bridgeChanged();
        }
    }

    /** The user asked, from the wallet's own screen, to move the trading XMZ back. */
    void askReturn() {
        returnWanted = true;
        listener.bridgeChanged();
    }

    void dismissReturn() {
        if (returnWanted) {
            returnWanted = false;
            listener.bridgeChanged();
        }
    }

    String payoutAccount(long chainId) {
        return DexConfig.solana(chainId) ? solAccount : account;
    }

    // ---- start and stop ------------------------------------------------------------------------

    private boolean originAllowed(String o) {
        if (o.isEmpty()) return false;
        for (String allowed : DexConfig.origins()) if (allowed.equals(o)) return true;
        return DexConfig.localPages() && LOCAL_ORIGIN.matcher(o).matches();
    }

    boolean listen() {
        if (server != null) return false;
        final JSONObject saved = DexFiles.readJson(settings, 4096);
        if (saved != null) {
            final byte[] hash = DexFiles.unhex(saved.optString("tokenHash"));
            final String o = saved.optString("origin"), a = saved.optString("account"), s = saved.optString("solAccount");
            final boolean evmOk = DexConfig.evmAddress(a), solOk = DexConfig.solanaAddress(s);
            if (hash != null && hash.length == 32 && originAllowed(o) && (evmOk || solOk) && (a.isEmpty() || evmOk) && (s.isEmpty() || solOk)) {
                tokenHash = hash;
                origin = o;
                account = a;
                solAccount = s;
            }
        }
        for (int i = 0; i < DexConfig.PORTS && server == null; i++) {
            try {
                server = new ServerSocket(DexConfig.FIRST_PORT + i, 16, InetAddress.getByName("127.0.0.1"));
            } catch (Exception e) {
                server = null;
            }
        }
        if (server == null) return false;
        final ServerSocket listening = server;
        final Thread accept = new Thread(() -> {
            while (!closed) {
                try {
                    final Socket socket = listening.accept();
                    pool.execute(() -> serve(socket, listening.getLocalPort()));
                } catch (Exception e) {
                    if (closed) return;
                }
            }
        }, "dex-bridge");
        accept.setDaemon(true);
        accept.start();
        engine.addListener(this);
        return true;
    }

    void close() {
        closed = true;
        try {
            if (server != null) server.close();
        } catch (Exception ignored) {
        }
        pool.shutdownNow();
    }

    private void save() {
        try {
            DexFiles.write(settings, new JSONObject().put("tokenHash", tokenHash == null ? "" : DexFiles.hex(tokenHash)).put("origin", origin)
                    .put("account", account).put("solAccount", solAccount).toString());
        } catch (Exception ignored) {
        }
    }

    private static byte[] sha256(byte[] bytes) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(bytes);
        } catch (Exception e) {
            return new byte[0];
        }
    }

    private String randomHex(int bytes) {
        final byte[] raw = new byte[bytes];
        random.nextBytes(raw);
        return DexFiles.hex(raw);
    }

    // ---- the user's answers (on the engine's thread) ---------------------------------------------

    boolean acceptPairing(String id) {
        final Pairing p = pairing();
        if (p == null || !p.id.equals(id)) return false;
        final String token = randomHex(32);
        tokenHash = sha256(token.getBytes());
        origin = p.origin;
        account = p.account;
        solAccount = p.solAccount;
        pairResult = "accepted";
        pairResultId = id;
        pairResultToken = token;
        pairing = null;
        save();
        // No trading keys yet, or locked: ask for the password right away, in the same sitting
        keysWanted = !engine.keysReady();
        listener.bridgeChanged();
        return true;
    }

    void refusePairing(String id) {
        if (pairing == null || !pairing.id.equals(id)) return;
        pairResult = "refused";
        pairResultId = id;
        pairResultToken = "";
        pairing = null;
        listener.bridgeChanged();
    }

    void disconnectPage() {
        tokenHash = null;
        origin = account = solAccount = "";
        keysWanted = returnWanted = false;
        save();
        listener.bridgeChanged();
    }

    // ---- what follows from the engine's changes ------------------------------------------------

    @Override
    public void engineChanged() {
        if (keysWanted && engine.keysReady()) keysWanted = false;
        // The app was closed (or stopped by the phone) with a trade under way: it can only go on,
        // or refund, once the keys are unlocked again. Say so once.
        if (!warnedLocked && engine.tradeWaitsForKeys()) {
            warnedLocked = true;
            keysWanted = true;
            listener.attention("keys", "A trade is waiting for your trading password", "Tap to unlock your trading keys so the trade can continue.");
        }
        if (tokenHash != null) watchTrade();
        listener.bridgeChanged();
    }

    private void watchTrade() {
        final JSONObject trade = engine.tradeStatus();
        final JSONObject snapshot = trade.optJSONObject("snapshot");
        final String id = trade.optString("tradeId");
        if (id.isEmpty() || snapshot == null) return;
        if (engine.awaitingApproval() && !announcedTrade.equals(id)) {
            announcedTrade = id;
            listener.attention("approve", "A trade is waiting for your approval", "Check the amounts in your Monzero wallet and approve the trade there.");
        }
        // When the trade is over, what the trade account holds goes back to the account the user
        // accepted for this page. Not while another offer of ours is still open: it needs the funds.
        final JSONObject payout = trade.optJSONObject("payout");
        if (!snapshot.optBoolean("done") || trade.optBoolean("busy") || paidTrade.equals(id) || (payout != null && payout.length() > 0)) return;
        if (engine.hasOpenOffer()) return;
        final JSONObject offer = snapshot.optJSONObject("offer");
        final String to = offer == null ? "" : payoutAccount(offer.optLong("chainId"));
        if (!to.isEmpty() && engine.payoutTrade(to)) paidTrade = id;
    }

    // ---- requests ------------------------------------------------------------------------------

    private static JSONObject pick(JSONObject from, String... keys) throws Exception {
        final JSONObject out = new JSONObject();
        if (from != null) for (String k : keys) if (from.has(k)) out.put(k, from.get(k));
        return out;
    }

    private JSONObject statusFor() throws Exception {
        final JSONObject markets = engine.marketStatus(), marketOut = new JSONObject();
        for (Iterator<String> it = markets.keys(); it.hasNext(); ) {
            final String coin = it.next();
            marketOut.put(coin, pick(markets.optJSONObject(coin), "coin", "chainId", "decimals", "configured", "busy", "state", "mine", "message", "error", "withdrawal"));
        }
        final JSONObject trade = engine.tradeStatus();
        final JSONObject tradeOut = pick(trade, "configured", "busy", "state", "tradeId", "commitment", "awaitingApproval", "awaitingFunding",
                "startRefusal", "error", "payout", "account", "solAccount");
        tradeOut.put("snapshot", pick(trade.optJSONObject("snapshot"), "stage", "role", "done", "offer", "evmTxs", "xmzSweeps", "approved", "progress", "deadlines"));
        return new JSONObject()
                .put("wallet", pick(engine.walletStatus(), "state", "balance", "unlocked", "address", "keysReady", "keysExist", "keysBusy", "tradeAccount", "solTradeAccount", "error"))
                .put("markets", marketOut).put("trade", tradeOut).put("account", account).put("solAccount", solAccount);
    }

    private static final class Reply {
        int status = 200;
        JSONObject body;
    }

    private static Reply fail(int code, String why) throws Exception {
        final Reply r = new Reply();
        r.status = code;
        r.body = new JSONObject().put("ok", false).put("error", why);
        return r;
    }

    private static Reply ok(JSONObject body) {
        final Reply r = new Reply();
        r.body = body;
        return r;
    }

    private String marketMessage() {
        return engine.marketMessage();
    }

    /** On the engine's thread. */
    private Reply respond(Request r) throws Exception {
        JSONObject body;
        try {
            body = r.body.length == 0 ? new JSONObject() : new JSONObject(new String(r.body, "UTF-8"));
        } catch (Exception e) {
            body = new JSONObject();
        }
        final boolean authorised = tokenHash != null && !r.token.isEmpty() && MessageDigest.isEqual(sha256(r.token.getBytes()), tokenHash) && r.origin.equals(origin);
        if (r.method.equals("GET") && r.path.equals("/v1/hello"))
            return ok(new JSONObject().put("ok", true).put("app", "monzero-wallet").put("bridge", 1).put("connected", authorised)
                    .put("account", authorised ? account : "").put("solAccount", authorised ? solAccount : ""));
        if (r.method.equals("POST") && r.path.equals("/v1/pair")) {
            // The user's own wallets: MetaMask (ETH, BNB) and/or a Solana wallet; at least one
            final String a = body.optString("account"), s = body.optString("solAccount");
            if ((a.isEmpty() && s.isEmpty()) || (!a.isEmpty() && !DexConfig.evmAddress(a)) || (!s.isEmpty() && !DexConfig.solanaAddress(s))) return fail(400, "account");
            final long now = System.currentTimeMillis() / 1000;
            if (pairing != null && now <= pairExpires) return fail(429, "another connection request is waiting in the wallet");
            final Pairing p = new Pairing();
            p.id = randomHex(16);
            p.origin = r.origin;
            p.account = a;
            p.solAccount = s;
            p.code = String.format(java.util.Locale.ROOT, "%06d", random.nextInt(1000000));
            pairing = p;
            pairExpires = now + 180;
            pairResult = pairResultId = pairResultToken = "";
            listener.bridgeChanged();
            listener.attention("pair", "The DEX web page asks to connect", "Tap to compare the code: " + p.code);
            return ok(new JSONObject().put("ok", true).put("id", p.id).put("code", p.code));
        }
        if (r.method.equals("GET") && r.path.equals("/v1/pair")) {
            String id = "";
            for (String part : r.query.split("&")) if (part.startsWith("id=")) id = part.substring(3);
            if (pairing != null && pairing.id.equals(id)) {
                if (System.currentTimeMillis() / 1000 <= pairExpires) return ok(new JSONObject().put("ok", true).put("state", "pending"));
                pairing = null;
                listener.bridgeChanged();
                return ok(new JSONObject().put("ok", true).put("state", "expired"));
            }
            if (!id.isEmpty() && id.equals(pairResultId)) {
                final JSONObject out = new JSONObject().put("ok", true).put("state", pairResult);
                if (pairResult.equals("accepted") && !pairResultToken.isEmpty()) {
                    out.put("token", pairResultToken);   // handed over once
                    pairResultToken = "";
                }
                return ok(out);
            }
            return ok(new JSONObject().put("ok", true).put("state", "expired"));
        }
        if (!authorised) return fail(401, "not connected");
        if (r.method.equals("GET") && r.path.equals("/v1/status")) return ok(statusFor().put("ok", true));
        if (!r.method.equals("POST")) return fail(404, "unknown");
        switch (r.path) {
            case "/v1/refresh": {
                final String pair = body.optString("pair");
                if (!pair.equals("ETH/XMZ") && !pair.equals("BNB/XMZ") && !pair.equals("SOL/XMZ")) return fail(400, "pair");
                return ok(new JSONObject().put("ok", engine.refreshOffers(pair)));
            }
            case "/v1/offer": {
                final long chainId = body.optLong("chainId");
                final String side = body.optString("side");
                if (!side.equals("buy-xmz") && !side.equals("sell-xmz")) return fail(400, "side");
                final boolean posted = engine.postOffer(chainId, side, body.optString("coin"), body.optString("xmz"), body.optInt("hours", 6));
                return ok(new JSONObject().put("ok", posted).put("message", marketMessage()));
            }
            case "/v1/take": {
                final String id = body.optString("id");
                if (!LISTING_ID.matcher(id).matches()) return fail(400, "id");
                final boolean taken = engine.takeOffer(id);
                return ok(new JSONObject().put("ok", taken).put("message", taken ? "" : marketMessage()));
            }
            case "/v1/cancel":
                return ok(new JSONObject().put("ok", engine.cancelOffer(body.optString("folder"))));
            case "/v1/return": {
                // Only ever to the account the user accepted for this page
                final long chainId = body.optLong("chainId");
                final String to = payoutAccount(chainId);
                if (to.isEmpty()) return ok(new JSONObject().put("ok", false).put("message", "Connect your own wallet for this coin first: the funds go back to it."));
                final boolean sent = engine.returnFunds(chainId, to);
                return ok(new JSONObject().put("ok", sent).put("message", marketMessage()));
            }
            // Things only the user can do, in the wallet: the page may only ask for the screen to be
            // shown (at most once every few seconds, so a page cannot flood the phone with notifications)
            case "/v1/unlock":
            case "/v1/xmz-add":
            case "/v1/xmz-return": {
                final long now = System.currentTimeMillis();
                if (now - lastAsk < 3000) return fail(429, "wait a moment");
                lastAsk = now;
                if (r.path.equals("/v1/unlock")) {
                    if (engine.keysReady()) return ok(new JSONObject().put("ok", true).put("ready", true));
                    keysWanted = true;
                    listener.bridgeChanged();
                    listener.attention("keys", "Your trading password is needed", "Tap to unlock your trading keys in your Monzero wallet.");
                    return ok(new JSONObject().put("ok", true));
                }
                if (!engine.walletReady()) return ok(new JSONObject().put("ok", false).put("message", "The trading wallet is still starting."));
                if (r.path.equals("/v1/xmz-add")) {
                    final String xmz = body.optString("xmz");
                    if (!xmz.isEmpty() && !XMZ.matcher(xmz).matches()) return fail(400, "xmz");
                    listener.xmzAddRequested(engine.tradingAddress(), xmz);
                } else {
                    returnWanted = true;
                    listener.bridgeChanged();
                    listener.attention("xmz-return", "Move XMZ back to your main balance", "Tap to confirm it in your Monzero wallet.");
                }
                return ok(new JSONObject().put("ok", true));
            }
            // Giving up a trade that was never approved moves nothing: the page may ask for it
            case "/v1/abandon":
                return ok(new JSONObject().put("ok", engine.abandonTrade()));
            case "/v1/disconnect":
                disconnectPage();
                return ok(new JSONObject().put("ok", true));
            default:
                return fail(404, "unknown");
        }
    }

    private void serve(Socket socket, int port) {
        try (Socket s = socket) {
            s.setSoTimeout(10000);
            final InputStream in = s.getInputStream();
            final ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            final byte[] chunk = new byte[4096];
            int head = -1;
            while (head < 0) {
                final int n = in.read(chunk);
                if (n < 0) return;
                buffer.write(chunk, 0, n);
                if (buffer.size() > MAX_REQUEST) return;
                head = indexOfHeaderEnd(buffer.toByteArray());
            }
            byte[] all = buffer.toByteArray();
            final String[] lines = new String(all, 0, head, "ISO-8859-1").split("\n");
            final String[] first = lines[0].trim().split(" ");
            final Request r = new Request();
            r.method = first.length > 0 ? first[0] : "";
            final String target = first.length > 1 ? first[1] : "";
            final int q = target.indexOf('?');
            r.path = q < 0 ? target : target.substring(0, q);
            r.query = q < 0 ? "" : target.substring(q + 1);
            long length = 0;
            for (int i = 1; i < lines.length; i++) {
                final String line = lines[i].trim();
                final int colon = line.indexOf(':');
                if (colon < 0) continue;
                final String name = line.substring(0, colon).toLowerCase(java.util.Locale.ROOT), value = line.substring(colon + 1).trim();
                switch (name) {
                    case "origin":
                        r.origin = value;
                        break;
                    case "host":
                        r.host = value;
                        break;
                    case "content-length":
                        try {
                            length = Long.parseLong(value);
                        } catch (NumberFormatException e) {
                            return;
                        }
                        break;
                    case "authorization":
                        if (value.startsWith("Bearer ")) r.token = value.substring(7).trim();
                        break;
                    case "access-control-request-private-network":
                        r.preflightPrivate = true;
                        break;
                    default:
                        break;
                }
            }
            if (length < 0 || length > MAX_REQUEST) return;
            while (all.length < head + 4 + length) {
                final int n = in.read(chunk);
                if (n < 0) return;
                buffer.write(chunk, 0, n);
                all = buffer.toByteArray();
            }
            r.body = java.util.Arrays.copyOfRange(all, head + 4, head + 4 + (int) length);
            // Only this loopback address by name, and only the DEX page's own origin
            final boolean hostOk = r.host.equals("127.0.0.1:" + port) || r.host.equals("localhost:" + port);
            final boolean allowed = hostOk && originAllowed(r.origin);
            int status = 403;
            byte[] out = "{\"ok\":false,\"error\":\"origin\"}".getBytes("UTF-8");
            if (allowed) {
                if (r.method.equals("OPTIONS")) {
                    status = 204;
                    out = new byte[0];
                } else {
                    final Reply reply = engine.call(() -> respond(r), null);
                    if (reply == null) {
                        status = 503;
                        out = "{\"ok\":false,\"error\":\"busy\"}".getBytes("UTF-8");
                    } else {
                        status = reply.status;
                        out = reply.body.toString().getBytes("UTF-8");
                    }
                }
            }
            final StringBuilder reply = new StringBuilder("HTTP/1.1 ").append(status).append(status == 200 ? " OK" : status == 204 ? " No Content" : " Error").append("\r\n");
            if (allowed) {
                reply.append("Access-Control-Allow-Origin: ").append(r.origin).append("\r\nVary: Origin\r\n")
                        .append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: content-type, authorization\r\n")
                        .append("Access-Control-Max-Age: 600\r\n");
                if (r.preflightPrivate) reply.append("Access-Control-Allow-Private-Network: true\r\n");
            }
            reply.append("Content-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ").append(out.length).append("\r\n\r\n");
            final OutputStream os = s.getOutputStream();
            os.write(reply.toString().getBytes("ISO-8859-1"));
            os.write(out);
            os.flush();
        } catch (Exception ignored) {
        }
    }

    private static int indexOfHeaderEnd(byte[] b) {
        for (int i = 0; i + 3 < b.length; i++) if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') return i;
        return -1;
    }
}
