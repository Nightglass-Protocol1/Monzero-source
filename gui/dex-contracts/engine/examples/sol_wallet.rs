//! A stand-in for a Solana browser wallet (Phantom) in the page tests: one key, which signs the
//! message a page built and sends the transaction.
//!
//!   RPC=http://127.0.0.1:8899 SEED_HEX=<64 hex> cargo run --example sol_wallet -- address
//!   ... cargo run --example sol_wallet -- send <message, base58>
//!   ... cargo run --example sol_wallet -- airdrop <lamports>
use base64::Engine as _;
use ed25519_dalek::{Signer, SigningKey};
use serde_json::{json, Value};

async fn rpc(url: &str, method: &str, params: Value) -> Value {
    let reply: Value = reqwest::Client::new()
        .post(url)
        .json(&json!({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}))
        .send()
        .await
        .expect("node")
        .json()
        .await
        .expect("json");
    if let Some(e) = reply.get("error") {
        eprintln!("{method}: {e}");
        std::process::exit(1);
    }
    reply["result"].clone()
}

#[tokio::main]
async fn main() {
    let seed: [u8; 32] = hex::decode(std::env::var("SEED_HEX").expect("SEED_HEX")).expect("hex").try_into().expect("32 bytes");
    let key = SigningKey::from_bytes(&seed);
    let address = bs58::encode(key.verifying_key().to_bytes()).into_string();
    let args: Vec<String> = std::env::args().skip(1).collect();
    let url = std::env::var("RPC").unwrap_or_default();
    match args.first().map(String::as_str) {
        Some("address") => println!("{address}"),
        Some("airdrop") => println!("{}", rpc(&url, "requestAirdrop", json!([address, args[1].parse::<u64>().expect("lamports")])).await),
        Some("send") => {
            let message = bs58::decode(&args[1]).into_vec().expect("base58 message");
            // The message must name this key as its one signer and fee payer
            assert_eq!(&message[..2], &[1, 0], "one signer, no read-only signer");
            assert_eq!(&message[4..36], &key.verifying_key().to_bytes(), "the payer is this wallet");
            let mut tx = vec![1u8];
            tx.extend_from_slice(&key.sign(&message).to_bytes());
            tx.extend_from_slice(&message);
            let text = base64::engine::general_purpose::STANDARD.encode(&tx);
            let id = rpc(&url, "sendTransaction", json!([text, {"encoding": "base64", "preflightCommitment": "confirmed"}])).await;
            println!("{}", id.as_str().expect("signature"));
        }
        _ => eprintln!("address | send <message> | airdrop <lamports>"),
    }
}
