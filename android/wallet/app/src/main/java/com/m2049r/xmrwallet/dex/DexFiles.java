package com.m2049r.xmrwallet.dex;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;

/** Small private files in the app's own storage. */
final class DexFiles {
    private DexFiles() {
    }

    static boolean privateDir(File dir) {
        if (!dir.isDirectory() && !dir.mkdirs()) return false;
        dir.setReadable(false, false);
        dir.setWritable(false, false);
        dir.setExecutable(false, false);
        return dir.setReadable(true, true) && dir.setWritable(true, true) && dir.setExecutable(true, true);
    }

    /** The whole file, or null when it is missing, unreadable or larger than max. */
    static byte[] read(File file, int max) {
        if (!file.isFile() || file.length() > max) return null;
        try (InputStream in = new FileInputStream(file)) {
            final byte[] buffer = new byte[(int) file.length()];
            int have = 0;
            while (have < buffer.length) {
                final int n = in.read(buffer, have, buffer.length - have);
                if (n < 0) break;
                have += n;
            }
            return have == buffer.length ? buffer : null;
        } catch (Exception e) {
            return null;
        }
    }

    /** A /proc file (its length reads as 0): up to 4096 bytes. */
    static byte[] readProc(File file) {
        try (InputStream in = new FileInputStream(file)) {
            final byte[] buffer = new byte[4096];
            final int n = in.read(buffer);
            return n <= 0 ? null : java.util.Arrays.copyOf(buffer, n);
        } catch (Exception e) {
            return null;
        }
    }

    static JSONObject readJson(File file, int max) {
        final byte[] bytes = read(file, max);
        if (bytes == null) return null;
        try {
            return new JSONObject(new String(bytes, "UTF-8"));
        } catch (Exception e) {
            return null;
        }
    }

    /** Written beside the target, synced, then moved over it: never a half-written file. */
    static boolean write(File file, byte[] bytes) {
        final File tmp = new File(file.getParentFile(), file.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(bytes);
            out.getFD().sync();
        } catch (Exception e) {
            tmp.delete();
            return false;
        }
        tmp.setReadable(false, false);
        tmp.setReadable(true, true);
        tmp.setWritable(false, false);
        tmp.setWritable(true, true);
        if (tmp.renameTo(file)) return true;
        tmp.delete();
        return false;
    }

    static boolean write(File file, String text) {
        try {
            return write(file, text.getBytes("UTF-8"));
        } catch (Exception e) {
            return false;
        }
    }

    static String hex(byte[] bytes) {
        final char[] digits = "0123456789abcdef".toCharArray();
        final char[] out = new char[bytes.length * 2];
        for (int i = 0; i < bytes.length; i++) {
            out[2 * i] = digits[(bytes[i] >> 4) & 15];
            out[2 * i + 1] = digits[bytes[i] & 15];
        }
        return new String(out);
    }

    /** Lower or upper case hex; null when it is not hex. */
    static byte[] unhex(String text) {
        if (text == null || (text.length() & 1) != 0) return null;
        final byte[] out = new byte[text.length() / 2];
        for (int i = 0; i < out.length; i++) {
            final int hi = Character.digit(text.charAt(2 * i), 16), lo = Character.digit(text.charAt(2 * i + 1), 16);
            if (hi < 0 || lo < 0) return null;
            out[i] = (byte) (hi * 16 + lo);
        }
        return out;
    }

    /** A decimal amount of atomic units as coins with `places` decimals, trailing zeros removed. */
    static String units(String digits, int places) {
        if (digits == null || digits.isEmpty()) digits = "0";
        final StringBuilder d = new StringBuilder(digits);
        while (d.length() <= places) d.insert(0, '0');
        final String whole = d.substring(0, d.length() - places);
        String frac = d.substring(d.length() - places);
        int end = frac.length();
        while (end > 0 && frac.charAt(end - 1) == '0') end--;
        frac = frac.substring(0, end);
        return frac.isEmpty() ? whole : whole + "." + frac;
    }

    /** Coins as typed ("0.004") to atomic units; null when not a positive amount with at most `places` decimals. */
    static String atomic(String text, int places) {
        if (text == null) return null;
        final java.util.regex.Matcher m = java.util.regex.Pattern.compile("\\A([0-9]{1,30})(\\.([0-9]{1,30}))?\\z").matcher(text.trim());
        if (!m.matches()) return null;
        final String frac = m.group(3) == null ? "" : m.group(3);
        if (frac.length() > places) return null;
        final StringBuilder digits = new StringBuilder(m.group(1)).append(frac);
        for (int i = frac.length(); i < places; i++) digits.append('0');
        int start = 0;
        while (start < digits.length() - 1 && digits.charAt(start) == '0') start++;
        final String out = digits.substring(start);
        return out.equals("0") ? null : out;
    }
}
