package com.m2049r.xmrwallet.dex;

import static android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import com.m2049r.xmrwallet.LoginActivity;
import com.m2049r.xmrwallet.R;

import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Keeps the DEX link alive while the user is in the browser: a foreground service that owns the
 * trading engine and the loopback link to the DEX web page. Whatever the page needs from the user
 * (connect, trading password, approve a trade, move XMZ) arrives as a notification that opens the
 * wallet's own prompt; nothing is decided in the page.
 */
public class DexService extends Service implements DexBridge.Listener {
    public static final String ACTION_STOP = "com.m2049r.xmrwallet.dex.STOP";
    static final String PREFS = "dex", PREF_ENABLED = "enabled", PREF_HOME = "home", PREF_DAEMON = "daemon";
    private static final String CHANNEL_LINK = "dex_link", CHANNEL_ASK = "dex_ask";
    private static final int NOTE_LINK = 7101, NOTE_ASK = 7102, NOTE_ADD = 7103;

    private static volatile DexService instance;
    private static final List<Runnable> uiListeners = new ArrayList<>();
    private static final Handler main = new Handler(Looper.getMainLooper());

    private DexEngine engine;
    private DexBridge bridge;
    private PowerManager.WakeLock wakeLock;
    private String shownLine = "";
    private volatile boolean listening;

    // ---- for the wallet's screens (main thread) --------------------------------------------------

    static DexService get() {
        return instance;
    }

    static boolean running() {
        return instance != null;
    }

    DexEngine engine() {
        return engine;
    }

    DexBridge bridge() {
        return bridge;
    }

    boolean listening() {
        return listening;
    }

    static void addUiListener(Runnable r) {
        synchronized (uiListeners) {
            uiListeners.add(r);
        }
    }

    static void removeUiListener(Runnable r) {
        synchronized (uiListeners) {
            uiListeners.remove(r);
        }
    }

    private static void notifyUi() {
        final List<Runnable> copy;
        synchronized (uiListeners) {
            copy = new ArrayList<>(uiListeners);
        }
        for (Runnable r : copy) main.post(r);
    }

    static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** Is a request of the DEX page waiting for the user (connect, password, approve, move XMZ)? */
    boolean somethingWaiting() {
        final DexEngine e = engine;
        final DexBridge b = bridge;
        if (e == null || b == null) return false;
        return Boolean.TRUE.equals(e.call(() -> b.pairing() != null || (b.keysWanted() && !e.keysReady()) || e.awaitingApproval() || b.returnWanted(), false));
    }

    /** Opening the wallet while a request is waiting shows that request (as tapping its notification does). */
    public static void showWaiting(Context context) {
        final DexService s = instance;
        if (s != null && s.somethingWaiting())
            context.startActivity(new Intent(context, DexPromptActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP));
    }

    /** Switch the link on (from a screen of the app, so the app is in the foreground). */
    public static void start(Context context) {
        prefs(context).edit().putBoolean(PREF_ENABLED, true).apply();
        ContextCompat.startForegroundService(context, new Intent(context, DexService.class));
    }

    /** When the user opens the wallet and had left the link on: bring it back. */
    public static void startIfEnabled(Context context) {
        if (!running() && prefs(context).getBoolean(PREF_ENABLED, false)) {
            try {
                ContextCompat.startForegroundService(context, new Intent(context, DexService.class));
            } catch (Exception ignored) {
                // not allowed from the background: the user switches it on from the DEX screen
            }
        }
    }

    /** Switch the link off; refused (false) while a trade is running, which needs the app until it ends. */
    static boolean stop(Context context) {
        final DexService s = instance;
        if (s != null && s.engine != null && Boolean.TRUE.equals(s.engine.call(() -> s.engine.tradeRunning(), false))) return false;
        prefs(context).edit().putBoolean(PREF_ENABLED, false).apply();
        context.stopService(new Intent(context, DexService.class));
        return true;
    }

