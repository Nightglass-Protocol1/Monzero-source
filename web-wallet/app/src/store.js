// The wallet as Monzero's wallet code exports it: the keys file (encrypted with the user's password)
// and the cache (encrypted with a key derived from the wallet keys). Kept in this site's IndexedDB,
// one record per network. Nothing here is ever sent anywhere.
const DB = "monzero-web-wallet";
const STORE = "wallets";

function open() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE);
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function run(mode, fn) {
  return open().then((db) => new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, mode);
    const result = fn(tx.objectStore(STORE));
    tx.oncomplete = () => { db.close(); resolve(result && "result" in result ? result.result : undefined); };
    tx.onerror = () => { db.close(); reject(tx.error); };
  }));
}

export const loadWallet = (network) => run("readonly", (s) => s.get(network));
export const saveWallet = (network, keys, cache) =>
  run("readwrite", (s) => s.put({ keys, cache, savedAt: Date.now() }, network));
export const removeWallet = (network) => run("readwrite", (s) => s.delete(network));
