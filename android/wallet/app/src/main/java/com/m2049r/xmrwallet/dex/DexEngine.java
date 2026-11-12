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
final class DexEngine implements DexWallets.Listener, DexMarket.Listener, DexTrade.Listener {
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
    private final DexRisk risk;
    private final DexTrade trade;
    private final Map<Long, DexMarket> markets = new LinkedHashMap<>();
    private final List<Listener> listeners = new ArrayList<>();
    private String marketMessage = "", startRefusal = "", keysMessage = "", evmAccount = "", solAccount = "", reportedTrade = "";
    private boolean keysBusy, resumed, resuming, stopped, marketsUp, accountsAsked;
    private final List<String[]> resume = new ArrayList<>();   // {chainId, tradeDir}
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
        risk = new DexRisk(DexConfig.CONTROL_KEY, new File(root, "started-trades.json"), handler, this::changed);
        trade = new DexTrade(helper, this);
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
            handler.postDelayed(ticker, 5000);
        });
    }

    /** True while a trade is running: the app must stay up until it finishes or refunds. */
    boolean tradeRunning() {
        return trade.configured() && !trade.done() && trade.snapshot().optBoolean("approved");
    }

    void stop() {
        handler.post(() -> {
            stopped = true;
            handler.removeCallbacks(ticker);
            wallets.stop();
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
                        for (DexMarket m : markets.values()) m.setSolAccount(solAccount);
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

    // ---- markets -------------------------------------------------------------------------------

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
        resumeTrades();
        changed();
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
        for (DexMarket m : markets.values()) if (!m.agreedFolders().isEmpty()) return true;
        return false;
    }

    private DexMarket marketFor(String tradeDir) {
        for (DexMarket m : markets.values()) if (tradeDir.startsWith(m.tradesDir.getPath() + "/")) return m;
        return null;
    }

    private DexMarket marketForCoin(String coin) {
        for (DexMarket m : markets.values()) if (m.coin().equals(coin)) return m;
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
        final DexMarket m = slash < 0 ? null : marketForCoin(pair.substring(0, slash));
        return m != null && m.refreshListings(pair);
    }

    boolean postOffer(long chainId, String side, String coinAmount, String xmzAmount, int hours) {
        final String wei = DexFiles.atomic(coinAmount, DexConfig.decimals(chainId)), atoms = DexFiles.atomic(xmzAmount, 11);
        if (wei == null || atoms == null || hours < 1 || hours > 24) return say("Check the amounts and how long the offer stays open (1-24 hours).");
        final String refusal = offerRefusal(chainId, wei, atoms);
        if (!refusal.isEmpty()) return say(refusal);
        final DexMarket m = markets.get(chainId);
        if (m == null) return false;
        final boolean started = m.post(keys.seedHex(), side, wei, atoms, hours * 3600L);
        marketMessage = started ? "" : "The offer could not be posted now.";
        changed();
        return started;
    }

    boolean takeOffer(String listingId) {
        for (DexMarket m : markets.values()) {
            final JSONObject l = m.listing(listingId);
            if (l == null) continue;
            final String refusal = offerRefusal(l.optLong("chainId"), l.optString("valueWei"), l.optString("xmzAtoms"));
            if (!refusal.isEmpty()) return say(refusal);
            final boolean started = m.take(keys.seedHex(), listingId);
            marketMessage = started ? "" : "The offer could not be taken now.";
            changed();
            return started;
        }
        return false;
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
        for (DexMarket m : markets.values()) if (m.hasOpenOffer()) return true;
        return false;
    }

    private void stepMarkets() {
        if (!keys.ready()) return;
        if (evmAccount.isEmpty()) askAccounts();
        for (DexMarket m : markets.values()) if (!m.busy()) m.step(keys.seedHex());
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
        final String network = DexConfig.controlNetwork(offer.optLong("chainId"));
        if (network.isEmpty()) return refuse("This chain is not supported for trading.");
        final BigInteger value, xmz;
        try {
            final String v = offer.optString("value");
            value = v.startsWith("0x") ? new BigInteger(v.substring(2), 16) : new BigInteger(v);
            xmz = new BigInteger(DexMarket.amountText(offer.opt("xmzAmount")));
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
        if (!dir.isEmpty()) wallets.releaseTradeService(dir);
        startRefusal = "";
        for (DexMarket m : markets.values()) m.scanFolders();
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
        for (DexMarket m : markets.values())
            for (String dir : m.agreedFolders()) {
                final File journal = new File(dir, "journal.json");
                found.add(new Object[]{(journal.exists() ? journal : new File(dir)).lastModified(), String.valueOf(m.chainId), dir});
            }
        Collections.sort(found, (a, b) -> Long.compare((Long) b[0], (Long) a[0]));
        resume.clear();
        for (Object[] f : found) resume.add(new String[]{(String) f[1], (String) f[2]});
        nextResume();
    }

    private boolean releaseTrade() {
        // A finished trade leaves: mark its folder (so a restart does not reopen it) and stop its swap wallet
        final String dir = trade.tradeDir();
        if (!trade.release()) return false;
        if (!dir.isEmpty()) {
            final File mark = new File(dir, "finished");
            if (!mark.exists()) DexFiles.write(mark, "finished\n");
            wallets.releaseTradeService(dir);
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
            final String[] next = resume.remove(0);
            final DexMarket m = markets.get(Long.parseLong(next[0]));
            if (m == null) continue;
            resuming = true;
            agreed(m, next[1]);
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
        wallets.tradeService(tradeDir, id, endpoint -> {
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
            if (!trade.configure(tradeDir, market.rpc, wallets.mainEndpoint(), endpoint)) {
                say("The agreed trade could not be opened: " + tradeDir);
                if (resuming) handler.post(this::nextResume);
                return;
            }
            trade.refresh();
            handler.postDelayed(this::stepTrade, 2000);
        });
    }
}
