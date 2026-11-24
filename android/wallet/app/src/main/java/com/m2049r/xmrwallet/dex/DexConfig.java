package com.m2049r.xmrwallet.dex;

import com.m2049r.xmrwallet.BuildConfig;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

/**
 * What the DEX link is allowed to do in this build. The mainnet build is the public edition:
 * main networks only, every new trade checked against the owner-signed control document, and only
 * the DEX page on monzero.org may connect. Test builds (devnet flavor) trade on test networks.
 */
final class DexConfig {
    private DexConfig() {
    }

    /** The owner's control key (the same one the desktop public edition pins). */
    static final String CONTROL_KEY = "9eba2b1784ddfc23f028c03c78f862fecc81af81616722b56e671e6662e0a6fb";
    static final String CONTROL_BASE = "https://monzero.org/dex/control/";
    /**
     * Where else the signed control documents are: the relays mirror them, so new trades go on
     * while the website is down (every copy is checked against the pinned key; the newest wins)
     */
    static final String[] CONTROL_MIRRORS = {"https://relay1.monzero.org/v1/control/", "https://relay2.monzero.org/v1/control/"};
    static final long SOL_MAINNET = 501000101L, SOL_DEVNET = 501000102L, SOL_LOCAL = 501000103L;
    /** USDT on Solana: the app's own market ids (the chain's id + 100); the helper trades them on
     *  the Solana chain with the pair "USDT/XMZ". Same ids as the desktop wallet and the page. */
    static final long SOL_USDT_MAINNET = 501000201L, SOL_USDT_DEVNET = 501000202L, SOL_USDT_LOCAL = 501000203L;
    /** USDT (BEP-20) on BNB Smart Chain: market ids = the chain's id + 100,000,000. */
    static final long BSC_USDT_MAINNET = 100000056L, BSC_USDT_TESTNET = 100000097L;
    /** The page looks for the wallet on these loopback ports (the same five as the desktop wallet). */
    static final int FIRST_PORT = 38471, PORTS = 5;

    private static final Pattern EVM_ADDRESS = Pattern.compile("\\A0x[0-9a-fA-F]{40}\\z");
    private static final Pattern SOL_ADDRESS = Pattern.compile("\\A[1-9A-HJ-NP-Za-km-z]{32,44}\\z");
    private static final Pattern EVM_TX = Pattern.compile("\\A0x[0-9a-f]{64}\\z");
    private static final Pattern SOL_TX = Pattern.compile("\\A[1-9A-HJ-NP-Za-km-z]{64,88}\\z");
    private static final Pattern RPC = Pattern.compile("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://127\\.0\\.0\\.1:[0-9]{1,5})\\z");
    private static final Pattern RELAY = Pattern.compile("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5})\\z");
    private static final Pattern DAEMON = Pattern.compile("\\A[A-Za-z0-9.-]{1,253}:[0-9]{1,5}\\z");

    static boolean mainnet() {
        return "mainnet".equals(BuildConfig.FLAVOR_net);
    }

    /** Pages served from this device (tests) may connect only to test builds and debug builds. */
    static boolean localPages() {
        return !mainnet() || BuildConfig.DEBUG;
    }

    static String networkFlag() {
        switch (BuildConfig.FLAVOR_net) {
            case "mainnet":
                return "";
            case "stagenet":
                return "--stagenet";
            default:
                return "--testnet";
        }
    }

    static String networkFolder() {
        return mainnet() ? "mainnet" : "testnet";
    }

    static String[] origins() {
        return new String[]{"https://monzero.org", "https://www.monzero.org"};
    }

    static boolean usdt(long id) {
        return id == SOL_USDT_MAINNET || id == SOL_USDT_DEVNET || id == SOL_USDT_LOCAL;
    }

    static boolean usdtBsc(long id) {
        return id == BSC_USDT_MAINNET || id == BSC_USDT_TESTNET;
    }

    /** A token market (USDT on either chain): the helper is told its pair. */
    static boolean token(long id) {
        return usdt(id) || usdtBsc(id);
    }

    /** The chain the helper trades a market on. */
    static long helperChain(long id) {
        return usdt(id) ? id - 100 : usdtBsc(id) ? id - 100000000L : id;
    }

