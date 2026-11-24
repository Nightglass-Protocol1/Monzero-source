package com.m2049r.xmrwallet.dex;

import android.content.Context;
import android.os.Handler;
import android.os.HandlerThread;

import org.json.JSONObject;

import java.io.File;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;

/**
 * The phone's trading engine: the part of the desktop wallet's DexEngine that the DEX web page
 * drives. It owns the trading wallet service, the trading keys, one market per chain and the one
 * running trade, and steps them with the trade helper. Everything here runs on one thread (the
 * engine's); other threads use call() and post().
 */
final class DexEngine implements DexWallets.Listener, DexMarket.Listener, DexTrade.Listener, DexDirect.Listener {
    interface Listener {
        /** On the engine's thread, after any change of wallet, keys, market or trade. */
        void engineChanged();
    }

    private static final Pattern TRADE_ID = Pattern.compile("\\A0x[0-9a-f]{64}\\z");
    /** A trade id no real trade has: checks the limits for an offer without counting anything. */
    private static final String OFFER_CHECK_ID = "6f666665-722d-4368-9563-6b0000000000";

    private final HandlerThread thread = new HandlerThread("dex-engine");
    private final Handler handler;
    private final File root;
    private final DexConfig.Overrides overrides;
    private final DexHelper helper;
    private final DexKeys keys;
    private final DexWallets wallets;
    /** The Monero trading wallet (Monero's official wallet RPC on a Monero node); null where this build has no Monero program */
    private final DexWallets xmrWallets;
    private final DexRisk risk;
    private final DexTrade trade;
    private final Map<Long, DexMarket> markets = new LinkedHashMap<>();
    /** Monero markets (BNB/XMR, ...), by chain id like the XMZ ones */
    private final Map<Long, DexMarket> xmrMarkets = new LinkedHashMap<>();
    /** Direct (coin <-> coin) trades: test networks only until direct pairs are opened */
    private DexDirect direct;
    private String directMessage = "";
    private final java.util.Set<String> directRecorded = new java.util.HashSet<>();
    private final List<Listener> listeners = new ArrayList<>();
    private String marketMessage = "", startRefusal = "", keysMessage = "", evmAccount = "", solAccount = "", reportedTrade = "";
    private boolean keysBusy, resumed, resuming, stopped, marketsUp, xmrMarketsUp, accountsAsked;
    private String xmrMessage = "";
    private final List<String> resume = new ArrayList<>();   // trade folders, newest first
    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            if (stopped) return;
            stepMarkets();
            stepTrade();
            handler.postDelayed(this, 20000);
        }
    };

    DexEngine(Context context, String daemon) {
        final Context app = context.getApplicationContext();
        thread.start();
        handler = new Handler(thread.getLooper());
        root = new File(new File(app.getFilesDir(), "dex"), DexConfig.networkFolder());
        DexFiles.privateDir(root);
        overrides = new DexConfig.Overrides(root);
        final File bin = new File(app.getApplicationInfo().nativeLibraryDir);
        helper = new DexHelper(new File(bin, "libmonzero_dex_helper.so"), root, handler);
        keys = new DexKeys(app, root);
        final String node = !overrides.daemon.isEmpty() ? overrides.daemon : DexConfig.daemonAddress(daemon) ? daemon : DexConfig.defaultDaemon();
        wallets = new DexWallets(root, new File(bin, "libmonzero_wallet_rpc.so"), node, DexConfig.networkFlag(), helper, handler, this);
        // Monero's own wallet RPC (Monero's official Android release, arm64): Monero pairs only where it is in this build
        final File moneroRpc = new File(bin, "libmonero_wallet_rpc.so");
        final String xmrNode = !overrides.xmrDaemon.isEmpty() ? overrides.xmrDaemon : DexConfig.defaultXmrDaemon();
        xmrWallets = moneroRpc.isFile()
                ? new DexWallets("xmr", new File(root, "xmr"), moneroRpc, xmrNode, DexConfig.xmrNetworkFlag(), helper, handler, this) : null;
        risk = new DexRisk(DexConfig.CONTROL_KEY, new File(root, "started-trades.json"), handler, this::changed);
        trade = new DexTrade(helper, this);
        trade.setRelays(overrides.relaysOrDefault());
    }

    // ---- threads -------------------------------------------------------------------------------

    void post(Runnable r) {
        handler.post(r);
    }

    /** Run on the engine's thread and wait for the result (from another thread). */
    <T> T call(Callable<T> c, T fallback) {
        if (Thread.currentThread() == thread) {
            try {
                return c.call();
            } catch (Exception e) {
                return fallback;
            }
        }
        final FutureTask<T> task = new FutureTask<>(c);
        if (!handler.post(task)) return fallback;
        try {
            return task.get(20, TimeUnit.SECONDS);
        } catch (Exception e) {
            return fallback;
        }
    }

    void addListener(Listener l) {
        handler.post(() -> listeners.add(l));
    }

    private void changed() {
        if (stopped) return;
        for (Listener l : new ArrayList<>(listeners)) l.engineChanged();
    }

    // ---- start and stop ------------------------------------------------------------------------

    void start() {
        handler.post(() -> {
            for (long chain : DexConfig.chains()) risk.watch(DexConfig.controlNetwork(chain));
            wallets.start();
            if (xmrWallets != null) xmrWallets.start();
            handler.postDelayed(ticker, 5000);
        });
    }

    /** True while a trade is running: the app must stay up until it finishes or refunds. */
    boolean tradeRunning() {
        return (trade.configured() && !trade.done() && trade.snapshot().optBoolean("approved")) || (direct != null && direct.hasRunningTrade());
    }

    void stop() {
        handler.post(() -> {
            stopped = true;
            handler.removeCallbacks(ticker);
            wallets.stop();
            if (xmrWallets != null) xmrWallets.stop();
            risk.stop();
            keys.lock();
            helper.shutdown();
            listeners.clear();
            thread.quitSafely();
        });
    }

    // ---- wallet and keys -----------------------------------------------------------------------

    @Override
    public void walletsChanged() {
        if (wallets.ready() && !marketsUp) setUpMarkets();
        if (marketsUp && !xmrMarketsUp && xmrWallets != null && xmrWallets.ready()) setUpXmrMarkets();
        changed();
    }

    JSONObject walletStatus() {
        final JSONObject s = wallets.status();
        try {
            s.put("keysReady", keys.ready()).put("keysBusy", keysBusy).put("keysExist", keys.exist()).put("tradeAccount", evmAccount)
                    .put("keysMessage", keysMessage);
            if (!solAccount.isEmpty()) s.put("solTradeAccount", solAccount);
        } catch (Exception ignored) {
        }
        return s;
    }

    boolean keysReady() {
        return keys.ready();
    }

    boolean keysExist() {
        return keys.exist();
    }

    boolean keysBusy() {
        return keysBusy;
    }

    String keysMessage() {
        return keysMessage;
    }

    /** Make the trading keys (first use) or unlock them. The password hash runs off the engine's thread. */
    boolean unlockKeys(final String password) {
        if (keysBusy || password == null || password.isEmpty() || keys.ready()) return false;
        keysBusy = true;
        keysMessage = "";
        changed();
        final boolean fresh = !keys.exist();
        final Thread work = new Thread(() -> {
            final boolean ok = fresh ? keys.create(password) : keys.unlock(password);
            handler.post(() -> {
                keysBusy = false;
                if (!ok) keysMessage = fresh ? "The trading keys could not be made." : "That is not the trading password.";
                else {
                    keysMessage = "";
                    askAccounts();
                    resumeTrades();
                }
                changed();
            });
        }, "dex-keys");
        work.start();
        return true;
    }

    /** The recovery words of the trading keys (slow: call off the main thread); null on a wrong password. */
    String recoveryWords(String password) {
        return keys.words(password);
    }

    /** This wallet's trade accounts for these keys: the EVM address and the Solana address. */
    private void askAccounts() {
        final String seed = keys.seedHex();
        if (seed == null || accountsAsked) return;
        accountsAsked = true;
        try {
            final JSONObject nowhere = new JSONObject().put("url", "http://127.0.0.1:1");
            final JSONObject tick = new JSONObject().put("evmRpc", "http://127.0.0.1:1").put("walletSeedHex", seed).put("accountIndex", 0)
                    .put("mainWallet", nowhere).put("swapWallet", nowhere);
            helper.run(new JSONObject().put("schema", DexHelper.REQUEST_SCHEMA).put("command", "accounts").put("tradeDir", root.getPath()).put("tick", tick),
                    30000, 4096, answer -> {
                        accountsAsked = false;
                        if (answer == null || !answer.optBoolean("ok")) return;
                        if (DexConfig.evmAddress(answer.optString("evm"))) evmAccount = answer.optString("evm");
                        if (DexConfig.solanaAddress(answer.optString("sol"))) solAccount = answer.optString("sol");
                        for (DexMarket m : allMarkets()) m.setSolAccount(solAccount);
                        changed();
                    });
        } catch (Exception e) {
            accountsAsked = false;
        }
    }

    String tradingAddress() {
        return wallets.address();
    }

    String tradingUnlocked() {
        return DexFiles.units(wallets.unlockedAtoms(), 11);
    }

    boolean walletReady() {
        return wallets.ready();
    }

    boolean refreshWallet() {
        return wallets.refresh();
    }

    /** Everything in the trading wallet back to the user's own wallet address. */
    boolean returnXmz(String homeAddress) {
        return wallets.send(homeAddress, "");
    }

    // ---- the Monero trading wallet --------------------------------------------------------------

    /** Whether this build can trade Monero pairs (it has Monero's wallet RPC for this phone) */
    boolean xmrAvailable() {
        return xmrWallets != null;
    }

    JSONObject xmrWalletStatus() {
        if (xmrWallets == null) {
            try {
                return new JSONObject().put("state", "unavailable").put("coin", "xmr");
            } catch (Exception e) {
                return new JSONObject();
            }
        }
        final JSONObject s = xmrWallets.status();
        try {
            s.put("message", xmrMessage);
        } catch (Exception ignored) {
        }
        return s;
    }

    String xmrAddress() {
        return xmrWallets == null ? "" : xmrWallets.address();
    }

    boolean refreshXmrWallet() {
        return xmrWallets != null && xmrWallets.refresh();
    }

    /** Send from the Monero trading wallet to the user's own Monero wallet (Cake Wallet, ...): an amount, or everything when empty. */
    boolean sendXmr(String address, String amount) {
        if (xmrWallets == null || !xmrWallets.ready()) return false;
        if (trade.configured() && !trade.done()) {
            xmrMessage = "A trade is running: its Monero is needed until it finishes.";
            changed();
            return false;
        }
        String atoms = "";
        if (amount != null && !amount.trim().isEmpty()) {
            atoms = DexFiles.atomic(amount.trim(), 12);
            if (atoms == null) {
                xmrMessage = "Check the XMR amount.";
                changed();
                return false;
            }
        }
        final boolean started = xmrWallets.send(address == null ? "" : address.trim(), atoms);
        xmrMessage = started ? "" : "Check the Monero address (it starts with 4 or 8).";
        changed();
        return started;
    }

    // ---- markets -------------------------------------------------------------------------------

    /** Every market: XMZ, then Monero */
    private List<DexMarket> allMarkets() {
        final List<DexMarket> all = new ArrayList<>(markets.values());
        all.addAll(xmrMarkets.values());
        return all;
    }

    /** The Monero markets, once both the XMZ wallet (the helper and keys) and the Monero wallet run */
    private void setUpXmrMarkets() {
        if (xmrMarketsUp || !marketsUp || xmrWallets == null || !xmrWallets.ready()) return;
        xmrMarketsUp = true;
        final List<String> witnesses = !overrides.xmrWitnesses.isEmpty() ? overrides.xmrWitnesses : DexConfig.xmrWitnesses();
        final JSONObject main = xmrWallets.mainEndpoint();
        for (long chain : DexConfig.xmrChains()) {
            final DexMarket m = new DexMarket(chain, true, witnesses, overrides.rpcFor(chain),
                    new File(new File(root, "evm-trades"), "xmr-" + chain), overrides.relaysOrDefault(), overrides.localContract, helper, this);
            m.setMainWallet(main);
            m.setSolAccount(solAccount);
            xmrMarkets.put(chain, m);
        }
        // A Monero trade may have been running when the app closed: after the first resume, it joins the queue
        if (!resumed) resumeTrades();
        else if (!resuming && !trade.configured()) {
            for (DexMarket m : xmrMarkets.values()) resume.addAll(m.agreedFolders());
            nextResume();
        }
        changed();
    }

    private void setUpMarkets() {
        if (marketsUp || !wallets.ready()) return;
        marketsUp = true;
        final JSONObject main = wallets.mainEndpoint();
        for (long chain : DexConfig.chains()) {
            final DexMarket m = new DexMarket(chain, overrides.rpcFor(chain), new File(new File(root, "evm-trades"), String.valueOf(chain)),
                    overrides.relaysOrDefault(), overrides.localContract, helper, this);
            m.setMainWallet(main);
            m.setSolAccount(solAccount);
            markets.put(chain, m);
        }
        // Direct trades: each chain's own coin, on the same nodes. Not on the main networks until
        // direct pairs are opened there (the relays refuse such offers until then too).
        if (!DexConfig.mainnet()) {
            final Map<Long, String> rpcs = new LinkedHashMap<>();
            final List<Long> tokens = new ArrayList<>();
            for (long chain : DexConfig.chains()) if (!DexConfig.token(chain)) rpcs.put(chain, overrides.rpcFor(chain)); else tokens.add(chain);
            if (rpcs.size() >= 2)
                direct = new DexDirect(new File(new File(root, "evm-trades"), "direct"), rpcs, tokens, overrides.relaysOrDefault(), false, "",
                        (request, done) -> helper.run(request, 120000, 8 * 1024 * 1024, done), this);
        }
        resumeTrades();
        if (xmrWallets != null && xmrWallets.ready()) setUpXmrMarkets();
        changed();
    }

    // ---- direct (coin <-> coin) trades ---------------------------------------------------------

    @Override
    public void directChanged() {
        recordDirectStarts();
        changed();
    }

    /** The direct market as the screens show it; {"configured": false} where there is none. */
    JSONObject directStatus() {
        if (direct == null) {
            try {
                return new JSONObject().put("configured", false);
            } catch (Exception e) {
                return new JSONObject();
            }
        }
        return direct.status(directMessage, keys.ready());
    }

    private boolean sayDirect(String message) {
        directMessage = message;
        changed();
        return false;
    }

    /** Both coins' kill switch and limits (amounts in base units); "" when it may go ahead. */
    private String directRefusal(String ledgerId, long givesChain, String givesUnits, long wantsChain, String wantsUnits) {
        final long[][] legs = {{givesChain}, {wantsChain}};
        final String[] units = {givesUnits, wantsUnits};
        for (int i = 0; i < 2; i++) {
            final String network = DexConfig.controlNetwork(legs[i][0]);
            if (network.isEmpty()) return DexDirect.ticker(legs[i][0]) + " is not supported for trading.";
            try {
                final String refusal = risk.refusal(network, ledgerId, new BigInteger(units[i]), BigInteger.ZERO, 0);
                if (!refusal.isEmpty()) return refusal;
            } catch (Exception e) {
                return "The amount could not be read.";
            }
        }
        return "";
    }

    boolean refreshDirect() {
        return direct != null && direct.refreshListings();
    }

    String directOrderRefusal(long givesChain, String givesAmount, long wantsChain, String wantsAmount) {
        final String g = DexFiles.atomic(givesAmount, DexDirect.decimals(givesChain)), w = DexFiles.atomic(wantsAmount, DexDirect.decimals(wantsChain));
        return g == null || w == null ? "" : directRefusal(OFFER_CHECK_ID, givesChain, g, wantsChain, w);
    }

    boolean postDirect(long givesChain, String givesAmount, long wantsChain, String wantsAmount, int hours) {
        if (direct == null) return sayDirect("Direct trades are not available in this wallet.");
        if (!keys.ready()) return sayDirect("Unlock your trading keys first.");
        final String g = DexFiles.atomic(givesAmount, DexDirect.decimals(givesChain)), w = DexFiles.atomic(wantsAmount, DexDirect.decimals(wantsChain));
        if (g == null || w == null || hours < 1 || hours > 31 * 24) return sayDirect("Check the amounts and how long the offer stays open (1 hour to 31 days).");
        if (DexDirect.ticker(givesChain).equals(DexDirect.ticker(wantsChain))) return sayDirect("Pick two different coins.");
        // The maker's posting is its approval: the limits are checked now, and counted when it is taken
        final String refusal = directRefusal(OFFER_CHECK_ID, givesChain, g, wantsChain, w);
        if (!refusal.isEmpty()) return sayDirect(refusal);
        final boolean started = direct.post(keys.seedHex(), givesChain, g, wantsChain, w, hours * 3600L);
        directMessage = started ? "" : "The offer could not be posted now.";
        changed();
        return started;
    }

    boolean takeDirect(String listingId) {
        if (direct == null) return sayDirect("Direct trades are not available in this wallet.");
        if (!keys.ready()) return sayDirect("Unlock your trading keys first.");
        final JSONObject l = direct.listing(listingId);
        if (l == null) return sayDirect("That offer is no longer listed.");
        if (!evmAccount.isEmpty() && l.optString("maker").equalsIgnoreCase(evmAccount)) return sayDirect("That is your own offer.");
        // We give what the maker wants
        final JSONObject gives = l.optJSONObject("wants"), gets = l.optJSONObject("gives");
        final String refusal = directRefusal(OFFER_CHECK_ID, gives.optLong("chainId"), gives.optString("units"), gets.optLong("chainId"), gets.optString("units"));
        if (!refusal.isEmpty()) return sayDirect(refusal);
        final boolean started = direct.take(keys.seedHex(), listingId);
        directMessage = started ? "" : "The offer could not be taken now.";
        changed();
        return started;
    }

    boolean cancelDirect(String folder) {
        return direct != null && keys.ready() && direct.cancel(keys.seedHex(), folder);
    }

    /** The taker's approval of the terms its trade shows; checked and counted under both coins' limits first. */
    boolean approveDirect(String folder, String commitment) {
        if (direct == null || !keys.ready()) return false;
        final JSONObject f = direct.folder(folder);
        final JSONObject trade = f == null ? null : f.optJSONObject("trade");
        if (trade == null || !trade.optString("commitment").equals(commitment)) return sayDirect("The trade changed; check it again.");
        final String id = ledgerId(trade.optString("tradeId"));
        final JSONObject gives = trade.optJSONObject("gives"), gets = trade.optJSONObject("gets");
        if (id == null || gives == null || gets == null) return sayDirect("The trade could not be checked; not started.");
        // Each coin under its own market's limits (USDT has its own document)
        final long gm = DexConfig.marketOf(gives.optLong("chainId"), gives.optString("asset")), wm = DexConfig.marketOf(gets.optLong("chainId"), gets.optString("asset"));
        final String refusal = directRefusal(id, gm, gives.optString("value"), wm, gets.optString("value"));
        if (!refusal.isEmpty()) return sayDirect(refusal);
        // Counted under the coin we lock (its daily limit), before anything is locked
        try {
            if (!risk.recordStart(DexConfig.controlNetwork(gm), id, new BigInteger(gives.optString("value")), 0))
                return sayDirect("The trade could not be recorded; not started.");
        } catch (Exception e) {
            return sayDirect("The trade could not be recorded; not started.");
        }
        directRecorded.add(id);
        directMessage = "";
        return direct.approve(folder, commitment);
    }

    /** A maker's trade starts when it is taken, with no click: it is counted when first seen. */
    private void recordDirectStarts() {
        if (direct == null) return;
        final org.json.JSONArray mine = direct.status("", false).optJSONArray("mine");
        for (int i = 0; mine != null && i < mine.length(); i++) {
            final JSONObject f = mine.optJSONObject(i), trade = f == null ? null : f.optJSONObject("trade");
            if (trade == null || !"offer".equals(f.optString("kind"))) continue;
            final String id = ledgerId(trade.optString("tradeId"));
            final JSONObject gives = trade.optJSONObject("gives");
            if (id == null || gives == null || directRecorded.contains(id)) continue;
            final String network = DexConfig.controlNetwork(DexConfig.marketOf(gives.optLong("chainId"), gives.optString("asset")));
            try {
                if (!network.isEmpty() && risk.recordStart(network, id, new BigInteger(gives.optString("value")), 0)) directRecorded.add(id);
            } catch (Exception ignored) {
            }
        }
    }

    /** A take of ours that its maker answered and that waits for the user's approval, or null. */
    JSONObject directAwaitingApproval() {
        if (direct == null) return null;
        final org.json.JSONArray mine = direct.status("", false).optJSONArray("mine");
        for (int i = 0; mine != null && i < mine.length(); i++) {
            final JSONObject f = mine.optJSONObject(i), trade = f == null ? null : f.optJSONObject("trade");
            if (trade != null && "take".equals(f.optString("kind")) && "agreed".equals(f.optString("state")) && !trade.optBoolean("approved")
                    && !trade.optString("commitment").isEmpty()) return f;
        }
        return null;
    }

    String directMessage() {
        return directMessage;
    }

    boolean payoutDirect(String folder, String address) {
        if (direct == null || !keys.ready()) return false;
        final boolean started = direct.payout(keys.seedHex(), folder, address);
        directMessage = started ? "" : "Check the address: it must be on the chain the coin came in on.";
        changed();
        return started;
    }

    @Override
    public void marketChanged() {
        changed();
    }

    JSONObject marketStatus() {
        final JSONObject all = new JSONObject();
        try {
            for (DexMarket m : markets.values()) all.put(m.coin(), m.status(marketMessage));
        } catch (Exception ignored) {
        }
        return all;
    }

    /** The Monero markets, by coin ("BNB", "USDT-BSC", ...), as the XMZ ones */
    JSONObject xmrMarketStatus() {
        final JSONObject all = new JSONObject();
        try {
            for (DexMarket m : xmrMarkets.values()) all.put(m.coin(), m.status(marketMessage));
        } catch (Exception ignored) {
        }
        return all;
    }

    /** One line per traded coin: what the owner-signed limits allow now (mainnet builds). */
    String limitsText() {
        final StringBuilder out = new StringBuilder();
        for (long chain : DexConfig.chains()) {
            final JSONObject l = risk.limits(DexConfig.controlNetwork(chain));
            if (l == null) continue;
            if (out.length() > 0) out.append('\n');
            out.append(DexConfig.coin(chain)).append(": ");
            if (!l.optBoolean("open")) out.append("new trades paused (").append(l.optString("why")).append(")");
            else out.append("up to ").append(shortUnits(l.optString("perTrade"), DexConfig.decimals(chain))).append(" per trade, ")
                    .append(shortUnits(l.optString("perDay"), DexConfig.decimals(chain))).append(" a day, ")
                    .append(DexFiles.units(l.optString("xmzPerTrade"), 11)).append(" XMZ per trade");
        }
        return out.toString();
    }

    /** For reading: at most six decimals (cut, never rounded up past a limit). */
    private static String shortUnits(String atoms, int places) {
        final String full = DexFiles.units(atoms, places);
        final int dot = full.indexOf('.');
        return dot < 0 || full.length() - dot - 1 <= 6 ? full : full.substring(0, dot + 7);
    }

    String marketMessage() {
        return marketMessage;
    }

    /** A trade was agreed or running when the app last closed, and the keys are still locked: it waits for the password. */
    boolean tradeWaitsForKeys() {
        if (keys.ready() || !keys.exist()) return false;
        for (DexMarket m : allMarkets()) if (!m.agreedFolders().isEmpty()) return true;
        return direct != null && direct.hasRunningTrade();
    }

    private DexMarket marketFor(String tradeDir) {
        for (DexMarket m : allMarkets()) if (tradeDir.startsWith(m.tradesDir.getPath() + "/")) return m;
        return null;
    }

    private DexMarket marketForCoin(String coin) {
        for (DexMarket m : markets.values()) if (m.coin().equals(coin)) return m;
        return null;
    }

    private DexMarket xmrMarketForCoin(String coin) {
        for (DexMarket m : xmrMarkets.values()) if (m.coin().equals(coin)) return m;
        return null;
    }

    private boolean say(String message) {
        marketMessage = message;
        changed();
        return false;
    }

    /** The same limits and kill switch as the approval; nothing is recorded. */
    private String offerRefusal(long chainId, String valueWei, String xmzAtoms) {
        final String network = DexConfig.controlNetwork(chainId);
        if (network.isEmpty()) return "This chain is not supported for trading.";
        try {
            return risk.refusal(network, OFFER_CHECK_ID, new BigInteger(valueWei), new BigInteger(xmzAtoms), 0);
        } catch (Exception e) {
            return "The amount could not be read.";
        }
    }

    boolean refreshOffers(String pair) {
        final int slash = pair.indexOf('/');
        final DexMarket m = slash < 0 ? null : pair.endsWith("/XMR") ? xmrMarketForCoin(pair.substring(0, slash)) : marketForCoin(pair.substring(0, slash));
        return m != null && m.refreshListings(pair);
    }

    /** minXmzAmount: empty for a whole offer, else the least XMZ one buyer or seller may take. */
    boolean postOffer(long chainId, String side, String coinAmount, String xmzAmount, String minXmzAmount, int hours, boolean renew) {
        final String wei = DexFiles.atomic(coinAmount, DexConfig.decimals(chainId)), atoms = DexFiles.atomic(xmzAmount, 11);
        if (wei == null || atoms == null || hours < 1 || hours > 31 * 24) return say("Check the amounts and how long the offer stays open (1 hour to 31 days).");
        String minAtoms = null;
        if (minXmzAmount != null && !minXmzAmount.trim().isEmpty()) {
            minAtoms = DexFiles.atomic(minXmzAmount.trim(), 11);
            if (minAtoms == null || new BigInteger(minAtoms).compareTo(MIN_FILL) < 0 || new BigInteger(minAtoms).compareTo(new BigInteger(atoms)) > 0)
                return say("The smallest part must be at least 1 XMZ and at most the whole offer.");
        }
        final String refusal = offerRefusal(chainId, wei, atoms);
        if (!refusal.isEmpty()) return say(refusal);
        final DexMarket m = markets.get(chainId);
        if (m == null) return false;
        final boolean started = m.post(keys.seedHex(), side, wei, atoms, minAtoms, hours * 3600L, renew);
        marketMessage = started ? "" : "The offer could not be posted now.";
        changed();
        return started;
    }

    /**
     * A Monero offer: the amount in XMR (12 places); minXmrAmount empty for a whole offer, else the
     * smallest part (at least 0.001 XMR). The coin's limits apply; XMZ's do not (none is traded).
     */
    boolean postXmrOffer(long chainId, String side, String coinAmount, String xmrAmount, String minXmrAmount, int hours, boolean renew) {
        final DexMarket m = xmrMarkets.get(chainId);
        if (m == null) return say("Monero trading is not open on this chain in this wallet.");
        final String wei = DexFiles.atomic(coinAmount, DexConfig.decimals(chainId)), atoms = DexFiles.atomic(xmrAmount, 12);
        if (wei == null || atoms == null || hours < 1 || hours > 31 * 24) return say("Check the amounts and how long the offer stays open (1 hour to 31 days).");
        String minAtoms = null;
        if (minXmrAmount != null && !minXmrAmount.trim().isEmpty()) {
            minAtoms = DexFiles.atomic(minXmrAmount.trim(), 12);
            if (minAtoms == null || new BigInteger(minAtoms).compareTo(MIN_FILL_XMR) < 0 || new BigInteger(minAtoms).compareTo(new BigInteger(atoms)) > 0)
                return say("The smallest part must be at least 0.001 XMR and at most the whole offer.");
        }
        final String refusal = offerRefusal(chainId, wei, "0");
        if (!refusal.isEmpty()) return say(refusal);
        final boolean started = m.post(keys.seedHex(), side, wei, atoms, minAtoms, hours * 3600L, renew);
        marketMessage = started ? "" : "The offer could not be posted now.";
        changed();
        return started;
    }

    /** xmzAmount: empty to take all of the offer, else how much of a partial offer (in XMZ, or XMR on a Monero market). */
    boolean takeOffer(String listingId, String xmzAmount) {
        for (DexMarket m : allMarkets()) {
            final JSONObject l = m.listing(listingId);
            if (l == null) continue;
            final String unit = m.xmr ? "XMR" : "XMZ";
            String atoms = null, wei = l.optString("valueWei");
            if (xmzAmount != null && !xmzAmount.trim().isEmpty()) {
                atoms = DexFiles.atomic(xmzAmount.trim(), m.privatePlaces());
                final String part = atoms == null ? null : partValue(l, new BigInteger(atoms));
                if (part == null) {
                    final String min = l.optString("minXmz");
                    return say(min.isEmpty() ? "This offer can only be taken whole."
                            : "Take at least " + min + " " + unit + " and at most " + l.optString("xmz") + " " + unit + ", leaving none or at least " + min + " " + unit + ".");
                }
                // All of it is a whole take: nothing to name
                if (atoms.equals(l.optString("xmzAtoms"))) atoms = null;
                else wei = part;
            }
            // A partial offer taken whole still names its amount: the maker then knows it was chosen
            if (atoms == null && l.has("minXmzAtoms")) atoms = l.optString("xmzAtoms");
            // A USDT offer is checked under the USDT market's limits
            final String pairName = l.optString("pair");
            final boolean usdt = pairName.startsWith("USDT/") || pairName.startsWith("USDT-BSC/");
            final long market = usdt ? DexConfig.marketOf(l.optLong("chainId"), "usdt") : l.optLong("chainId");
            // A Monero trade moves no XMZ: only the coin's limits apply
            final String refusal = offerRefusal(market, wei, m.xmr ? "0" : atoms == null ? l.optString("xmzAtoms") : atoms);
            if (!refusal.isEmpty()) return say(refusal);
            final boolean started = m.take(keys.seedHex(), listingId, atoms);
            marketMessage = started ? "" : "The offer could not be taken now.";
            changed();
            return started;
        }
        return false;
    }

    /** The least a partial offer's smallest part may be: 1 XMZ (as the engine checks). */
    private static final BigInteger MIN_FILL = BigInteger.valueOf(100_000_000_000L);
    /** The same on a Monero market: 0.001 XMR (1e9 of XMR's 1e12 atoms), as the engine checks */
    private static final BigInteger MIN_FILL_XMR = BigInteger.valueOf(1_000_000_000L);

    /**
     * The coin (smallest unit) for `atoms` XMZ of a listing the relays showed, at its price, rounded
     * in the maker's favour as the engine does; null when the offer does not allow that amount.
     */
    static String partValue(JSONObject l, BigInteger atoms) {
        try {
            final BigInteger total = new BigInteger(l.optString("xmzAtoms")), value = new BigInteger(l.optString("valueWei"));
            if (atoms.equals(total)) return value.toString();
            if (!l.has("minXmzAtoms")) return null;
            final BigInteger min = new BigInteger(l.optString("minXmzAtoms"));
            if (atoms.compareTo(min) < 0 || atoms.compareTo(total) >= 0 || total.subtract(atoms).compareTo(min) < 0) return null;
            final BigInteger[] qr = value.multiply(atoms).divideAndRemainder(total);
            final boolean up = l.optString("side").equals("sell-xmz") && qr[1].signum() != 0;
            return (up ? qr[0].add(BigInteger.ONE) : qr[0]).toString();
        } catch (Exception e) {
            return null;
        }
    }

    boolean setOfferRenew(String folder, boolean on) {
        final DexMarket m = folder == null ? null : marketFor(folder);
        return m != null && m.setRenew(folder, on);
    }

    boolean cancelOffer(String folder) {
        final DexMarket m = folder == null ? null : marketFor(folder);
        return m != null && m.cancel(keys.seedHex(), folder);
    }

    /** Unused coin in the trade account back to the user's own wallet (the one accepted for the page). */
    boolean returnFunds(long chainId, String address) {
        final DexMarket m = markets.get(chainId);
        if (m == null || !keys.ready()) return false;
        if (trade.configured() && !trade.done()) return say("A trade is running: its account needs its funds until it finishes.");
        final boolean started = m.withdraw(keys.seedHex(), address.trim());
        marketMessage = started ? "" : "The funds could not be sent back now.";
        changed();
        return started;
    }

    boolean hasOpenOffer() {
        for (DexMarket m : allMarkets()) if (m.hasOpenOffer()) return true;
        return false;
    }

    private void stepMarkets() {
        if (!keys.ready()) return;
        if (evmAccount.isEmpty()) askAccounts();
        for (DexMarket m : allMarkets()) if (!m.busy()) m.step(keys.seedHex());
        if (direct != null) direct.step(keys.seedHex());
    }

    // ---- the trade -----------------------------------------------------------------------------

    @Override
    public void tradeChanged() {
        if (resuming && trade.configured() && !trade.busy() && trade.snapshot().has("stage")) {
            // The newest unfinished trade is the one to continue; finished ones are passed over
            if (trade.done()) handler.post(this::nextResume);
            else {
                resuming = false;
                resume.clear();
            }
        }
        changed();
    }

    JSONObject tradeStatus() {
        return trade.status(startRefusal);
    }

    boolean awaitingApproval() {
        return trade.configured() && trade.awaitingApproval();
    }

    JSONObject tradeSnapshot() {
        return trade.snapshot();
    }

    String tradeCommitment() {
        return trade.commitment();
    }

    String startRefusal() {
        return startRefusal;
    }

    private static String ledgerId(String tradeId) {
        if (tradeId == null || !TRADE_ID.matcher(tradeId).matches()) return null;
        final String h = tradeId.substring(2, 34);
        return h.substring(0, 8) + "-" + h.substring(8, 12) + "-" + h.substring(12, 16) + "-" + h.substring(16, 20) + "-" + h.substring(20);
    }

    private boolean refuse(String why) {
        startRefusal = why;
        changed();
        return false;
    }

    /**
     * The user's approval. The kill switch and limits gate the START of a new trade; a trade already
     * counted is never blocked, so nothing in flight is stranded.
     */
    boolean approveTrade(String commitment) {
        final JSONObject offer = trade.offer();
        if (offer == null || trade.tradeId().isEmpty()) return false;
        // The trade's market: its chain, or for a token (USDT on Solana) the token's own market
        final String network = DexConfig.controlNetwork(DexConfig.marketOf(offer.optLong("chainId"), offer.optString("asset")));
        if (network.isEmpty()) return refuse("This chain is not supported for trading.");
        final BigInteger value, xmz;
        try {
            final String v = offer.optString("value");
            value = v.startsWith("0x") ? new BigInteger(v.substring(2), 16) : new BigInteger(v);
            // A Monero trade moves no XMZ: the XMZ cap does not apply (as the desktop wallet)
            xmz = "XMR".equals(offer.optString("privateCoin")) ? BigInteger.ZERO : new BigInteger(DexMarket.amountText(offer.opt("xmzAmount")));
        } catch (Exception e) {
            return refuse("The trade amount could not be read.");
        }
        final String id = ledgerId(trade.tradeId());
        if (id == null) return refuse("The trade identifier could not be read.");
        final String refusal = risk.refusal(network, id, value, xmz, 0);
        if (!refusal.isEmpty()) return refuse(refusal);
        if (!risk.recordStart(network, id, value, 0)) return refuse("The trade could not be recorded; not started.");
        startRefusal = "";
        final boolean sent = trade.approve(commitment);
        if (sent) handler.postDelayed(this::stepTrade, 1500);   // the next step locks; no need to wait for the ticker
        return sent;
    }

    boolean payoutTrade(String address) {
        return keys.ready() && !trade.busy() && trade.payout(keys.seedHex(), address);
    }

    boolean abandonTrade() {
        final String dir = trade.tradeDir();
        if (!trade.abandon()) return false;
        if (!dir.isEmpty()) releaseService(dir);
        startRefusal = "";
        for (DexMarket m : allMarkets()) m.scanFolders();
        changed();
        return true;
    }

    private void stepTrade() {
        if (!trade.configured()) return;
        if (trade.done()) {
            // A swap we claimed: tell the relays where the claim is, for the public price chart
            final JSONObject s = trade.snapshot();
            final DexMarket m = marketFor(trade.tradeDir());
            if (s.optString("role").equals("claimer") && s.optString("stage").equals("Done(Swapped)") && !reportedTrade.equals(trade.tradeId())
                    && m != null && m.report(trade.tradeDir())) reportedTrade = trade.tradeId();
            // Over and paid out: nothing is left to do for it after a restart
            final File mark = new File(trade.tradeDir(), "finished");
            if (trade.hasPayout() && !trade.busy() && !mark.exists()) DexFiles.write(mark, "finished\n");
            return;
        }
        if (trade.busy() || !keys.ready()) return;
        trade.tick(keys.seedHex());
    }

    /**
     * Once per session, when the keys are unlocked and the markets know their folders: a trade that
     * was running when the app closed is reopened, or its refund would never be claimed.
     */
    private void resumeTrades() {
        if (resumed || markets.isEmpty() || !keys.ready()) return;
        resumed = true;
        if (trade.configured()) return;
        final List<Object[]> found = new ArrayList<>();
        for (DexMarket m : allMarkets())
            for (String dir : m.agreedFolders()) {
                final File journal = new File(dir, "journal.json");
                found.add(new Object[]{(journal.exists() ? journal : new File(dir)).lastModified(), dir});
            }
        Collections.sort(found, (a, b) -> Long.compare((Long) b[0], (Long) a[0]));
        resume.clear();
        for (Object[] f : found) resume.add((String) f[1]);
        nextResume();
    }

    /** A trade's swap-wallet service runs in its coin's wallets: stop it in both */
    private void releaseService(String dir) {
        wallets.releaseTradeService(dir);
        if (xmrWallets != null) xmrWallets.releaseTradeService(dir);
    }

    private boolean releaseTrade() {
        // A finished trade leaves: mark its folder (so a restart does not reopen it) and stop its swap wallet
        final String dir = trade.tradeDir();
        if (!trade.release()) return false;
        if (!dir.isEmpty()) {
            final File mark = new File(dir, "finished");
            if (!mark.exists()) DexFiles.write(mark, "finished\n");
            releaseService(dir);
        }
        return true;
    }

    private void nextResume() {
        if (trade.configured() && !releaseTrade()) {
            resuming = false;
            resume.clear();
            return;
        }
        while (!resume.isEmpty()) {
            final String next = resume.remove(0);
            final DexMarket m = marketFor(next);
            if (m == null) continue;
            resuming = true;
            agreed(m, next);
            return;
        }
        resuming = false;
    }

    /** A taken offer is a saved trade now: give it its own swap-wallet service, then run it (one trade at a time). */
    @Override
    public void agreed(final DexMarket market, final String tradeDir) {
        if (stopped) return;
        String id = "";
        for (String name : new String[]{"listing.json", "take.json"}) {
            final JSONObject o = DexFiles.readJson(new File(tradeDir, name), 65536);
            final JSONObject signed = o == null ? null : o.optJSONObject("listing");
            final JSONObject l = signed == null ? null : signed.optJSONObject("listing");
            if (l != null) {
                id = l.optString("id");
                break;
            }
        }
        // A Monero trade's swap wallet is a Monero wallet, in the Monero wallets' service
        final DexWallets coinWallets = market.xmr ? xmrWallets : wallets;
        if (coinWallets == null) {
            say("This wallet has no Monero program for this phone: the Monero trade cannot run here.");
            return;
        }
        coinWallets.tradeService(tradeDir, id, endpoint -> {
            if (stopped) return;
            if (endpoint == null) {
                say("The trade's swap wallet could not start: " + tradeDir);
                if (resuming) handler.post(this::nextResume);
                return;
            }
            // A finished trade makes room for the next one; a running one keeps the job
            if (trade.configured() && !releaseTrade()) {
                if (trade.done()) {
                    // Finished, but its last step (the payout) is still in flight: open the new trade right after
                    handler.postDelayed(() -> agreed(market, tradeDir), 3000);
                    return;
                }
                say("Keys agreed for a new trade. Finish the current trade first; the new one opens when the app next starts.");
                return;
            }
            startRefusal = "";
            if (!trade.configure(tradeDir, market.rpc, coinWallets.mainEndpoint(), endpoint, market.witnesses)) {
                say("The agreed trade could not be opened: " + tradeDir);
                if (resuming) handler.post(this::nextResume);
                return;
            }
            trade.refresh();
            handler.postDelayed(this::stepTrade, 2000);
        });
    }
}
