package com.m2049r.xmrwallet.dex;

import org.json.JSONObject;

import java.io.File;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * The one running trade (the phone's version of the desktop wallet's EvmTradeJob): its state as
 * the helper's journal holds it, the user's approval, and one step at a time until it is done.
 * Runs on the engine's thread.
 */
final class DexTrade {
    interface Listener {
        void tradeChanged();
    }

    private static final Pattern HASH = Pattern.compile("\\A0x[0-9a-f]{64}\\z"), STAGE = Pattern.compile("\\A[A-Za-z]{1,32}(\\([A-Za-z]{1,16}\\))?\\z"),
            WEI = Pattern.compile("\\A[0-9]{1,78}\\z");
    private static final Set<String> ALLOWED = new HashSet<>(Arrays.asList("schema", "ok", "role", "stage", "done", "approved", "commitment",
            "offer", "deadlines", "evmTxs", "xmzLock", "xmzSweeps", "awaitingApproval", "account", "awaitingFunding", "payout", "progress", "solAccount"));

    private final DexHelper helper;
    private final Listener listener;
    private String tradeDir = "", rpc = "", trade = "", commitment = "", command = "", state = "unconfigured", error = "", stepError = "", pendingApproval = "";
    private JSONObject mainWallet, swapWallet, snapshot = new JSONObject(), funding, payout;
    private boolean busy, awaitingApproval;

    DexTrade(DexHelper helper, Listener listener) {
        this.helper = helper;
        this.listener = listener;
    }

    boolean configured() {
        return !tradeDir.isEmpty();
    }

    boolean busy() {
        return busy;
    }

    boolean done() {
        return snapshot.optBoolean("done");
    }

    String tradeDir() {
        return tradeDir;
    }

    String tradeId() {
        return trade;
    }

    JSONObject snapshot() {
        return snapshot;
    }

    JSONObject offer() {
        return snapshot.optJSONObject("offer");
    }

    boolean hasPayout() {
        return payout != null && payout.length() > 0;
    }

    boolean configure(String dir, String evmRpc, JSONObject main, JSONObject swap) {
        if (configured() || busy || main == null || swap == null || !new File(dir).isDirectory()) return false;
        tradeDir = dir;
        rpc = evmRpc;
        mainWallet = main;
        swapWallet = swap;
        state = "ready";
        listener.tradeChanged();
        return true;
    }

    /** A finished trade leaves, making room for the next one. */
    boolean release() {
        if (!configured()) return true;
        if (busy || !done()) return false;
        tradeDir = rpc = trade = commitment = command = error = stepError = pendingApproval = "";
        mainWallet = swapWallet = funding = payout = null;
        snapshot = new JSONObject();
        awaitingApproval = false;
        state = "unconfigured";
        listener.tradeChanged();
        return true;
    }

    /** Give up a trade that was never approved: nothing of it was ever locked. */
    boolean abandon() {
        final String stage = snapshot.optString("stage");
        if (!configured() || busy || snapshot.optBoolean("approved") || (!stage.equals("Start") && !stage.equals("WaitingForEvmLock"))) return false;
        if (!DexFiles.write(new File(tradeDir, "abandoned"), "abandoned before approval\n")) return false;
        try {
            snapshot.put("done", true);
        } catch (Exception ignored) {
        }
        return release();
    }

    private JSONObject request(String cmd) throws Exception {
        return new JSONObject().put("schema", DexHelper.REQUEST_SCHEMA).put("command", cmd).put("tradeDir", tradeDir);
    }

    private JSONObject tickConfig(String seedHex) throws Exception {
        if (seedHex == null) return null;
        return new JSONObject().put("evmRpc", rpc).put("walletSeedHex", seedHex).put("accountIndex", 0)
                .put("mainWallet", mainWallet).put("swapWallet", swapWallet);
    }

    boolean refresh() {
        try {
            return run(request("status"));
        } catch (Exception e) {
            return false;
        }
    }

    /** Only what the user was shown for this trade. */
    boolean approve(String shown) {
        if (commitment.isEmpty() || !commitment.equals(shown)) return false;
        // A step is running (they take a while): send the approval as soon as it ends
        if (busy) {
            pendingApproval = shown;
            listener.tradeChanged();
            return true;
        }
        try {
            return run(request("approve").put("commitment", shown));
        } catch (Exception e) {
            return false;
        }
    }

    boolean tick(String seedHex) {
        try {
            final JSONObject t = tickConfig(seedHex);
            return t != null && run(request("tick").put("tick", t));
        } catch (Exception e) {
            return false;
        }
    }

    /** After the trade: what its account holds goes to the user's own wallet. */
    boolean payout(String seedHex, String address) {
        final JSONObject offer = offer();
        if (!done() || offer == null || !DexConfig.address(offer.optLong("chainId"), address)) return false;
        try {
            final JSONObject t = tickConfig(seedHex);
            return t != null && run(request("payout").put("tick", t).put("payoutAddress", address));
        } catch (Exception e) {
            return false;
        }
    }

    private boolean run(JSONObject request) {
        if (busy || !configured() || !helper.available()) return false;
        command = request.optString("command");
        busy = true;
        state = "running";
        error = "";
        helper.run(request, command.equals("tick") ? 300000 : 30000, 65536, this::finish);
        listener.tradeChanged();
        return true;
    }