    /** The market of a trade the helper describes: its chain, and a token asset (USDT on Solana). */
    static long marketOf(long chainId, String asset) {
        if (asset == null || asset.matches("\\A(0x)?0*\\z")) return chainId;
        if (chainId == SOL_MAINNET || chainId == SOL_DEVNET || chainId == SOL_LOCAL) return chainId + 100;
        if (chainId == 56 || chainId == 97) return chainId + 100000000L;
        return chainId;
    }

    /** Solana addresses and transactions: the chain's coin and its USDT alike. */
    static boolean solana(long chainId) {
        return chainId == SOL_MAINNET || chainId == SOL_DEVNET || chainId == SOL_LOCAL || usdt(chainId);
    }

    static String coin(long chainId) {
        return usdt(chainId) ? "USDT" : usdtBsc(chainId) ? "USDT-BSC" : solana(chainId) ? "SOL" : (chainId == 56 || chainId == 97) ? "BNB" : "ETH";
    }

    static String pair(long chainId) {
        return coin(chainId) + "/XMZ";
    }

    /** The Monero pair of a chain ("BNB/XMR", "USDT/XMR", ...) */
    static String xmrPair(long chainId) {
        return coin(chainId) + "/XMR";
    }

    /**
     * The chains with a Monero market here. Main network: the pairs proven with real coins, as in
     * the desktop wallet (BNB/XMR, USDT-BSC/XMR, SOL/XMR, USDT (Solana)/XMR); ETH/XMR opens after
     * its own real-funds swap. Test build: Monero's stagenet against the test chains.
     */
    static long[] xmrChains() {
        return mainnet() ? new long[]{56, BSC_USDT_MAINNET, SOL_MAINNET, SOL_USDT_MAINNET} : new long[]{11155111, 97, SOL_DEVNET};
    }

    /** Monero's node for the app's Monero wallets: the project's own (main network, or stagenet for tests) */
    static String defaultXmrDaemon() {
        return mainnet() ? "31.70.193.31:18089" : "31.70.193.31:38089";
    }

    /** Monero's network for its wallet RPC: the main network, or stagenet in the test build */
    static String xmrNetworkFlag() {
        return mainnet() ? "" : "--stagenet";
    }

    /**
     * Independent public Monero nodes that must also see the other trader's XMR lock before it
     * counts (a main-network trade needs two; the engine refuses it otherwise). The same list as
     * the desktop wallet.
     */
    static List<String> xmrWitnesses() {
        final List<String> w = new ArrayList<>();
        if (!mainnet()) return w;
        w.add("https://node.monerodevs.org:18089");
        w.add("https://monero.stackwallet.com:18081");
        w.add("https://xmr.cryptostorm.is:18081");
        w.add("https://xmr-node.cakewallet.com:18081");
        w.add("https://xmr.unshakled.net:443");
        return w;
    }

    static int decimals(long chainId) {
        return usdt(chainId) ? 6 : solana(chainId) ? 9 : 18;
    }

    static String networkName(long chainId) {
        if (chainId == 1) return "Ethereum Mainnet";
        if (chainId == 56) return "BNB Smart Chain";
        if (chainId == 11155111) return "Sepolia (test network)";
        if (chainId == 97) return "BNB Smart Chain Testnet";
        if (chainId == SOL_MAINNET) return "Solana";
        if (chainId == SOL_USDT_MAINNET) return "Solana (USDT)";
        if (chainId == BSC_USDT_MAINNET) return "BNB Smart Chain (USDT)";
        if (chainId == SOL_DEVNET) return "Solana Devnet (test network)";
        return "chain " + chainId;
    }

    static boolean evmAddress(String text) {
        return text != null && EVM_ADDRESS.matcher(text).matches();
    }

    static boolean solanaAddress(String text) {
        return text != null && SOL_ADDRESS.matcher(text).matches();
    }

    static boolean address(long chainId, String text) {
        return solana(chainId) ? solanaAddress(text) : evmAddress(text);
    }

    static boolean transactionId(String text) {
        return text != null && (EVM_TX.matcher(text).matches() || SOL_TX.matcher(text).matches());
    }

