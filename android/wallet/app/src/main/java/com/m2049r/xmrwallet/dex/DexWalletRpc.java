package com.m2049r.xmrwallet.dex;

import java.io.File;
import java.io.InputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * One XMZ wallet RPC program (monzero-wallet-rpc, built for Android) serving one private folder on
 * a loopback port. It makes its own login (monzero-wallet-rpc.&lt;port&gt;.login in the folder), which
 * only this app can read; the trade helper logs in with it.
 */
final class DexWalletRpc {
    final File dir;
    final int port;
    private Process process;

    DexWalletRpc(File dir, int port) {
        this.dir = dir;
        this.port = port;
    }

    static int freePort() {
        try (ServerSocket probe = new ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))) {
            return probe.getLocalPort();
        } catch (Exception e) {
            return 0;
        }
    }

    static boolean portFree(int port) {
        try (ServerSocket probe = new ServerSocket(port, 1, InetAddress.getByName("127.0.0.1"))) {
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /** The login file appears just before the service accepts connections: true once it answers. */
    boolean listening() {
        if (!loginFile().isFile()) return false;
        try (java.net.Socket probe = new java.net.Socket()) {
            probe.connect(new java.net.InetSocketAddress(InetAddress.getByName("127.0.0.1"), port), 300);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /** A port kept next to a service folder, so a restart reuses it. */
    static int keptPort(File portFile, int fresh) {
        final byte[] saved = DexFiles.read(portFile, 16);
        if (saved != null) {
            try {
                final int p = Integer.parseInt(new String(saved).trim());
                if (p > 1024 && p < 65536) return p;
            } catch (NumberFormatException ignored) {
            }
        }
        if (fresh > 0) DexFiles.write(portFile, String.valueOf(fresh));
        return fresh;
    }

    File loginFile() {
        return new File(dir, "monzero-wallet-rpc." + port + ".login");
    }

    File pidFile() {
        return new File(dir.getParentFile(), dir.getName() + ".pid");
    }

    synchronized boolean running() {
        if (process == null) return false;
        try {
            process.exitValue();
            return false;
        } catch (IllegalThreadStateException stillRunning) {
            return true;
        }
    }

    private static int pidOf(Process p) {
        // "Process[pid=1234, hasExited=false]"
        final Matcher m = Pattern.compile("pid=([0-9]+)").matcher(String.valueOf(p));
        return m.find() ? Integer.parseInt(m.group(1)) : 0;
    }

    /**
     * A wallet RPC left running by an earlier session of this app (the app was killed): stopped only
     * when it is verifiably ours, the same program serving this very folder.
     */
    static void stopLeftover(File pidFile, File program, File dir) {
        final byte[] saved = DexFiles.read(pidFile, 16);
        if (saved == null) return;
        try {
            final int pid = Integer.parseInt(new String(saved).trim());
            final byte[] cmdline = DexFiles.readProc(new File("/proc/" + pid + "/cmdline"));
            if (pid > 1 && cmdline != null) {
                final String text = new String(cmdline, "UTF-8").replace('\0', '\n');
                if (text.contains(program.getName()) && text.contains(new File(dir, "wallets").getPath())) {
                    android.os.Process.sendSignal(pid, 15);
                    for (int i = 0; i < 50 && new File("/proc/" + pid).exists(); i++) Thread.sleep(100);
                    if (new File("/proc/" + pid).exists()) android.os.Process.killProcess(pid);
                }
            }
        } catch (Exception ignored) {
        }
        pidFile.delete();
    }

    synchronized boolean start(File program, String daemon, String networkFlag) {
        if (process != null || port <= 0 || !DexConfig.daemonAddress(daemon)) return false;
        final File wallets = new File(dir, "wallets");
        if (!DexFiles.privateDir(dir) || !DexFiles.privateDir(wallets)) return false;
        stopLeftover(pidFile(), program, dir);
        // A login file from a session that did not end cleanly would be taken for this one's
        final File[] old = dir.listFiles((d, name) -> name.startsWith("monzero-wallet-rpc.") && name.endsWith(".login"));
        if (old != null) for (File f : old) f.delete();
        if (!portFree(port)) return false;
        final List<String> args = new ArrayList<>();
        args.add(program.getAbsolutePath());
        if (!networkFlag.isEmpty()) args.add(networkFlag);
        args.add("--wallet-dir");
        args.add(wallets.getPath());
        args.add("--daemon-address");
        args.add(daemon);
        args.add("--rpc-bind-ip");
        args.add("127.0.0.1");
        args.add("--rpc-bind-port");
        args.add(String.valueOf(port));
        args.add("--non-interactive");
        args.add("--max-concurrency");
        args.add("2");
        args.add("--log-file");
        args.add(new File(dir, "rpc.log").getPath());
        args.add("--max-log-file-size");
        args.add("1048576");
        args.add("--max-log-files");
        args.add("2");
        try {
            final ProcessBuilder builder = new ProcessBuilder(args);
            builder.directory(dir);
            builder.environment().clear();
            builder.environment().put("LANG", "C");
            builder.environment().put("HOME", dir.getPath());
            builder.redirectErrorStream(true);
            process = builder.start();
            process.getOutputStream().close();
            final Process started = process;
            final Thread drain = new Thread(() -> {
                try (InputStream in = started.getInputStream()) {
                    final byte[] sink = new byte[1024];
                    while (in.read(sink) >= 0) { /* the service logs to its file */ }
                } catch (Exception ignored) {
                }
            });
            drain.setDaemon(true);
            drain.start();
            final int pid = pidOf(process);
            if (pid > 0) DexFiles.write(pidFile(), String.valueOf(pid));
            return true;
        } catch (Exception e) {
            process = null;
            return false;
        }
    }

    /** Asks the service to end (it saves its wallet), then makes sure it is gone. */
    synchronized void stop() {
        final Process p = process;
        process = null;
        if (p == null) return;
        final int pid = pidOf(p);
        final Thread t = new Thread(() -> {
            try {
                if (pid > 0) android.os.Process.sendSignal(pid, 15);
                else p.destroy();
                for (int i = 0; i < 100; i++) {
                    try {
                        p.exitValue();
                        break;
                    } catch (IllegalThreadStateException running) {
                        Thread.sleep(100);
                    }
                }
            } catch (Exception ignored) {
            }
            p.destroy();
            loginFile().delete();
            pidFile().delete();
        });
        t.start();
    }
}
