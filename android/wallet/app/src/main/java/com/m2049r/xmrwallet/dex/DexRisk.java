package com.m2049r.xmrwallet.dex;

import android.os.Handler;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.math.BigInteger;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;

/**
 * The owner's kill switch and limits, as on the desktop wallet (DexRiskControl): a new trade starts
 * only under a valid, unexpired control document signed with the pinned key, within its per-trade
 * and 24-hour limits. Documents are fetched from monzero.org for each traded network. A trade
 * already started is never blocked. Runs on the engine's thread.
 */
final class DexRisk {
    private static final long DAY = 86400, RETENTION = 7 * DAY, MAX_AGE = 30 * DAY;
    private static final Pattern ATOMS = Pattern.compile("\\A(?:0|[1-9][0-9]{0,18})\\z");
    private static final Pattern HEX128 = Pattern.compile("\\A[0-9a-f]{128}\\z");
    private static final Pattern UUID = Pattern.compile("\\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z");
    private static final BigInteger MAX_SAFE = BigInteger.valueOf(9007199254740991L);

    private final byte[] publicKey;
    private final File ledgerFile;
    private final Handler handler;
    private final Runnable changed;
    private final ExecutorService network = Executors.newSingleThreadExecutor();
    /** By control network ("eth-mainnet"): the newest valid payload */
    private final Map<String, JSONObject> documents = new HashMap<>();
    private final Map<String, String> errors = new HashMap<>();
    private final Map<String, Boolean> polled = new HashMap<>();
    private boolean stopped;
    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            for (String n : polled.keySet()) fetch(n);
            handler.postDelayed(this, 600000);
        }
    };

    DexRisk(String publicKeyHex, File ledgerFile, Handler handler, Runnable changed) {
        final byte[] key = DexFiles.unhex(publicKeyHex);
        this.publicKey = key != null && key.length == 32 ? key : null;
        this.ledgerFile = ledgerFile;
        this.handler = handler;
        this.changed = changed;
    }

    static boolean documentRequired(String controlNetwork) {
        return !controlNetwork.endsWith("-regtest");
    }

    /** Start fetching this network's document (mainnets only; test networks have none). */
    void watch(String controlNetwork) {
        if (!documentRequired(controlNetwork) || polled.containsKey(controlNetwork)) return;
        final boolean first = polled.isEmpty();
        polled.put(controlNetwork, true);
        fetch(controlNetwork);
        if (first) handler.postDelayed(poll, 600000);
    }

    void stop() {
        stopped = true;
        handler.removeCallbacks(poll);
        network.shutdownNow();
    }

    private void fetch(final String controlNetwork) {
        if (stopped) return;
        network.execute(() -> {
            byte[] body = null;
            HttpURLConnection c = null;
            try {
                c = (HttpURLConnection) new URL(DexConfig.CONTROL_BASE + controlNetwork + ".json").openConnection();
                c.setConnectTimeout(15000);
                c.setReadTimeout(15000);
                c.setUseCaches(false);
                c.setInstanceFollowRedirects(false);
                if (c.getResponseCode() == 200) {
                    final ByteArrayOutputStream out = new ByteArrayOutputStream();
                    try (InputStream in = c.getInputStream()) {
                        final byte[] buffer = new byte[1024];
                        int n;
                        while ((n = in.read(buffer)) >= 0 && out.size() <= 4096) out.write(buffer, 0, n);
                    }
                    if (out.size() <= 4096) body = out.toByteArray();
                }
            } catch (Exception ignored) {
                body = null;
            } finally {
                if (c != null) c.disconnect();
            }
            final byte[] document = body;
            handler.post(() -> {
                if (stopped) return;
                if (document == null) {
                    if (!documents.containsKey(controlNetwork)) errors.put(controlNetwork, "control document unreachable");
                } else accept(document, controlNetwork, 0);
                changed.run();
            });
        });
    }

    private static BigInteger integer(JSONObject o, String key) {
        final Object v = o.opt(key);
        if (!(v instanceof Integer) && !(v instanceof Long)) return null;
        final BigInteger n = BigInteger.valueOf(((Number) v).longValue());
        return n.signum() < 0 || n.compareTo(MAX_SAFE) > 0 ? null : n;
    }

    private static boolean atoms(JSONObject o, String key) {
        return o.opt(key) instanceof String && ATOMS.matcher(o.optString(key)).matches();
    }

    /** Check and keep a control document for `expected`; false (with the reason kept) when refused. */
    boolean accept(byte[] bytes, String expected, long now) {
        if (now <= 0) now = System.currentTimeMillis() / 1000;
        final String reason = check(bytes, expected, now);
        if (reason != null) {
            errors.put(expected, reason);
            return false;
        }
        errors.remove(expected);
        return true;
    }

    private String check(byte[] bytes, String expected, long now) {
        if (publicKey == null) return "no control key configured";
        final JSONObject envelope, payload;
        final String body, signatureHex;
        try {
            if (bytes.length > 4096) return "malformed control document";
            envelope = new JSONObject(new String(bytes, "UTF-8"));
            if (envelope.length() != 2 || !(envelope.opt("payload") instanceof String)) return "malformed control document";
            body = envelope.getString("payload");
            signatureHex = envelope.optString("signature");
            if (!HEX128.matcher(signatureHex).matches()) return "malformed control document";
            final byte[] text = body.getBytes("UTF-8");
            final byte[] domain = "monzero-dex-control-v1".getBytes("UTF-8");
            final byte[] message = new byte[domain.length + 1 + text.length];
            System.arraycopy(domain, 0, message, 0, domain.length);
            System.arraycopy(text, 0, message, domain.length + 1, text.length);
            if (!DexNative.verify(DexFiles.unhex(signatureHex), message, publicKey)) return "control signature invalid";
            payload = new JSONObject(body);
        } catch (Exception e) {
            return "malformed control document";
        }
        final BigInteger sequence = integer(payload, "sequence"), issued = integer(payload, "issuedAt"), expires = integer(payload, "expiresAt");
        if (payload.length() != 9 || !"monzero-dex-control-v1".equals(payload.optString("schema")) || !(payload.opt("newTradesEnabled") instanceof Boolean)
                || sequence == null || issued == null || expires == null || !atoms(payload, "maxLtcAtomsPerTrade")
                || !atoms(payload, "maxXmzAtomsPerTrade") || !atoms(payload, "maxLtcAtomsPerDay")) return "malformed control payload";
        if (!expected.equals(payload.optString("network"))) return "control document is for another network";
        final long from = issued.longValue(), until = expires.longValue();
        if (until <= from || until - from > MAX_AGE || from > now + 300) return "control document validity window rejected";
        if (until <= now) return "control document expired";
        // Never roll back to an older document (a replayed "enabled" after a stop)
        final JSONObject kept = documents.get(expected);
        if (kept != null && sequence.longValue() < kept.optLong("sequence")) return "older control document ignored";
        documents.put(expected, payload);
        return null;
    }

    /**
     * What the owner's document allows on a network now, for the wallet's DEX screen: the per-trade
     * limit in the coin's atomic units, or why new trades are paused. Null for test networks.
     */
    JSONObject limits(String controlNetwork) {
        if (!documentRequired(controlNetwork)) return null;
        final JSONObject control = documents.get(controlNetwork);
        final long now = System.currentTimeMillis() / 1000;
        try {
            if (control == null) return new JSONObject().put("open", false).put("why", errors.containsKey(controlNetwork) ? errors.get(controlNetwork) : "no control document yet");
            if (control.optLong("expiresAt") <= now) return new JSONObject().put("open", false).put("why", "control document expired");
            if (!control.optBoolean("newTradesEnabled")) return new JSONObject().put("open", false).put("why", "paused by the operator");
            return new JSONObject().put("open", true).put("perTrade", control.optString("maxLtcAtomsPerTrade")).put("perDay", control.optString("maxLtcAtomsPerDay"))
                    .put("xmzPerTrade", control.optString("maxXmzAtomsPerTrade")).put("expiresAt", control.optLong("expiresAt")).put("sequence", control.optLong("sequence"));
        } catch (Exception e) {
            return null;
        }
    }

    private JSONObject ledger() {
        if (!ledgerFile.exists()) return new JSONObject();
        final JSONObject o = DexFiles.readJson(ledgerFile, 1048576);
        if (o == null || !"monzero-dex-started-trades-v1".equals(o.optString("schema"))) return null;
        return o;
    }

    /**
     * Why a new trade may not start on this network now; empty when it may. `amount` is the coin
     * side in its atomic units, `xmzAtoms` the XMZ side.
     */
    String refusal(String controlNetwork, String ledgerId, BigInteger amount, BigInteger xmzAtoms, long now) {
        if (now <= 0) now = System.currentTimeMillis() / 1000;
        if (ledgerId == null || !UUID.matcher(ledgerId).matches()) return "Trade identifier is invalid.";
        final JSONObject record = ledger();
        if (record == null) return "The local trade-limit record is unreadable; new trades are paused.";
        final JSONObject started = record.optJSONObject("trades") == null ? new JSONObject() : record.optJSONObject("trades");
        if (started.has(ledgerId)) return "";   // already counted: resuming the same trade
        if (!documentRequired(controlNetwork)) return "";
        final JSONObject control = documents.get(controlNetwork);
        if (control == null) {
            final String why = errors.get(controlNetwork);
            return why == null ? "No valid operator control document; new trades are paused." : "New trades are paused: " + why + ".";
        }
        if (control.optLong("expiresAt") <= now) return "The operator control document expired; new trades are paused.";
        if (!control.optBoolean("newTradesEnabled")) return "New trades are paused by the operator.";
        final String coin = controlNetwork.startsWith("bnb") ? "BNB" : controlNetwork.startsWith("sol") ? "SOL" : "ETH";
        if (amount.compareTo(new BigInteger(control.optString("maxLtcAtomsPerTrade"))) > 0) return coin + " amount exceeds the per-trade limit.";
        if (xmzAtoms.compareTo(new BigInteger(control.optString("maxXmzAtomsPerTrade"))) > 0) return "XMZ amount exceeds the per-trade limit.";
        BigInteger day = BigInteger.ZERO;
        for (Iterator<String> it = started.keys(); it.hasNext(); ) {
            final JSONObject entry = started.optJSONObject(it.next());
            if (entry == null) continue;
            final String on = entry.optString("network");
            if (!on.isEmpty() && !on.equals(controlNetwork)) continue;
            if (now - entry.optLong("startedAt") < DAY && ATOMS.matcher(entry.optString("ltcAtoms")).matches())
                day = day.add(new BigInteger(entry.optString("ltcAtoms")));
        }
        if (day.add(amount).compareTo(new BigInteger(control.optString("maxLtcAtomsPerDay"))) > 0) return "This trade would exceed the 24-hour " + coin + " limit.";
        return "";
    }

    /** Count a trade that is about to start; false when it could not be written (then it must not start). */
    boolean recordStart(String controlNetwork, String ledgerId, BigInteger amount, long now) {
        if (now <= 0) now = System.currentTimeMillis() / 1000;
        if (ledgerId == null || !UUID.matcher(ledgerId).matches()) return false;
        final JSONObject record = ledger();
        if (record == null) return false;
        try {
            final JSONObject started = record.optJSONObject("trades") == null ? new JSONObject() : record.optJSONObject("trades");
            if (started.has(ledgerId)) return true;
            final JSONObject kept = new JSONObject();
            for (Iterator<String> it = started.keys(); it.hasNext(); ) {
                final String key = it.next();
                final JSONObject entry = started.optJSONObject(key);
                if (entry != null && now - entry.optLong("startedAt") <= RETENTION) kept.put(key, entry);
            }
            kept.put(ledgerId, new JSONObject().put("ltcAtoms", amount.toString()).put("startedAt", now).put("network", controlNetwork));
            return DexFiles.write(ledgerFile, new JSONObject().put("schema", "monzero-dex-started-trades-v1").put("trades", kept).toString());
        } catch (Exception e) {
            return false;
        }
    }
}
