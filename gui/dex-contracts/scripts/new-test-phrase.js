// Creates the dedicated recovery phrase for the real-funds test and saves it privately (0600):
//   node scripts/new-test-phrase.js [~/.config/monzero-dex/bsc-mainnet-accounts.json]
// Prints only the two addresses to fund (buyer = account 0, seller = account 1). The phrase
// itself is never printed: it stays in the file. Never put a main wallet's phrase here.
const { ethers } = require("ethers");
const fs = require("fs");
const os = require("os");
const path = require("path");

const out = process.argv[2] || path.join(os.homedir(), ".config", "monzero-dex", "bsc-mainnet-accounts.json");
if (fs.existsSync(out)) {
  console.error(`${out} already exists; not overwriting it (it may hold funds).`);
  process.exit(1);
}
const mnemonic = ethers.Wallet.createRandom().mnemonic.phrase;
const account = (i) => ethers.HDNodeWallet.fromPhrase(mnemonic, undefined, `m/44'/60'/0'/0/${i}`).address;
fs.mkdirSync(path.dirname(out), { recursive: true, mode: 0o700 });
fs.writeFileSync(out, JSON.stringify({ mnemonic, purpose: "Monzero DEX real-funds test only" }) + "\n", { mode: 0o600, flag: "wx" });
console.log(`Saved ${out} (private, 0600). Back it up like any wallet while it holds funds.`);
console.log(`Buyer  (account 0, pays BNB):  ${account(0)}`);
console.log(`Seller (account 1, sells XMZ): ${account(1)}`);