    // ---- service -------------------------------------------------------------------------------

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            final NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            final NotificationChannel link = new NotificationChannel(CHANNEL_LINK, getString(R.string.dex_channel_link), NotificationManager.IMPORTANCE_LOW);
            link.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
            manager.createNotificationChannel(link);
            final NotificationChannel ask = new NotificationChannel(CHANNEL_ASK, getString(R.string.dex_channel_ask), NotificationManager.IMPORTANCE_HIGH);
            ask.setLockscreenVisibility(Notification.VISIBILITY_PRIVATE);
            manager.createNotificationChannel(ask);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            if (!stop(this)) attention("trade", getString(R.string.dex_stop_refused_title), getString(R.string.dex_stop_refused_text));
            return START_NOT_STICKY;
        }
        foreground(getString(R.string.dex_link_starting));
        if (engine == null) {
            instance = this;
            engine = new DexEngine(this, prefs(this).getString(PREF_DAEMON, ""));
            final File root = new File(new File(getFilesDir(), "dex"), DexConfig.networkFolder());
            bridge = new DexBridge(engine, new File(root, "web-page.json"), this);
            engine.post(() -> {
                listening = bridge.listen();
                bridgeChanged();
            });
            engine.start();
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        instance = null;
        if (bridge != null) bridge.close();
        if (engine != null) engine.stop();
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        final NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        manager.cancel(NOTE_ASK);
        manager.cancel(NOTE_ADD);
        notifyUi();
        super.onDestroy();
    }

    private PendingIntent open(Class<?> activity, int request) {
        final Intent intent = new Intent(this, activity).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        return PendingIntent.getActivity(this, request, intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private void foreground(String line) {
        shownLine = line;
        final Intent stop = new Intent(this, DexService.class).setAction(ACTION_STOP);
        final Notification notification = new NotificationCompat.Builder(this, CHANNEL_LINK)
                .setContentTitle(getString(R.string.dex_link_title))
                .setContentText(line)
                .setStyle(new NotificationCompat.BigTextStyle().bigText(line))
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setSmallIcon(R.drawable.ic_monerujo)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setCategory(NotificationCompat.CATEGORY_SERVICE)
                .setGroup("dex_link")
                .setContentIntent(open(DexActivity.class, 1))
                .addAction(0, getString(R.string.dex_link_off), PendingIntent.getService(this, 2, stop, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE))
                .build();
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) startForeground(NOTE_LINK, notification, FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            else startForeground(NOTE_LINK, notification);
        } catch (RuntimeException notAllowed) {
            // The phone restarted the service while the app is in the background and does not let it
            // show itself: it comes back when the user opens the wallet
            stopSelf();
        }
    }

    // ---- from the bridge (engine's thread) -------------------------------------------------------

    @Override
    public void attention(String kind, String title, String text) {
        final Notification notification = new NotificationCompat.Builder(this, CHANNEL_ASK)
                .setContentTitle(title)
                .setContentText(text)
                .setStyle(new NotificationCompat.BigTextStyle().bigText(text))
                .setSmallIcon(R.drawable.ic_monerujo)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setCategory(NotificationCompat.CATEGORY_MESSAGE)
                .setDefaults(NotificationCompat.DEFAULT_ALL)
                .setAutoCancel(true)
                .setGroup("dex_ask")   // its own group: bundled with the link's notification, a tap would open the app instead
                .setContentIntent(open(DexPromptActivity.class, 3))
                .build();
        ((NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE)).notify(NOTE_ASK, notification);
    }

    @Override
    public void xmzAddRequested(String address, String xmz) {
        // The wallet's own Send screen, with the trading wallet's address filled in: the user picks
        // the wallet, checks the amount and confirms as for any payment
        final StringBuilder uri = new StringBuilder("monzero:").append(address);
        if (xmz != null && !xmz.isEmpty()) uri.append("?tx_amount=").append(xmz);
        final Intent send = new Intent(Intent.ACTION_VIEW, Uri.parse(uri.toString()), this, LoginActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        final Notification notification = new NotificationCompat.Builder(this, CHANNEL_ASK)
                .setContentTitle(getString(R.string.dex_add_title))
                .setContentText(getString(R.string.dex_add_text))
                .setStyle(new NotificationCompat.BigTextStyle().bigText(getString(R.string.dex_add_text)))
                .setSmallIcon(R.drawable.ic_monerujo)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setCategory(NotificationCompat.CATEGORY_MESSAGE)
                .setDefaults(NotificationCompat.DEFAULT_ALL)
                .setAutoCancel(true)
                .setGroup("dex_add")
                .setContentIntent(PendingIntent.getActivity(this, 4, send, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE))
                .build();
        ((NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE)).notify(NOTE_ADD, notification);
    }

    @Override
    public void bridgeChanged() {
        final String line = statusLine();
        final boolean trading = engine.tradeRunning();
        main.post(() -> {
            if (instance != this) return;
            if (!line.equals(shownLine)) foreground(line);
            // A running trade must reach its end: keep the processor awake until it does
            if (trading && (wakeLock == null || !wakeLock.isHeld())) {
                if (wakeLock == null) wakeLock = ((PowerManager) getSystemService(Context.POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "monzero:dex-trade");
                wakeLock.acquire(6 * 60 * 60 * 1000L);
            } else if (!trading && wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        });
        notifyUi();
    }

    /** One line for the ongoing notification. On the engine's thread. */
    private String statusLine() {
        if (!listening) return getString(R.string.dex_link_no_port);
        final JSONObject trade = engine.tradeStatus();
        final JSONObject snapshot = trade.optJSONObject("snapshot");
        if (trade.optBoolean("configured") && snapshot != null && snapshot.has("stage") && !snapshot.optBoolean("done")) {
            if (engine.awaitingApproval()) return getString(R.string.dex_line_approve);
            return getString(R.string.dex_line_trade, DexText.stage(this, snapshot));
        }
        if (bridge.connected()) return getString(R.string.dex_line_connected, bridge.origin().replace("https://", ""));
        return getString(R.string.dex_line_waiting);
    }
}
