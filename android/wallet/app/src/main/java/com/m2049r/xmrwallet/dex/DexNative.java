package com.m2049r.xmrwallet.dex;

/** libsodium, through the wallet's native library (see cpp/dex_native.cpp). */
final class DexNative {
    static {
        System.loadLibrary("monerujo");
    }

    private DexNative() {
    }

    /** Ed25519 detached signature check (crypto_sign_verify_detached). */
    static native boolean verify(byte[] signature, byte[] message, byte[] publicKey);

    /** Argon2id: 32 bytes from a password and a 16-byte salt; null when it could not be computed. */
    static native byte[] passwordKey(byte[] password, byte[] salt, int operations, int memoryMiB);
}
