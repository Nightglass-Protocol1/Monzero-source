package com.m2049r.xmrwallet.dex;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.File;
import java.math.BigInteger;

/**
 * Checks of the parts that only run on a device (libsodium through JNI, the key file), for test
 * builds: `am start -n <package>/com.m2049r.xmrwallet.dex.DexActivity --ez selfcheck true` on a
 * debug build writes each result to the log under "DexSelfCheck". Never run by release builds.
 */
final class DexSelfCheck {
    private DexSelfCheck() {
    }

    // A real control document of the owner (eth-mainnet, sequence 5), as published on monzero.org
    private static final String PAYLOAD = "{\\\"expiresAt\\\":1795037243,\\\"issuedAt\\\":1794432443,\\\"maxLtcAtomsPerDay\\\":\\\"120246649928332996\\\",\\\"maxLtcAtomsPerTrade\\\":\\\"12024664992833299\\\",\\\"maxXmzAtomsPerTrade\\\":\\\"2500000000000000\\\",\\\"network\\\":\\\"eth-mainnet\\\",\\\"newTradesEnabled\\\":true,\\\"schema\\\":\\\"monzero-dex-control-v1\\\",\\\"sequence\\\":5}";
    private static final String SIGNATURE = "09d0a6ecd8daee177af7ebf21b84022b7ddbdcb1ceb63c96dc1c93fd4640a09fb1cf196e48e2a6a7943719c424a096152cb244332fd605a651fabf1997460205";
    private static final long WHEN = 1794500000L;   // inside the document's validity

    private static String document(String payload, String signature) {
        return "{\"payload\":\"" + payload + "\",\"signature\":\"" + signature + "\"}";
    }

    private static int failures;

    private static void check(String what, boolean ok) {
        if (!ok) failures++;
        Log.i("DexSelfCheck", (ok ? "PASS " : "FAIL ") + what);
    }

    static void run(Context context) {
        failures = 0;
        try {
            final Handler handler = new Handler(Looper.getMainLooper());
            final File dir = new File(context.getCacheDir(), "dex-selfcheck");
            for (File f : dir.listFiles() == null ? new File[0] : dir.listFiles()) f.delete();
            DexFiles.privateDir(dir);
            final Runnable nothing = () -> {
            };
            // The owner's signature on a control document
            DexRisk risk = new DexRisk(DexConfig.CONTROL_KEY, new File(dir, "ledger.json"), handler, nothing);
            check("the owner's document is accepted", risk.accept(document(PAYLOAD, SIGNATURE).getBytes("UTF-8"), "eth-mainnet", WHEN));
            check("a trade within the limits may start", risk.refusal("eth-mainnet", "6f666665-722d-4368-9563-6b0000000000", new BigInteger("1000000000000000"), new BigInteger("10000000000000"), WHEN).isEmpty());
            check("a trade over the per-trade limit is refused", !risk.refusal("eth-mainnet", "6f666665-722d-4368-9563-6b0000000000", new BigInteger("12024664992833300"), new BigInteger("10000000000000"), WHEN).isEmpty());
            check("XMZ over the per-trade limit is refused", !risk.refusal("eth-mainnet", "6f666665-722d-4368-9563-6b0000000000", new BigInteger("1000000000000000"), new BigInteger("2500000000000001"), WHEN).isEmpty());
            check("after it expires, new trades are refused", !risk.refusal("eth-mainnet", "6f666665-722d-4368-9563-6b0000000000", new BigInteger("1000000000000000"), new BigInteger("10000000000000"), 1795037244L).isEmpty());
            check("a network without a document is refused", !risk.refusal("bnb-mainnet", "6f666665-722d-4368-9563-6b0000000000", BigInteger.ONE, BigInteger.ONE, WHEN).isEmpty());
            // The 24-hour limit counts started trades
            final BigInteger most = new BigInteger("12024664992833299");
            boolean counted = true;
            for (int i = 0; i < 10; i++) counted = counted && risk.recordStart("eth-mainnet", String.format("00000000-0000-4000-8000-%012d", i), most, WHEN);
            check("ten trades at the limit are recorded", counted);
            check("the eleventh in a day is refused", !risk.refusal("eth-mainnet", "00000000-0000-4000-8000-000000000099", most, BigInteger.ONE, WHEN + 60).isEmpty());
            check("a trade already started is never blocked", risk.refusal("eth-mainnet", "00000000-0000-4000-8000-000000000003", most, BigInteger.ONE, 1795037244L).isEmpty());
            check("a day later the limit is free again", risk.refusal("eth-mainnet", "00000000-0000-4000-8000-000000000099", most, BigInteger.ONE, WHEN + 86401).isEmpty());
            // Anything changed is refused
            risk = new DexRisk(DexConfig.CONTROL_KEY, new File(dir, "ledger2.json"), handler, nothing);
            check("a changed payload is refused", !risk.accept(document(PAYLOAD.replace("12024664992833299", "92024664992833299"), SIGNATURE).getBytes("UTF-8"), "eth-mainnet", WHEN));
            check("a changed signature is refused", !risk.accept(document(PAYLOAD, "19" + SIGNATURE.substring(2)).getBytes("UTF-8"), "eth-mainnet", WHEN));
            check("a document of another network is refused", !risk.accept(document(PAYLOAD, SIGNATURE).getBytes("UTF-8"), "bnb-mainnet", WHEN));
            check("an expired document is refused", !risk.accept(document(PAYLOAD, SIGNATURE).getBytes("UTF-8"), "eth-mainnet", 1795037244L));
            check("a document from the future is refused", !risk.accept(document(PAYLOAD, SIGNATURE).getBytes("UTF-8"), "eth-mainnet", 1794432443L - 301));
            check("with nothing accepted, new trades are refused", !risk.refusal("eth-mainnet", "6f666665-722d-4368-9563-6b0000000000", BigInteger.ONE, BigInteger.ONE, WHEN).isEmpty());
            final DexRisk other = new DexRisk("00" + DexConfig.CONTROL_KEY.substring(2), new File(dir, "ledger3.json"), handler, nothing);
            check("another key does not accept the owner's document", !other.accept(document(PAYLOAD, SIGNATURE).getBytes("UTF-8"), "eth-mainnet", WHEN));
            // The trading keys: made, locked, unlocked; a wrong password opens nothing
            final DexKeys keys = new DexKeys(context, dir);
            check("keys are made", keys.create("correct horse") && keys.ready());
            final String seed = keys.seedHex(), words = keys.words("correct horse");
            keys.lock();
            check("locked keys give no seed", keys.seedHex() == null && !keys.ready());
            check("a wrong password does not unlock", !keys.unlock("correct horsf") && !keys.ready() && keys.words("x") == null);
            check("the right password unlocks the same seed", keys.unlock("correct horse") && seed != null && seed.equals(keys.seedHex()));
            check("twelve recovery words", words != null && words.split(" ").length == 12);
            check("keys cannot be made twice", !keys.create("another"));
            final DexKeys again = new DexKeys(context, new File(dir, "restored"));
            DexFiles.privateDir(new File(dir, "restored"));
            check("the words restore the same seed", again.restore(words, "new password") && seed.equals(again.seedHex()));
            check("words with a wrong checksum are refused", !new DexKeys(context, new File(dir, "bad")).restore("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon", "p"));
            keys.lock();
            again.lock();
        } catch (Throwable t) {
            check("no error: " + t, false);
        }
        Log.i("DexSelfCheck", failures == 0 ? "RESULT all checks passed" : "RESULT " + failures + " FAILED");
    }
}
