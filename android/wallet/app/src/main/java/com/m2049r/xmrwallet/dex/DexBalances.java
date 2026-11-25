package com.m2049r.xmrwallet.dex;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.math.BigInteger;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * What the trade account holds of a coin, read from the chain's public node (read-only JSON-RPC,
 * as the desktop wallet's exchange feed): ETH/BNB eth_getBalance, BNB Smart Chain USDT balanceOf,
 * SOL getBalance, Solana USDT from its token account (getAccountInfo, amount at byte 64; the
 * indexed token calls are refused by public nodes). Blocking: call off the main thread.
 * Returns the smallest unit, or null when it could not be read.
 */
final class DexBalances {
    static final String BSC_USDT = "0x55d398326f99059fF775485246999027B3197955";

    private DexBalances() {
    }

    static BigInteger read(String coin, String rpc, String evm, String sol, String solUsdt) {
        try {
            switch (coin) {
                case "ETH":
                case "BNB": {
                    if (!DexConfig.evmAddress(evm)) return null;
                    return hex(call(rpc, "eth_getBalance", new JSONArray().put(evm).put("latest")).optString("result"));
                }
                case "USDT-BSC": {
                    if (!DexConfig.evmAddress(evm)) return null;
                    final String data = "0x70a08231" + "000000000000000000000000" + evm.substring(2).toLowerCase();
                    return hex(call(rpc, "eth_call", new JSONArray().put(new JSONObject().put("to", BSC_USDT).put("data", data)).put("latest")).optString("result"));
                }
                case "SOL": {
                    if (!DexConfig.solanaAddress(sol)) return null;
                    final JSONObject r = call(rpc, "getBalance", new JSONArray().put(sol).put(new JSONObject().put("commitment", "confirmed")));
                    final JSONObject res = r.optJSONObject("result");
                    return res == null ? null : BigInteger.valueOf(res.optLong("value"));
                }
                case "USDT": {
                    if (!DexConfig.solanaAddress(solUsdt)) return null;
                    final JSONObject r = call(rpc, "getAccountInfo", new JSONArray().put(solUsdt).put(new JSONObject().put("encoding", "base64").put("commitment", "confirmed")));
                    final JSONObject res = r.optJSONObject("result");
                    if (res == null) return null;
                    // No token account yet: nothing has been sent to it
                    if (res.isNull("value")) return BigInteger.ZERO;
                    final JSONArray data = res.optJSONObject("value").optJSONArray("data");
                    final byte[] raw = android.util.Base64.decode(data.optString(0), android.util.Base64.DEFAULT);
                    if (raw.length < 72) return null;
                    long amount = 0;
                    for (int i = 7; i >= 0; i--) amount = (amount << 8) | (raw[64 + i] & 0xff);
                    return BigInteger.valueOf(amount).max(BigInteger.ZERO);
                }
                default:
                    return null;
            }
        } catch (Exception e) {
            return null;
        }
    }

    private static BigInteger hex(String text) {
        if (text == null || !text.startsWith("0x")) return null;
        final String digits = text.substring(2);
        return digits.isEmpty() ? BigInteger.ZERO : new BigInteger(digits, 16);
    }

    private static JSONObject call(String rpc, String method, JSONArray params) throws Exception {
        final HttpURLConnection c = (HttpURLConnection) new URL(rpc).openConnection();
        try {
            c.setConnectTimeout(10000);
            c.setReadTimeout(15000);
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setRequestProperty("Content-Type", "application/json");
            final byte[] body = new JSONObject().put("jsonrpc", "2.0").put("id", 1).put("method", method).put("params", params).toString().getBytes(StandardCharsets.UTF_8);
            try (OutputStream out = c.getOutputStream()) {
                out.write(body);
            }
            if (c.getResponseCode() != 200) throw new IllegalStateException("http " + c.getResponseCode());
            final ByteArrayOutputStream buf = new ByteArrayOutputStream();
            try (InputStream in = c.getInputStream()) {
                final byte[] chunk = new byte[8192];
                int n;
                while ((n = in.read(chunk)) > 0) {
                    buf.write(chunk, 0, n);
                    if (buf.size() > 262144) throw new IllegalStateException("answer too large");
                }
            }
            return new JSONObject(buf.toString("UTF-8"));
        } finally {
            c.disconnect();
        }
    }
}
