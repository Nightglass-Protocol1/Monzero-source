package com.m2049r.xmrwallet.dex;

import java.security.MessageDigest;
import java.text.Normalizer;
import java.util.Arrays;
import java.util.List;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/**
 * BIP-39 recovery words (English list, 12 words) and their seed. The trade account is the standard
 * Ethereum account m/44'/60'/0'/0/0 of this seed, so the same words restore it in any EVM wallet.
 */
final class DexMnemonic {
    private DexMnemonic() {
    }

    /** 16 bytes of entropy as 12 words. */
    static String words(byte[] entropy, List<String> list) throws Exception {
        if (entropy.length != 16 || list.size() != 2048) throw new IllegalArgumentException("entropy");
        final byte[] hash = MessageDigest.getInstance("SHA-256").digest(entropy);
        // 128 bits of entropy and the first 4 bits of its SHA-256: twelve 11-bit numbers
        final byte[] bits = Arrays.copyOf(entropy, 17);
        bits[16] = hash[0];
        final StringBuilder out = new StringBuilder();
        for (int w = 0; w < 12; w++) {
            int index = 0;
            for (int b = 0; b < 11; b++) {
                final int at = w * 11 + b;
                index = (index << 1) | ((bits[at / 8] >> (7 - at % 8)) & 1);
            }
            if (w > 0) out.append(' ');
            out.append(list.get(index));
        }
        Arrays.fill(bits, (byte) 0);
        return out.toString();
    }

    /** The entropy of 12 valid words; null when a word is unknown or the checksum is wrong. */
    static byte[] entropy(String words, List<String> list) throws Exception {
        final String[] parts = words.trim().toLowerCase(java.util.Locale.ROOT).split("\\s+");
        if (parts.length != 12 || list.size() != 2048) return null;
        final byte[] bits = new byte[17];
        for (int w = 0; w < 12; w++) {
            final int index = list.indexOf(parts[w]);
            if (index < 0) return null;
            for (int b = 0; b < 11; b++) {
                final int at = w * 11 + b;
                if (((index >> (10 - b)) & 1) != 0) bits[at / 8] |= (byte) (1 << (7 - at % 8));
            }
        }
        final byte[] entropy = Arrays.copyOf(bits, 16);
        final byte[] hash = MessageDigest.getInstance("SHA-256").digest(entropy);
        final boolean ok = (bits[16] & 0xf0) == (hash[0] & 0xf0);
        Arrays.fill(bits, (byte) 0);
        if (!ok) {
            Arrays.fill(entropy, (byte) 0);
            return null;
        }
        return entropy;
    }

    /** The 64-byte BIP-39 seed of the words (no extra passphrase): PBKDF2-HMAC-SHA512, 2048 rounds. */
    static byte[] seed(String words) throws Exception {
        final byte[] password = Normalizer.normalize(words, Normalizer.Form.NFKD).getBytes("UTF-8");
        final Mac mac = Mac.getInstance("HmacSHA512");
        mac.init(new SecretKeySpec(password, "HmacSHA512"));
        mac.update("mnemonic".getBytes("UTF-8"));
        mac.update(new byte[]{0, 0, 0, 1});   // one 64-byte block
        byte[] u = mac.doFinal();
        final byte[] out = u.clone();
        for (int i = 1; i < 2048; i++) {
            u = mac.doFinal(u);
            for (int j = 0; j < out.length; j++) out[j] ^= u[j];
        }
        Arrays.fill(password, (byte) 0);
        Arrays.fill(u, (byte) 0);
        return out;
    }
}
