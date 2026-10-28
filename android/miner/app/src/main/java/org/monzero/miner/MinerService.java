package org.monzero.miner;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Runs the bundled XMRig binary while the user wants to mine, and pauses it when the
 * phone is unplugged (if requested) or the battery is too hot.
 */
public class MinerService extends Service {
    public static final String ACTION_START = "org.monzero.miner.START";
    public static final String ACTION_STOP = "org.monzero.miner.STOP";

    private static final String CHANNEL = "mining";
    private static final int NOTIFICATION_ID = 1;
    private static final int LOG_LINES = 200;
    // Resume only once the battery has cooled this far below the limit.
    private static final int RESUME_HYSTERESIS_C = 3;

    public enum State {STOPPED, MINING, PAUSED_UNPLUGGED, PAUSED_HOT, FAILED}

    /** A snapshot the activity can draw. */
    public static final class Status {
        public State state = State.STOPPED;
        public double hashrate10s = -1;
        public int accepted;
        public int rejected;
        public long poolHeight = -1;
        public String message = "";
    }

    public interface Listener {
        void onStatus(Status status, List<String> log);
    }

    private static final Pattern SPEED = Pattern.compile("speed 10s/60s/15m ([0-9.]+|n/a)");
    private static final Pattern ACCEPTED = Pattern.compile("accepted \\((\\d+)/(\\d+)\\)");
    private static final Pattern REJECTED = Pattern.compile("rejected \\((\\d+)/(\\d+)\\)");
    private static final Pattern HEIGHT = Pattern.compile("new job from .* height (\\d+)");

    private static final Object lock = new Object();
    private static final Status status = new Status();
    private static final ArrayDeque<String> log = new ArrayDeque<>();
    private static Listener listener;

    private final Handler main = new Handler(Looper.getMainLooper());
    private MinerSettings settings;
    private Process process;
    private PowerManager.WakeLock wakeLock;
    private boolean charging;
    private int batteryTempC;

    public static void setListener(Listener l) {
        synchronized (lock) {
            listener = l;
        }
        publish();
    }

    public static State state() {
        synchronized (lock) {
            return status.state;
        }
    }

