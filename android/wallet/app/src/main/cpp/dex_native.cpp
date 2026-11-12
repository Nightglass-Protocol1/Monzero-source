// What the DEX web link needs from libsodium (already linked for the wallet): the signature check
// of the owner-signed control document, exactly as the desktop wallet does it, and the password
// hash that protects the trading keys on this device.
#include <jni.h>
#include <cstddef>

extern "C" {
int sodium_init(void);
int crypto_sign_verify_detached(const unsigned char *sig, const unsigned char *m,
                                unsigned long long mlen, const unsigned char *pk);
int crypto_pwhash(unsigned char *const out, unsigned long long outlen, const char *const passwd,
                  unsigned long long passwdlen, const unsigned char *const salt,
                  unsigned long long opslimit, size_t memlimit, int alg);
void sodium_memzero(void *const pnt, const size_t len);

JNIEXPORT jboolean JNICALL
Java_com_m2049r_xmrwallet_dex_DexNative_verify(JNIEnv *env, jclass, jbyteArray signature,
                                               jbyteArray message, jbyteArray publicKey) {
    if (!signature || !message || !publicKey || sodium_init() < 0) return JNI_FALSE;
    if (env->GetArrayLength(signature) != 64 || env->GetArrayLength(publicKey) != 32) return JNI_FALSE;
    const jsize length = env->GetArrayLength(message);
    jbyte *s = env->GetByteArrayElements(signature, nullptr);
    jbyte *m = env->GetByteArrayElements(message, nullptr);
    jbyte *k = env->GetByteArrayElements(publicKey, nullptr);
    const bool ok = s && m && k
        && crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(s),
                                       reinterpret_cast<const unsigned char *>(m),
                                       static_cast<unsigned long long>(length),
                                       reinterpret_cast<const unsigned char *>(k)) == 0;
    if (s) env->ReleaseByteArrayElements(signature, s, JNI_ABORT);
    if (m) env->ReleaseByteArrayElements(message, m, JNI_ABORT);
    if (k) env->ReleaseByteArrayElements(publicKey, k, JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}

// Argon2id (libsodium's crypto_pwhash, algorithm 2): 32 bytes from the password and a 16-byte salt
JNIEXPORT jbyteArray JNICALL
Java_com_m2049r_xmrwallet_dex_DexNative_passwordKey(JNIEnv *env, jclass, jbyteArray password,
                                                    jbyteArray salt, jint operations, jint memoryMiB) {
    if (!password || !salt || sodium_init() < 0 || env->GetArrayLength(salt) != 16
        || operations < 1 || operations > 16 || memoryMiB < 8 || memoryMiB > 1024) return nullptr;
    const jsize length = env->GetArrayLength(password);
    jbyte *p = env->GetByteArrayElements(password, nullptr);
    jbyte *s = env->GetByteArrayElements(salt, nullptr);
    unsigned char key[32];
    const bool ok = p && s
        && crypto_pwhash(key, sizeof key, reinterpret_cast<const char *>(p),
                         static_cast<unsigned long long>(length),
                         reinterpret_cast<const unsigned char *>(s),
                         static_cast<unsigned long long>(operations),
                         static_cast<size_t>(memoryMiB) * 1024 * 1024, 2) == 0;
    if (p) env->ReleaseByteArrayElements(password, p, JNI_ABORT);
    if (s) env->ReleaseByteArrayElements(salt, s, JNI_ABORT);
    jbyteArray out = nullptr;
    if (ok) {
        out = env->NewByteArray(sizeof key);
        if (out) env->SetByteArrayRegion(out, 0, sizeof key, reinterpret_cast<const jbyte *>(key));
    }
    sodium_memzero(key, sizeof key);
    return out;
}
}
