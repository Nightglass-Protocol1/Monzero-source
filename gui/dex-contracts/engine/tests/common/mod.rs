//! A local XMZ chain for the integration tests: `monzerod --regtest` (fakechain) and
//! `monzero-wallet-rpc`s with their digest logins, all killed on drop.
#![allow(dead_code)]

use monzero_dex_evm::xmz::WalletRpc;
use serde_json::{json, Value};
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::time::Duration;

pub const GENESIS: &str = "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4";

pub struct XmzChain {
    bin: PathBuf,
    pub root: PathBuf,
    daemon_port: u16,
    pub daemon: WalletRpc,
    procs: Vec<Child>,
}

impl Drop for XmzChain {
    fn drop(&mut self) {
        for child in &mut self.procs {
            let _ = child.kill();
            let _ = child.wait();
        }
        let _ = std::fs::remove_dir_all(&self.root);
    }
}

fn free_port() -> u16 {
    std::net::TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port()
}

pub async fn wait_for(rpc: &WalletRpc, method: &str) -> Value {
    for _ in 0..300 {
        if let Ok(v) = rpc.call(method, json!({})).await {
            return v;
        }
        tokio::time::sleep(Duration::from_millis(200)).await;
    }
    panic!("{method} never answered");
}

impl XmzChain {
    /// None when MONZERO_XMZ_BIN (the directory with monzerod and monzero-wallet-rpc) is unset.
    pub async fn start(name: &str) -> Option<XmzChain> {
        let bin = PathBuf::from(std::env::var("MONZERO_XMZ_BIN").ok()?);
        let root = std::env::temp_dir().join(format!("monzero-xmz-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(&root).unwrap();
        let (daemon_port, p2p) = (free_port(), free_port());
        let node = Command::new(bin.join("monzerod"))
            .args(["--regtest", "--keep-fakechain", "--fixed-difficulty", "1", "--offline", "--no-igd", "--hide-my-port"])
            .args(["--no-zmq", "--non-interactive", "--data-dir", root.join("chain").to_str().unwrap()])
            .args(["--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", &daemon_port.to_string()])
            .args(["--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", &p2p.to_string()])
            .args(["--log-file", root.join("node.log").to_str().unwrap()])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let daemon = WalletRpc::new(&format!("http://127.0.0.1:{daemon_port}"), None);
        let chain = XmzChain { bin, root, daemon_port, daemon, procs: vec![node] };
        let info = wait_for(&chain.daemon, "get_info").await;
        assert_eq!(info["nettype"], "fakechain");
        let genesis = chain.daemon.call("get_block", json!({"height": 0})).await.unwrap();
        assert_eq!(genesis["block_header"]["hash"], GENESIS);
        Some(chain)
    }

    pub async fn wallet_rpc(&mut self, name: &str) -> WalletRpc {
        let port = free_port();
        let dir = self.root.join(name);
        std::fs::create_dir_all(&dir).unwrap();
        self.procs.push(
            Command::new(self.bin.join("monzero-wallet-rpc"))
                .args(["--wallet-dir", dir.to_str().unwrap(), "--daemon-address", &format!("127.0.0.1:{}", self.daemon_port)])
                .args(["--trusted-daemon", "--allow-mismatched-daemon-version", "--rpc-bind-ip", "127.0.0.1"])
                .args(["--rpc-bind-port", &port.to_string(), "--non-interactive"])
                .args(["--log-file", self.root.join(format!("{name}.log")).to_str().unwrap()])
                .current_dir(&self.root)
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .unwrap(),
        );
        // The RPC writes its digest login next to it on start
        let login = self.root.join(format!("monzero-wallet-rpc.{port}.login"));
        for _ in 0..300 {
            if login.exists() {
                break;
            }
            tokio::time::sleep(Duration::from_millis(200)).await;
        }
        let rpc = WalletRpc::with_login_file(&format!("http://127.0.0.1:{port}"), &login).unwrap();
        wait_for(&rpc, "get_version").await;
        rpc
    }

    /// A wallet RPC with a new, open wallet; returns it and the wallet's address.
    pub async fn trader(&mut self, name: &str) -> (WalletRpc, String) {
        let rpc = self.wallet_rpc(name).await;
        rpc.call("create_wallet", json!({"filename": name, "password": "", "language": "English"})).await.unwrap();
        let address = rpc.call("get_address", json!({})).await.unwrap()["address"].as_str().unwrap().to_string();
        (rpc, address)
    }

    pub async fn mine(&self, to: &str, blocks: u64) {
        self.daemon.call("generateblocks", json!({"wallet_address": to, "amount_of_blocks": blocks})).await.unwrap();
    }

    pub async fn height(&self) -> u64 {
        self.daemon.call("get_info", json!({})).await.unwrap()["height"].as_u64().unwrap()
    }
}
