package org.monzero.miner;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.regex.Pattern;

/** The user's mining choices, kept in app-private preferences. */
public final class MinerSettings {
    public static final String DEFAULT_POOL = "pool.monzero.org:3333";

    // Monzero standard addresses (prefix 86) and subaddresses (88) are 95 base58
    // characters starting with F; integrated addresses (87) are 106.
    private static final Pattern ADDRESS =
            Pattern.compile("^F[1-9A-HJ-NP-Za-km-z]{94}([1-9A-HJ-NP-Za-km-z]{11})?$");
    private static final Pattern POOL = Pattern.compile("^[A-Za-z0-9.-]+:[0-9]{1,5}$");
    private static final Pattern WORKER = Pattern.compile("^[A-Za-z0-9_-]{0,32}$");

    public String address = "";
    public String pool = DEFAULT_POOL;
    public String worker = "android";
    public int threads;
    public boolean onlyWhileCharging = true;
    public int maxBatteryTempC = 40;
    public boolean lightMode = false;

    public static int cores() {
        return Math.max(1, Runtime.getRuntime().availableProcessors());
    }

    public static MinerSettings load(Context context) {
        SharedPreferences p = prefs(context);
        MinerSettings s = new MinerSettings();
        s.address = p.getString("address", "");
        s.pool = p.getString("pool", DEFAULT_POOL);
        s.worker = p.getString("worker", "android");
        // Half the cores by default: phones throttle hard and stay usable this way.
        s.threads = p.getInt("threads", Math.max(1, cores() / 2));
        s.onlyWhileCharging = p.getBoolean("onlyWhileCharging", true);
        s.maxBatteryTempC = p.getInt("maxBatteryTempC", 40);
        s.lightMode = p.getBoolean("lightMode", false);
        return s;
    }

    public void save(Context context) {
        prefs(context).edit()
                .putString("address", address)
                .putString("pool", pool)
                .putString("worker", worker)
                .putInt("threads", threads)
                .putBoolean("onlyWhileCharging", onlyWhileCharging)
                .putInt("maxBatteryTempC", maxBatteryTempC)
                .putBoolean("lightMode", lightMode)
                .apply();
    }

    /** Returns a string resource id describing the first problem, or 0 if the settings are usable. */
    public int problem() {
        if (!ADDRESS.matcher(address).matches()) return R.string.error_address;
        if (!POOL.matcher(pool).matches()) return R.string.error_pool;
        if (!WORKER.matcher(worker).matches()) return R.string.error_worker;
        if (threads < 1 || threads > cores()) return R.string.error_threads;
        return 0;
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("miner", Context.MODE_PRIVATE);
    }
}
