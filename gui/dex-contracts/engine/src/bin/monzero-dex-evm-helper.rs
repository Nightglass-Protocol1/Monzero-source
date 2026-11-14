//! `monzero-dex-evm-helper`: one JSON request on stdin, one JSON response on stdout (see
//! `monzero_dex_evm::helper`). Exit status 0 when the response says ok.

use std::io::Read;

#[tokio::main]
async fn main() {
    const LIMIT: u64 = 1 << 20;
    // A crash still answers: where it happened (file and line only, never the data in hand),
    // so the app can show something better than "no valid answer"
    std::panic::set_hook(Box::new(|info| {
        let place = info.location().map(|l| format!("{}:{}", l.file().rsplit('/').next().unwrap_or(""), l.line())).unwrap_or_default();
        println!("{{\"schema\":\"{}\",\"ok\":false,\"error\":\"the trade helper stopped unexpectedly ({place})\"}}", monzero_dex_evm::helper::RESPONSE_SCHEMA);
        std::process::exit(1);
    }));
    let mut request = Vec::new();
    if std::io::stdin().take(LIMIT + 1).read_to_end(&mut request).is_err() || request.len() as u64 > LIMIT {
        println!("{{\"schema\":\"{}\",\"ok\":false,\"error\":\"request missing or too large\"}}", monzero_dex_evm::helper::RESPONSE_SCHEMA);
        std::process::exit(1);
    }
    let response = monzero_dex_evm::helper::handle(&request).await;
    println!("{response}");
    std::process::exit(if response["ok"] == true { 0 } else { 1 });
}
