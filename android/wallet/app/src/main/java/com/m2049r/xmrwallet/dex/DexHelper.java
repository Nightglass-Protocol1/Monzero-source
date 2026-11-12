package com.m2049r.xmrwallet.dex;

import android.os.Handler;
import android.util.Log;

import com.m2049r.xmrwallet.BuildConfig;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Runs the trade helper (the same program the desktop wallet runs, built for Android): one JSON
 * request on its input, one JSON answer on its output, then it exits. Answers are delivered on the
 * engine's thread.
 */
final class DexHelper {
    static final String REQUEST_SCHEMA = "monzero-xmz-evm-helper-request-v1";
    static final String RESPONSE_SCHEMA = "monzero-xmz-evm-helper-response-v1";

    interface Done {
        /** answer is null when the helper gave no valid answer (crashed, timed out, too much output). */
        void done(JSONObject answer);
    }

    private final File program, workDir;
    private final Handler handler;
    private final ExecutorService pool = Executors.newCachedThreadPool();

    DexHelper(File program, File workDir, Handler handler) {
        this.program = program;
        this.workDir = workDir;
        this.handler = handler;
    }

    boolean available() {
        return program.isFile() && program.canExecute();
    }

    void shutdown() {
        pool.shutdownNow();
    }

    void run(final JSONObject request, final long timeoutMs, final int maxOutput, final Done done) {
        final byte[] input;
        try {
            input = request.toString().getBytes("UTF-8");
        } catch (Exception e) {
            handler.post(() -> done.done(null));
            return;
        }
        pool.execute(() -> {
            JSONObject answer = null;
            Process process = null;
            String why = "";
            final AtomicBoolean killed = new AtomicBoolean(false);
            try {
                final ProcessBuilder builder = new ProcessBuilder(program.getAbsolutePath());
                builder.directory(workDir);
                builder.environment().clear();
                builder.environment().put("LANG", "C");
                process = builder.start();
                final Process running = process;
                // The deadline, and the error stream (read so the helper never blocks on it; never shown)
                final Thread watchdog = new Thread(() -> {
                    try {
                        Thread.sleep(timeoutMs);
                        killed.set(true);
                        running.destroy();
                    } catch (InterruptedException ignored) {
                    }
                });
                watchdog.setDaemon(true);
                watchdog.start();
                final StringBuilder errorText = new StringBuilder();
                final Thread errors = new Thread(() -> {
                    try (InputStream err = running.getErrorStream()) {
                        final byte[] sink = new byte[1024];
                        int n;
                        while ((n = err.read(sink)) >= 0) {
                            // Kept only by test builds, for their log; never shown
                            if (BuildConfig.DEBUG && errorText.length() < 2000) errorText.append(new String(sink, 0, n));
                        }
                    } catch (Exception ignored) {
                    }
                });
                errors.setDaemon(true);
                errors.start();
                try (OutputStream out = process.getOutputStream()) {
                    out.write(input);
                }
                final ByteArrayOutputStream collected = new ByteArrayOutputStream();
                boolean tooMuch = false;
                try (InputStream in = process.getInputStream()) {
                    final byte[] buffer = new byte[8192];
                    int n;
                    while ((n = in.read(buffer)) >= 0) {
                        if (collected.size() + n > maxOutput) {
                            tooMuch = true;
                            break;
                        }
                        collected.write(buffer, 0, n);
                    }
                }
                if (tooMuch) process.destroy();
                final int exit = process.waitFor();   // a refusal exits 1 with ok:false: the answer counts either way
                watchdog.interrupt();
                why = "exit " + exit + (tooMuch ? ", too much output" : "") + (killed.get() ? ", timed out" : "") + ", " + collected.size() + " bytes: " + errorText;
                if (!tooMuch && !killed.get()) {
                    final JSONObject parsed = new JSONObject(new String(collected.toByteArray(), "UTF-8").trim());
                    if (RESPONSE_SCHEMA.equals(parsed.optString("schema")) && parsed.opt("ok") instanceof Boolean) answer = parsed;
                }
            } catch (Exception e) {
                answer = null;
                why = why + " " + e;
            } finally {
                if (BuildConfig.DEBUG && answer == null) Log.w("DexHelper", request.optString("command") + ": no valid answer (" + why.trim() + ")");
                Arrays.fill(input, (byte) 0);   // may hold the wallet seed
                if (process != null) process.destroy();
            }
            final JSONObject result = answer;
            handler.post(() -> done.done(result));
        });
    }
}
