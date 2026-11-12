package com.m2049r.xmrwallet.dex;

import android.os.Handler;

import org.json.JSONObject;

import java.io.File;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Pattern;

/**
 * The app's own XMZ trading wallet ("dex"), in its own wallet RPC service, and one more service per
 * running trade for that trade's shared swap wallet. The user's main wallet is never opened here:
 * XMZ for trading is sent to the trading wallet and sent back when done. (The phone's version of
 * the desktop wallet's DexWalletService.) Everything runs on the engine's thread.
 */
final class DexWallets {
    interface Listener {
        void walletsChanged();
    }

    interface Endpoint {
        /** null when the service could not start */
        void ready(JSONObject endpoint);
    }

    private static final String WALLET_NAME = "dex";
    private static final Pattern ADDRESS = Pattern.compile("\\A[0-9A-Za-z]{90,110}\\z");
    private static final Pattern ATOMS = Pattern.compile("\\A[1-9][0-9]{0,19}\\z");
    private static final Pattern TRADE = Pattern.compile("\\A0x[0-9a-f]{64}\\z");

    private final File root, program;
    private final String daemon, networkFlag;
    private final DexHelper helper;
    private final Handler handler;
    private final Listener listener;
    private String state = "configured", error = "";
    private DexWalletRpc main;
    private JSONObject wallet = new JSONObject();
    private int running;
    private boolean stopping;
    private final Map<String, DexWalletRpc> trades = new HashMap<>();
    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            refresh();
            handler.postDelayed(this, 30000);
        }
    };

    DexWallets(File root, File program, String daemon, String networkFlag, DexHelper helper, Handler handler, Listener listener) {
        this.root = root;
        this.program = program;
        this.daemon = daemon;
        this.networkFlag = networkFlag;
        this.helper = helper;
        this.handler = handler;
        this.listener = listener;
    }

    File root() {
        return root;
    }

    boolean ready() {
        return state.equals("ready");
    }

    private File passwordFile() {
        return new File(root, "xmz-wallet.password");
    }

    private boolean makePasswordFile() {
        if (passwordFile().isFile()) return true;
        final byte[] secret = new byte[24];
        new SecureRandom().nextBytes(secret);
        return DexFiles.write(passwordFile(), DexFiles.hex(secret));
    }

    private void fail(String why) {
        state = "failed";
        if (why != null && !why.isEmpty()) error = why;
        listener.walletsChanged();
    }

    boolean start() {
        if (!state.equals("configured") && !state.equals("stopped") && !state.equals("failed")) return false;
        if (!program.isFile() || !helper.available()) {
            fail("This build has no trading programs for this phone.");
            return false;
        }
        if (!DexFiles.privateDir(root) || !makePasswordFile()) {
            fail("The DEX folder could not be created.");
            return false;
        }
        final File dir = new File(root, "xmz-wallet");
        final int port = DexWalletRpc.keptPort(new File(root, "xmz-wallet.port"), DexWalletRpc.freePort());
        main = new DexWalletRpc(dir, port);
        state = "starting";
        error = "";
        stopping = false;
        listener.walletsChanged();
        if (!main.start(program, daemon, networkFlag)) {
            main = null;
            fail("The trading wallet service could not start.");
            return false;
        }
        waitForLogin(main, 0, () -> openWallet());
        return true;
    }

    /** The login file appears once the service is listening. */
    private void waitForLogin(final DexWalletRpc service, final int tries, final Runnable listening) {
        handler.postDelayed(() -> {
            if (stopping || service != main && !trades.containsValue(service)) return;
            if (service.listening()) {
                listening.run();
                return;
            }
            if (!service.running() || tries > 240) {
                if (service == main) fail("The trading wallet service did not start.");
                else listening.run();
                return;
            }
            waitForLogin(service, tries + 1, listening);
        }, 250);
    }

    JSONObject mainEndpoint() {
        if (main == null || !ready()) return null;
        return endpoint(main);
    }

    private static JSONObject endpoint(DexWalletRpc service) {
        try {
            return new JSONObject().put("url", "http://127.0.0.1:" + service.port).put("loginFile", service.loginFile().getPath());
        } catch (Exception e) {
            return null;
        }
    }

    private boolean runHelper(JSONObject walletRequest, final DexHelper.Done done) {
        try {
            final JSONObject request = new JSONObject().put("schema", DexHelper.REQUEST_SCHEMA).put("command", "xmzWallet")
                    .put("tradeDir", root.getPath()).put("xmzWallet", walletRequest);
            running++;
            helper.run(request, 120000, 65536, answer -> {
                running--;
                done.done(answer);
            });
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private JSONObject walletRequest(String action) throws Exception {
        return new JSONObject().put("wallet", endpoint(main)).put("action", action).put("name", WALLET_NAME)
                .put("passwordFile", passwordFile().getPath());
    }

    private static String shortError(JSONObject answer) {
        final String e = answer == null ? "the helper gave no answer" : answer.optString("error");
        return e.length() > 300 ? e.substring(0, 300) : e;
    }

    private int openTries;

    private void openWallet() {
        try {
            runHelper(walletRequest("open"), answer -> {
                if (stopping) return;
                if (answer == null || !answer.optBoolean("ok")) {
                    // The service may still be coming up (a slow phone): a few more tries
                    if (main != null && main.running() && ++openTries < 6) handler.postDelayed(this::openWallet, 3000);
                    else fail(shortError(answer));
                    return;
                }
                openTries = 0;
                wallet = answer;
                state = "ready";
                error = "";
                handler.removeCallbacks(poll);
                handler.postDelayed(poll, 30000);
                listener.walletsChanged();
            });
        } catch (Exception e) {
            fail("The trading wallet could not be opened.");
        }
    }

    boolean refresh() {
        if (!ready() || running > 0) return false;
        try {
            return runHelper(walletRequest("status"), answer -> {
                if (answer != null && answer.optBoolean("ok")) {
                    wallet = answer;
                    error = "";
                } else error = shortError(answer);
                listener.walletsChanged();
            });
        } catch (Exception e) {
            return false;
        }
    }

    /** Send from the trading wallet: `atoms`, or everything when it is empty. */
    boolean send(String address, String atoms) {
        if (!ready() || address == null || !ADDRESS.matcher(address).matches()
                || (atoms != null && !atoms.isEmpty() && !ATOMS.matcher(atoms).matches())) return false;
        try {
            final JSONObject request = walletRequest("send").put("to", address);
            if (atoms != null && !atoms.isEmpty()) request.put("atoms", atoms);
            return runHelper(request, answer -> {
                if (answer != null && answer.optBoolean("ok")) {
                    wallet = answer;
                    error = "";
                } else error = shortError(answer);
                listener.walletsChanged();
            });
        } catch (Exception e) {
            return false;
        }
    }

    /** The swap-wallet service of one trade (its folder is the trade's), started when needed. */
    void tradeService(final String tradeDir, String tradeId, final Endpoint done) {
        final DexWalletRpc existing = trades.get(tradeDir);
        if (existing != null && existing.running()) {
            done.ready(endpoint(existing));
            return;
        }
        if (tradeId == null || !TRADE.matcher(tradeId).matches() || !new File(tradeDir).isDirectory()) {
            done.ready(null);
            return;
        }
        final File dir = new File(tradeDir, "swap-service");
        final int port = DexWalletRpc.keptPort(new File(tradeDir, "swap-service.port"), DexWalletRpc.freePort());
        final DexWalletRpc service = new DexWalletRpc(dir, port);
        if (!service.start(program, daemon, networkFlag)) {
            done.ready(null);
            return;
        }
        trades.put(tradeDir, service);
        waitForLogin(service, 0, () -> {
            if (service.listening()) done.ready(endpoint(service));
            else {
                trades.remove(tradeDir);
                service.stop();
                done.ready(null);
            }
        });
    }

    void releaseTradeService(String tradeDir) {
        final DexWalletRpc service = trades.remove(tradeDir);
        if (service != null) service.stop();
    }

    void stop() {
        stopping = true;
        handler.removeCallbacks(poll);
        if (main != null) main.stop();
        main = null;
        for (DexWalletRpc s : trades.values()) s.stop();
        trades.clear();
        state = "stopped";
    }

    String address() {
        return wallet.optString("address");
    }

    String unlockedAtoms() {
        return wallet.optString("unlockedAtoms", "0");
    }

    JSONObject status() {
        try {
            return new JSONObject().put("state", state).put("error", error).put("busy", running > 0)
                    .put("address", wallet.optString("address"))
                    .put("balance", DexFiles.units(wallet.optString("balanceAtoms", "0"), 11))
                    .put("unlocked", DexFiles.units(wallet.optString("unlockedAtoms", "0"), 11))
                    .put("height", wallet.optLong("height")).put("trades", trades.size());
        } catch (Exception e) {
            return new JSONObject();
        }
    }
}