    private final BroadcastReceiver batteryReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int plugged = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0);
            charging = plugged != 0;
            batteryTempC = intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) / 10;
            applyPolicy();
        }
    };

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_STOP.equals(intent.getAction())) {
            stopMining(State.STOPPED, getString(R.string.status_stopped));
            stopSelf();
            return START_NOT_STICKY;
        }
        settings = MinerSettings.load(this);
        if (settings.problem() != 0) {
            stopMining(State.FAILED, getString(settings.problem()));
            stopSelf();
            return START_NOT_STICKY;
        }
        startForegroundCompat(getString(R.string.status_starting));
        Intent sticky = registerReceiver(batteryReceiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (sticky != null) batteryReceiver.onReceive(this, sticky);
        else applyPolicy();
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        try {
            unregisterReceiver(batteryReceiver);
        } catch (IllegalArgumentException ignored) {
            // never registered
        }
        killProcess();
        super.onDestroy();
    }

    /** Starts or pauses XMRig to match the charging and temperature rules. */
    private void applyPolicy() {
        if (settings == null) return; // stopped
        State current = state();
        if (settings.onlyWhileCharging && !charging) {
            pause(State.PAUSED_UNPLUGGED, getString(R.string.status_paused_unplugged));
        } else if (batteryTempC >= settings.maxBatteryTempC) {
            pause(State.PAUSED_HOT, getString(R.string.status_paused_hot, batteryTempC));
        } else if (current == State.PAUSED_HOT
                && batteryTempC > settings.maxBatteryTempC - RESUME_HYSTERESIS_C) {
            // still cooling down
        } else if (process == null) {
            launch();
        }
    }

    private void pause(State why, String message) {
        killProcess();
        update(s -> {
            s.state = why;
            s.hashrate10s = -1;
            s.message = message;
        });
        updateNotification(message);
    }

    private void launch() {
        File binary = new File(getApplicationInfo().nativeLibraryDir, "libxmrig.so");
        List<String> command = new ArrayList<>();
        command.add(binary.getAbsolutePath());
        command.add("--url=" + settings.pool);
        command.add("--user=" + settings.address);
        command.add("--pass=" + (settings.worker.isEmpty() ? "x" : settings.worker));
        if (!settings.worker.isEmpty()) command.add("--rig-id=" + settings.worker);
        command.add("--algo=rx/0");
        command.add("--threads=" + settings.threads);
        command.add("--keepalive");
        command.add("--donate-level=1"); // XMRig developer donation
        command.add("--no-color");
        command.add("--print-time=10");
        command.add("--cpu-priority=0");
        if (settings.lightMode) command.add("--randomx-mode=light");
        try {
            ProcessBuilder builder = new ProcessBuilder(command)
                    .directory(getFilesDir())
                    .redirectErrorStream(true);
            process = builder.start();
        } catch (IOException e) {
            stopMining(State.FAILED, getString(R.string.status_failed, e.getMessage()));
            stopSelf();
            return;
        }
        acquireWakeLock();
        update(s -> {
            s.state = State.MINING;
            s.message = getString(R.string.status_connecting, settings.pool);
        });
        updateNotification(getString(R.string.status_connecting, settings.pool));
        final Process started = process;
        new Thread(() -> readOutput(started), "xmrig-output").start();
    }

    private void readOutput(Process p) {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
            String line;
            while ((line = reader.readLine()) != null) {
                parse(line);
            }
        } catch (IOException ignored) {
            // process killed
        }
        int code;
        try {
            code = p.waitFor();
        } catch (InterruptedException e) {
            return;
        }
        main.post(() -> {
            if (process != p) return; // a pause or stop already replaced it
            process = null;
            stopMining(State.FAILED, getString(R.string.status_exited, code));
            stopSelf();
        });
    }

    private void parse(String line) {
        Matcher m;
        String text = line.replaceFirst("^\\[[^]]*]\\s*", "");
        synchronized (lock) {
            log.addLast(text);
            while (log.size() > LOG_LINES) log.removeFirst();
            if ((m = SPEED.matcher(line)).find()) {
                status.hashrate10s = m.group(1).equals("n/a") ? -1 : Double.parseDouble(m.group(1));
                status.message = "";
            } else if ((m = ACCEPTED.matcher(line)).find()) {
                status.accepted = Integer.parseInt(m.group(1));
                status.rejected = Integer.parseInt(m.group(2));
            } else if ((m = REJECTED.matcher(line)).find()) {
                status.accepted = Integer.parseInt(m.group(1));
                status.rejected = Integer.parseInt(m.group(2));
            } else if ((m = HEIGHT.matcher(line)).find()) {
                status.poolHeight = Long.parseLong(m.group(1));
                status.message = "";
            }
        }
        publish();
        if (SPEED.matcher(line).find()) {
            main.post(() -> updateNotification(notificationText()));
        }
    }

    private String notificationText() {
        synchronized (lock) {
            if (status.hashrate10s < 0) return getString(R.string.status_warming_up);
            return getString(R.string.notification_mining, status.hashrate10s, status.accepted);
        }
    }

    private void stopMining(State state, String message) {
        settings = null; // later battery broadcasts must not relaunch
        killProcess();
        update(s -> {
            s.state = state;
            s.hashrate10s = -1;
            s.message = message;
        });
        stopForeground(STOP_FOREGROUND_REMOVE);
    }

    private void killProcess() {
        Process p = process;
        process = null;
        if (p != null) {
            p.destroy();
            try {
                if (Build.VERSION.SDK_INT >= 26 && !p.waitFor(3, java.util.concurrent.TimeUnit.SECONDS)) {
                    p.destroyForcibly();
                }
            } catch (InterruptedException ignored) {
                p.destroyForcibly();
            }
        }
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
    }

    private void acquireWakeLock() {
        if (wakeLock == null) {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "monzero:mining");
            wakeLock.setReferenceCounted(false);
        }
        wakeLock.acquire();
    }

    private interface Change {
        void apply(Status s);
    }

    private static void update(Change change) {
        synchronized (lock) {
            change.apply(status);
        }
        publish();
    }

    private static void publish() {
        Listener l;
        Status copy = new Status();
        List<String> lines;
        synchronized (lock) {
            l = listener;
            if (l == null) return;
            copy.state = status.state;
            copy.hashrate10s = status.hashrate10s;
            copy.accepted = status.accepted;
            copy.rejected = status.rejected;
            copy.poolHeight = status.poolHeight;
            copy.message = status.message;
            lines = new ArrayList<>(log);
        }
        new Handler(Looper.getMainLooper()).post(() -> l.onStatus(copy, lines));
    }

    private void startForegroundCompat(String text) {
        Notification n = buildNotification(text);
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTIFICATION_ID, n);
        }
    }

    private void updateNotification(String text) {
        if (state() == State.STOPPED || state() == State.FAILED) return;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        nm.notify(NOTIFICATION_ID, buildNotification(text));
    }

    private Notification buildNotification(String text) {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CHANNEL) == null) {
            nm.createNotificationChannel(new NotificationChannel(CHANNEL,
                    getString(R.string.channel_mining), NotificationManager.IMPORTANCE_LOW));
        }
        PendingIntent open = PendingIntent.getActivity(this, 0,
                new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE);
        PendingIntent stop = PendingIntent.getService(this, 1,
                new Intent(this, MinerService.class).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        return b.setSmallIcon(R.drawable.ic_notification)
                .setContentTitle(getString(R.string.app_name))
                .setContentText(text)
                .setOngoing(true)
                .setContentIntent(open)
                .addAction(new Notification.Action.Builder(null, getString(R.string.action_stop), stop).build())
                .build();
    }
}
