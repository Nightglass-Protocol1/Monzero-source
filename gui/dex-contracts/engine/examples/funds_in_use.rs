//! Says whether a trades folder still needs its trade account's coin (the check behind "send it
//! back to MetaMask"): `cargo run --example funds_in_use -- <trades folder>`
fn main() {
    let root = std::env::args().nth(1).expect("trades folder");
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs();
    match monzero_dex_evm::helper_market::funds_in_use(std::path::Path::new(&root), now) {
        Ok(None) => println!("free: nothing needs the coin"),
        Ok(Some(why)) => println!("in use: {why}"),
        Err(e) => println!("in use (unreadable): {e}"),
    }
}
