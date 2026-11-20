//! Direct trades between an EVM chain and Solana, both ways: the one secret is revealed on one
//! curve (secp256k1 on the EVM contract, ed25519 on the Solana program) and used on the other,
//! which is what the leader's cross-curve proof guarantees.
//!
//!   (cd dex-contracts && npx hardhat compile && npx hardhat node)
//!   solana-test-validator --reset --bpf-program <PROGRAM_ID> monzero_sol_swap.so
//!   MONZERO_EVM_RPC=http://127.0.0.1:8545 MONZERO_SOL_RPC=http://127.0.0.1:8899 \
//!     MONZERO_SOL_PROGRAM=<PROGRAM_ID> cargo test --test direct_sol_local -- --nocapture --test-threads 1
//!
//! Skipped (passes trivially) unless all three are set.

use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{keccak256, Address, Bytes, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::rpc::types::TransactionRequest;
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::chain::AlloyChain;
use monzero_dex_evm::direct::{DirectOffer, DirectSession, DirectTiming, Leg, SavedDirect, Side};
use monzero_dex_evm::direct_driver::{DirectDriver, DirectFileJournal, DirectRole};
use monzero_dex_evm::sol::{self, AnyChain, SolChain};
use monzero_dex_evm::swap_seed::SwapSeed;
use monzero_dex_evm::Chain;
use serde_json::{json, Value};
use std::str::FromStr;

const LEADER_KEY: &str = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
const FOLLOWER_KEY: &str = "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d";
const WEI: u64 = 10u64.pow(17); // 0.1 "ETH"
const LAMPORTS: u64 = 50_000_000; // 0.05 SOL

fn signer(key: &str) -> PrivateKeySigner {
    key.parse().unwrap()
}

struct Env {
    evm: String,
    sol: String,
    program: sol::Pubkey,
}

fn env() -> Option<Env> {
    Some(Env { evm: std::env::var("MONZERO_EVM_RPC").ok()?, sol: std::env::var("MONZERO_SOL_RPC").ok()?, program: sol::from_base58(&std::env::var("MONZERO_SOL_PROGRAM").ok()?).unwrap() })
}

async fn deploy(url: &str) -> Address {
    let artifact: Value = serde_json::from_str(&std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../artifacts/contracts/XmzSwap.sol/XmzSwap.json")).unwrap()).unwrap();
    let code = Bytes::from_str(artifact["bytecode"].as_str().unwrap()).unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer(LEADER_KEY))).connect_http(url.parse().unwrap());
    provider.send_transaction(TransactionRequest::default().with_deploy_code(code)).await.unwrap().get_receipt().await.unwrap().contract_address.unwrap()
}

async fn sol_rpc(url: &str, method: &str, params: Value) -> Value {
    let r: Value = reqwest::Client::new().post(url).json(&json!({"jsonrpc": "2.0", "id": 1, "method": method, "params": params})).send().await.unwrap().json().await.unwrap();
    r["result"].clone()
}

async fn sol_balance(url: &str, account: &sol::Pubkey) -> u64 {
    sol_rpc(url, "getBalance", json!([sol::to_base58(account), {"commitment": "confirmed"}])).await["value"].as_u64().unwrap()
}

async fn airdrop(url: &str, account: &sol::Pubkey, lamports: u64) {
    sol_rpc(url, "requestAirdrop", json!([sol::to_base58(account), lamports])).await;
    for _ in 0..60 {
        if sol_balance(url, account).await >= lamports {
            return;
        }
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
    }
    panic!("the airdrop did not arrive");
}

fn sol_account(key: &PrivateKeySigner) -> sol::Pubkey {
    sol::account_key(&key.to_bytes().0).verifying_key().to_bytes()
}

struct Party {
    seed: SwapSeed,
    key: PrivateKeySigner,
    saved: SavedDirect,
    journal: std::path::PathBuf,
}

impl Party {
    fn leg(&self, e: &Env, leg_a: bool) -> AnyChain {
        let agreed = self.saved.agreed(&self.seed, &self.key).unwrap();
        let (terms, binding, nonce, from) = if leg_a {
            (agreed.leg_a, agreed.sol_a, self.saved.nonce_a(), self.saved.from_block_a)
        } else {
            (agreed.leg_b, agreed.sol_b, self.saved.nonce_b(), self.saved.from_block_b)
        };
        if terms.chain.is_solana() {
            AnyChain::Sol(SolChain::with_asset(&e.sol, terms.chain, e.program, sol::account_key(&self.key.to_bytes().0), binding.unwrap(), nonce, terms.value, terms.asset).unwrap())
        } else {
            AnyChain::Evm(AlloyChain::new(&e.evm, terms.chain, terms.contract, from, self.key.clone()).unwrap())
        }
    }

    fn driver(&self, e: &Env) -> DirectDriver<AnyChain, AnyChain, DirectFileJournal> {
        DirectDriver::resume(&self.seed, &self.key, self.leg(e, true), self.leg(e, false), DirectFileJournal(self.journal.clone())).unwrap()
    }
}