    /** What the app may show: the journal view, nothing that could carry a secret. */
    private static boolean snapshotValid(JSONObject r) {
        for (Iterator<String> it = r.keys(); it.hasNext(); ) if (!ALLOWED.contains(it.next())) return false;
        final JSONObject offer = r.optJSONObject("offer");
        if (offer == null || !HASH.matcher(offer.optString("tradeId")).matches()) return false;
        final long chain = offer.optLong("chainId");
        if (r.has("progress") && !r.isNull("progress")) {
            final JSONObject progress = r.optJSONObject("progress");
            if (progress == null || progress.length() > 2) return false;
            for (Iterator<String> it = progress.keys(); it.hasNext(); ) {
                final String key = it.next();
                final JSONObject count = progress.optJSONObject(key);
                if ((!key.equals("xmzLock") && !key.equals("xmzSweep")) || count == null || count.length() != 2) return false;
                final long have = count.optLong("confirmations", -1), needed = count.optLong("needed", -1);
                if (have < 0 || needed < 1 || needed > 1000 || have > needed) return false;
            }
        }
        if (r.has("account") && !DexConfig.evmAddress(r.optString("account"))) return false;
        if (r.has("solAccount") && !(DexConfig.solana(chain) && DexConfig.solanaAddress(r.optString("solAccount")))) return false;
        if (r.has("awaitingFunding") && !r.isNull("awaitingFunding")) {
            final JSONObject f = r.optJSONObject("awaitingFunding");
            if (f == null || f.length() != 3) return false;
            for (String k : new String[]{"neededWei", "haveWei", "missingWei"}) if (!WEI.matcher(f.optString(k)).matches()) return false;
        }
        if (r.has("payout")) {
            final JSONObject p = r.optJSONObject("payout");
            if (p == null || p.length() != 3 || !DexConfig.address(chain, p.optString("to"))) return false;
            final boolean sent = p.has("tx");
            if (sent ? !(DexConfig.transactionId(p.optString("tx")) && WEI.matcher(p.optString("amountWei")).matches())
                    : !(Boolean.FALSE.equals(p.opt("sent")) && p.opt("reason") instanceof String)) return false;
        }
        final String role = r.optString("role");
        return (role.equals("owner") || role.equals("claimer")) && STAGE.matcher(r.optString("stage")).matches()
                && r.opt("done") instanceof Boolean && r.opt("approved") instanceof Boolean && HASH.matcher(r.optString("commitment")).matches()
                && r.optJSONObject("evmTxs") != null && (!r.has("awaitingApproval") || r.opt("awaitingApproval") instanceof Boolean);
    }

    private void finish(JSONObject r) {
        busy = false;
        // A queued approval is dropped when the step fails: the user approves again after reading the error
        if (r == null || !r.optBoolean("ok")) {
            pendingApproval = "";
            state = r == null ? "failed" : "refused";
            final String e = r == null ? "no valid answer from the helper" : r.optString("error");
            error = e.length() > 300 ? e.substring(0, 300) : e;
            // A failed step leaves the trade where it was: show its stage next to the error
            if (command.equals("tick")) {
                stepError = error;
                listener.tradeChanged();
                refresh();
                return;
            }
            listener.tradeChanged();
            return;
        }
        final JSONObject offer = r.optJSONObject("offer");
        final String id = offer == null ? "" : offer.optString("tradeId"), shown = r.optString("commitment");
        if (!snapshotValid(r) || (!trade.isEmpty() && (!id.equals(trade) || !shown.equals(commitment)))) {
            pendingApproval = "";
            state = "mismatch";
            error = "the helper answered for another trade";
            listener.tradeChanged();
            return;
        }
        trade = id;
        commitment = shown;
        snapshot = r;
        awaitingApproval = command.equals("tick") && r.optBoolean("awaitingApproval");
        if (command.equals("tick")) {
            funding = r.optJSONObject("awaitingFunding");
            stepError = "";
        }
        if (command.equals("payout")) payout = r.optJSONObject("payout");
        state = "ok";
        listener.tradeChanged();
        if (!pendingApproval.isEmpty()) {
            final String pending = pendingApproval;
            pendingApproval = "";
            if (!snapshot.optBoolean("approved") && !approve(pending)) {
                state = "refused";
                error = "the approval could not be sent";
                listener.tradeChanged();
            }
        }
    }

    boolean awaitingApproval() {
        return awaitingApproval && !snapshot.optBoolean("approved") && pendingApproval.isEmpty();
    }

    String commitment() {
        return commitment;
    }

    JSONObject status(String startRefusal) {
        try {
            return new JSONObject().put("configured", configured()).put("busy", busy).put("state", state).put("error", error)
                    .put("tradeId", trade).put("commitment", commitment).put("awaitingApproval", awaitingApproval).put("snapshot", snapshot)
                    .put("approvalPending", !pendingApproval.isEmpty()).put("stepError", stepError)
                    .put("account", snapshot.optString("account")).put("solAccount", snapshot.optString("solAccount"))
                    .put("awaitingFunding", funding == null ? new JSONObject() : funding).put("payout", payout == null ? new JSONObject() : payout)
                    .put("startRefusal", startRefusal);
        } catch (Exception e) {
            return new JSONObject();
        }
    }
}