    /** The chains this build trades on. */
    static long[] chains() {
        return mainnet() ? new long[]{1, 56, SOL_MAINNET, SOL_USDT_MAINNET, BSC_USDT_MAINNET} : new long[]{11155111, 97, SOL_DEVNET};
    }

    static boolean tradedHere(long chainId) {
        for (long c : chains()) if (c == chainId) return true;
        return false;
    }

    /** The control document's network name for a chain; empty when this build does not trade there. */
    static String controlNetwork(long chainId) {
        if (!tradedHere(chainId)) return "";
        if (chainId == 56) return "bnb-mainnet";
        if (chainId == 1) return "eth-mainnet";
        if (chainId == SOL_MAINNET) return "sol-mainnet";
        if (chainId == SOL_USDT_MAINNET) return "sol-usdt-v2-mainnet";
        if (chainId == BSC_USDT_MAINNET) return "bnb-usdt-v2-mainnet";
        return "evm-regtest";   // test networks: no document, nothing of value
    }

    static String defaultRpc(long chainId) {
        if (chainId == 1) return "https://ethereum-rpc.publicnode.com";
        if (chainId == 56 || chainId == BSC_USDT_MAINNET) return "https://bsc-rpc.publicnode.com";
        if (chainId == SOL_MAINNET || chainId == SOL_USDT_MAINNET) return "https://solana-rpc.publicnode.com";
        if (chainId == 11155111) return "https://ethereum-sepolia-rpc.publicnode.com";
        if (chainId == 97) return "https://bsc-testnet-rpc.publicnode.com";
        return "https://api.devnet.solana.com";
    }

    /** Settings a test build reads from dex/&lt;network&gt;/overrides.json (never the public build). */
    static final class Overrides {
        final List<String> relays = new ArrayList<>();
        final JSONObject rpc;
        final String daemon;
        final String localContract;
        /** Test builds: the Monero node ("host:port") and witnesses (https:// or loopback URLs) */
        final String xmrDaemon;
        final List<String> xmrWitnesses = new ArrayList<>();

        Overrides(File root) {
            JSONObject o = null;
            if (localPages()) {
                final byte[] bytes = DexFiles.read(new File(root, "overrides.json"), 16384);
                try {
                    if (bytes != null) o = new JSONObject(new String(bytes, "UTF-8"));
                } catch (Exception ignored) {
                    o = null;
                }
            }
            JSONObject rpcs = new JSONObject();
            String node = "", contract = "", xmrNode = "";
            if (o != null) {
                final JSONArray list = o.optJSONArray("relays");
                for (int i = 0; list != null && i < list.length() && relays.size() < 8; i++) {
                    final String r = list.optString(i);
                    if (RELAY.matcher(r).matches()) relays.add(r);
                }
                if (o.optJSONObject("rpc") != null) rpcs = o.optJSONObject("rpc");
                if (DAEMON.matcher(o.optString("daemon")).matches()) node = o.optString("daemon");
                if (evmAddress(o.optString("localContract"))) contract = o.optString("localContract");
                if (DAEMON.matcher(o.optString("xmrDaemon")).matches()) xmrNode = o.optString("xmrDaemon");
                final JSONArray w = o.optJSONArray("xmrWitnesses");
                for (int i = 0; w != null && i < w.length() && xmrWitnesses.size() < 8; i++) {
                    final String u = w.optString(i);
                    if (RELAY.matcher(u).matches()) xmrWitnesses.add(u);
                }
            }
            xmrDaemon = xmrNode;
            rpc = rpcs;
            daemon = node;
            localContract = contract;
        }

        List<String> relaysOrDefault() {
            if (!relays.isEmpty()) return relays;
            final List<String> r = new ArrayList<>();
            r.add("https://relay1.monzero.org");
            r.add("https://relay2.monzero.org");
            return r;
        }

        String rpcFor(long chainId) {
            final String o = rpc.optString(String.valueOf(chainId));
            return RPC.matcher(o).matches() ? o : defaultRpc(chainId);
        }
    }

    static boolean daemonAddress(String text) {
        return text != null && DAEMON.matcher(text).matches();
    }

    static String defaultDaemon() {
        return mainnet() ? "node.monzero.org:6175" : "10.0.2.2:16175";
    }
}