async fn trade(e: &Env, evm_first: bool, tag: &str) -> (DirectRole, DirectRole) {
    sol::set_local_program(e.program);
    let contract = deploy(&e.evm).await;
    let (lk, fk) = (signer(LEADER_KEY), signer(FOLLOWER_KEY));
    for k in [&lk, &fk] {
        airdrop(&e.sol, &sol_account(k), 1_000_000_000).await;
    }
    let evm = Leg { chain_id: Chain::Local.id(), contract, asset: Address::ZERO, value: U256::from(WEI), duration1: 0, duration2: 0 };
    let solana = Leg { chain_id: Chain::SolLocal.id(), contract: sol::contract_address(&e.program), asset: Address::ZERO, value: U256::from(LAMPORTS), duration1: 0, duration2: 0 };
    let (mut a, mut b) = if evm_first { (evm, solana) } else { (solana, evm) };
    (a.duration1, a.duration2, b.duration1, b.duration2) = (3600, 8 * 3600, 3600, 4 * 3600);
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs();
    let offer = DirectOffer { trade_id: keccak256(format!("{tag}-{now}")), leader: lk.address(), follower: fk.address(), leg_a: a, leg_b: b, expires_at: now + 600 };
    let (ls, fs) = (SwapSeed::from_wallet_seed(format!("{tag} leader").as_bytes()), SwapSeed::from_wallet_seed(format!("{tag} follower").as_bytes()));
    let mut lsess = DirectSession::start(&ls, offer.clone(), Side::Leader, &lk, now).unwrap();
    let mut fsess = DirectSession::start(&fs, offer.clone(), Side::Follower, &fk, now).unwrap();
    fsess.accept_theirs(&lsess.frame().clone(), now).unwrap();
    lsess.accept_theirs(&fsess.frame().clone(), now).unwrap();
    let provider = ProviderBuilder::new().connect_http(e.evm.parse().unwrap());
    let head = provider.get_block_number().await.unwrap();
    // The EVM leg's search starts at its head; Solana reads the swap account itself. The lock
    // deadline runs on leg A's clock (a local node's clock may have been moved on by other tests)
    let evm_now = provider.get_block_by_number(head.into()).await.unwrap().unwrap().header.timestamp;
    let deadline = if evm_first { evm_now } else { now } + 2 * 3600;
    let dir = std::env::temp_dir().join(format!("monzero-direct-sol-{tag}-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let mk = |seed: SwapSeed, key: PrivateKeySigner, sess: &DirectSession, name: &str| Party {
        saved: sess.saved(DirectTiming::DEFAULT, deadline, head, head).unwrap(),
        seed,
        key,
        journal: dir.join(format!("{name}.json")),
    };
    let (leader, follower) = (mk(ls, lk, &lsess, "leader"), mk(fs, fk, &fsess, "follower"));
    for p in [&leader, &follower] {
        let mut d = DirectDriver::start(p.saved.clone(), &p.seed, &p.key, p.leg(e, true), p.leg(e, false), DirectFileJournal(p.journal.clone())).unwrap();
        d.approve(p.saved.transcript).unwrap();
    }
    let (mut l, mut f) = (None, None);
    for round in 0..120 {
        // Rebuilt from the journal every round, as the app does
        let lt = leader.driver(e).tick().await;
        let ft = follower.driver(e).tick().await;
        let (ls_, fs_) = (lt.unwrap(), ft.unwrap());
        println!("{tag} round {round}: leader {} | follower {}", ls_.stage(), fs_.stage());
        let done = ls_.is_done() && fs_.is_done();
        (l, f) = (Some(ls_), Some(fs_));
        if done {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    let _ = std::fs::remove_dir_all(&dir);
    (l.unwrap(), f.unwrap())
}

#[tokio::test]
async fn eth_for_sol_the_leader_reveals_on_solana_and_the_follower_claims_on_the_evm_chain() {
    let Some(e) = env() else { return };
    let follower_sol = sol_account(&signer(FOLLOWER_KEY));
    let leader_sol = sol_account(&signer(LEADER_KEY));
    let (lb, fb) = (sol_balance(&e.sol, &leader_sol).await, sol_balance(&e.sol, &follower_sol).await);
    let (l, f) = trade(&e, true, "eth-for-sol").await;
    assert_eq!(l.stage(), "Done(Swapped)");
    assert_eq!(f.stage(), "Done(Swapped)");
    // The leader received the SOL (airdrops of 1 SOL each came in during the trade's setup)
    let (la, fa) = (sol_balance(&e.sol, &leader_sol).await, sol_balance(&e.sol, &follower_sol).await);
    assert!(la >= lb + 1_000_000_000 + LAMPORTS - 10 * sol::FEE_LAMPORTS, "leader {lb} -> {la}");
    assert!(fa <= fb + 1_000_000_000 - LAMPORTS, "follower {fb} -> {fa}");
}

#[tokio::test]
async fn sol_for_eth_the_leader_reveals_on_the_evm_chain_and_the_follower_claims_on_solana() {
    let Some(e) = env() else { return };
    let (l, f) = trade(&e, false, "sol-for-eth").await;
    assert_eq!(l.stage(), "Done(Swapped)");
    assert_eq!(f.stage(), "Done(Swapped)");
}
