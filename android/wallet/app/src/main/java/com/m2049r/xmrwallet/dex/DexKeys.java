package com.m2049r.xmrwallet.dex;

import android.content.Context;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * The trading keys of this phone: twelve recovery words made on the device, kept encrypted under
 * the trading password (Argon2id, then AES-256-GCM) in the app's private storage. Unlocked, the
 * wallet seed stays in memory until the DEX link is switched off; the trade helper derives the
 * swap keys and the trade account from it for each step.
 */
final class DexKeys {
    private static final String SCHEMA = "monzero-android-dex-keys-v1";
    private static final int OPERATIONS = 3, MEMORY_MIB = 64;
    private final File file;
    private final Context context;
    private byte[] seed;   // the 64-byte BIP-39 seed while unlocked
    private List<String> list;

    DexKeys(Context context, File root) {
        this.context = context.getApplicationContext();
        this.file = new File(root, "trading-keys.json");
    }

    boolean exist() {
        return file.isFile();
    }

    synchronized boolean ready() {
        return seed != null;
    }

    /** Hex of the wallet seed for one helper request; null while locked. */
    synchronized String seedHex() {
        return seed == null ? null : DexFiles.hex(seed);
    }

    synchronized void lock() {
        if (seed != null) Arrays.fill(seed, (byte) 0);
        seed = null;
    }

    private List<String> wordList() throws Exception {
        if (list != null) return list;
        final List<String> words = new ArrayList<>(2048);
        try (BufferedReader in = new BufferedReader(new InputStreamReader(context.getAssets().open("dex/bip39-english.txt"), "UTF-8"))) {
            String line;
            while ((line = in.readLine()) != null) if (!line.trim().isEmpty()) words.add(line.trim());
        }
        if (words.size() != 2048) throw new IllegalStateException("word list");
        list = words;
        return list;
    }

    private static byte[] key(String password, byte[] salt, int operations, int memory) throws Exception {
        final byte[] bytes = password.getBytes("UTF-8");
        final byte[] key = DexNative.passwordKey(bytes, salt, operations, memory);
        Arrays.fill(bytes, (byte) 0);
        if (key == null || key.length != 32) throw new IllegalStateException("password hash");
        return key;
    }

    /** Make the keys (first use). Slow (the password hash): never on the main thread. */
    synchronized boolean create(String password) {
        if (exist() || password == null || password.isEmpty()) return false;
        final SecureRandom random = new SecureRandom();
        final byte[] entropy = new byte[16], salt = new byte[16], nonce = new byte[12];
        random.nextBytes(entropy);
        return store(entropy, salt, nonce, random, password);
    }

    /** Put back keys from their twelve words (a new phone, or a reinstall). */
    synchronized boolean restore(String words, String password) {
        if (exist() || password == null || password.isEmpty()) return false;
        try {
            final byte[] entropy = DexMnemonic.entropy(words, wordList());
            if (entropy == null) return false;
            return store(entropy, new byte[16], new byte[12], new SecureRandom(), password);
        } catch (Exception e) {
            return false;
        }
    }

    private boolean store(byte[] entropy, byte[] salt, byte[] nonce, SecureRandom random, String password) {
        byte[] key = null;
        try {
            random.nextBytes(salt);
            random.nextBytes(nonce);
            key = key(password, salt, OPERATIONS, MEMORY_MIB);
            final Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, nonce));
            cipher.updateAAD(SCHEMA.getBytes("UTF-8"));
            final byte[] sealed = cipher.doFinal(entropy);
            final JSONObject o = new JSONObject();
            o.put("schema", SCHEMA);
            o.put("salt", DexFiles.hex(salt));
            o.put("operations", OPERATIONS);
            o.put("memoryMiB", MEMORY_MIB);
            o.put("nonce", DexFiles.hex(nonce));
            o.put("sealed", DexFiles.hex(sealed));
            final byte[] made = DexMnemonic.seed(DexMnemonic.words(entropy, wordList()));
            if (!DexFiles.write(file, o.toString())) {
                Arrays.fill(made, (byte) 0);
                return false;
            }
            lock();
            seed = made;
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            Arrays.fill(entropy, (byte) 0);
            if (key != null) Arrays.fill(key, (byte) 0);
        }
    }

    private byte[] open(String password) {
        final JSONObject o = DexFiles.readJson(file, 4096);
        if (o == null || !SCHEMA.equals(o.optString("schema")) || password == null || password.isEmpty()) return null;
        final byte[] salt = DexFiles.unhex(o.optString("salt")), nonce = DexFiles.unhex(o.optString("nonce")), sealed = DexFiles.unhex(o.optString("sealed"));
        final int operations = o.optInt("operations"), memory = o.optInt("memoryMiB");
        if (salt == null || salt.length != 16 || nonce == null || nonce.length != 12 || sealed == null || sealed.length != 32) return null;
        byte[] key = null;
        try {
            key = key(password, salt, operations, memory);
            final Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, nonce));
            cipher.updateAAD(SCHEMA.getBytes("UTF-8"));
            return cipher.doFinal(sealed);   // a wrong password fails the tag
        } catch (Exception e) {
            return null;
        } finally {
            if (key != null) Arrays.fill(key, (byte) 0);
        }
    }

    /** Unlock with the trading password. Slow: never on the main thread. */
    synchronized boolean unlock(String password) {
        final byte[] entropy = open(password);
        if (entropy == null) return false;
        try {
            final byte[] made = DexMnemonic.seed(DexMnemonic.words(entropy, wordList()));
            lock();
            seed = made;
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            Arrays.fill(entropy, (byte) 0);
        }
    }

    /** The twelve recovery words, for the user to write down; null on a wrong password. */
    synchronized String words(String password) {
        final byte[] entropy = open(password);
        if (entropy == null) return null;
        try {
            return DexMnemonic.words(entropy, wordList());
        } catch (Exception e) {
            return null;
        } finally {
            Arrays.fill(entropy, (byte) 0);
        }
    }
}
