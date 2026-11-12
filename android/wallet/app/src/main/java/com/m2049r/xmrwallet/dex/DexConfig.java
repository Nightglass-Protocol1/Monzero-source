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
    static final long SOL_MAINNET = 501000101L, SOL_DEVNET = 501000102L, SOL_LOCAL = 501000103L;
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

    static boolean solana(long chainId) {
        return chainId == SOL_MAINNET || chainId == SOL_DEVNET || chainId == SOL_LOCAL;
    }

    static String coin(long chainId) {
        return solana(chainId) ? "SOL" : (chainId == 56 || chainId == 97) ? "BNB" : "ETH";
    }

    static int decimals(long chainId) {
        return solana(chainId) ? 9 : 18;
    }

    static String networkName(long chainId) {
        if (chainId == 1) return "Ethereum Mainnet";
        if (chainId == 56) return "BNB Smart Chain";
        if (chainId == 11155111) return "Sepolia (test network)";
        if (chainId == 97) return "BNB Smart Chain Testnet";
        if (chainId == SOL_MAINNET) return "Solana";
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
        return mainnet() ? new long[]{1, 56, SOL_MAINNET} : new long[]{11155111, 97, SOL_DEVNET};
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
        return "evm-regtest";   // test networks: no document, nothing of value
    }

    static String defaultRpc(long chainId) {
        if (chainId == 1) return "https://ethereum-rpc.publicnode.com";
        if (chainId == 56) return "https://bsc-rpc.publicnode.com";
        if (chainId == SOL_MAINNET) return "https://solana-rpc.publicnode.com";
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
            String node = "", contract = "";
            if (o != null) {
                final JSONArray list = o.optJSONArray("relays");
                for (int i = 0; list != null && i < list.length() && relays.size() < 8; i++) {
                    final String r = list.optString(i);
                    if (RELAY.matcher(r).matches()) relays.add(r);
                }
                if (o.optJSONObject("rpc") != null) rpcs = o.optJSONObject("rpc");
                if (DAEMON.matcher(o.optString("daemon")).matches()) node = o.optString("daemon");
                if (evmAddress(o.optString("localContract"))) contract = o.optString("localContract");
            }
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
