//! On-chain SPL-token (USDC/USDT-style) swap tests in LiteSVM: mint + token accounts are
//! set up by packing state directly; the program creates the PDA vault and moves tokens via
//! token-program CPIs. Proves the SPL lock/claim/refund flow and that the same ed25519
//! syscall secret check gates it.
use curve25519_dalek::{edwards::EdwardsPoint, scalar::Scalar};
use litesvm::LiteSVM;
use solana_program::program_option::COption;
use solana_sdk::{
    account::Account, clock::Clock, compute_budget::ComputeBudgetInstruction,
    instruction::{AccountMeta, Instruction}, program_pack::Pack, pubkey::Pubkey,
    signature::{Keypair, Signer}, system_program, transaction::Transaction,
};
use spl_token::state::{Account as TokenAccount, AccountState, Mint};

const SO: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../program/target/deploy/monzero_sol_swap.so");
const SOL: u64 = 1_000_000_000;

fn secret_pair(seed: u8) -> ([u8; 32], [u8; 32]) {
    let mut sb = [seed; 32]; sb[31] = 0;
    let s = Scalar::from_bytes_mod_order(sb);
    (s.to_bytes(), EdwardsPoint::mul_base(&s).compress().to_bytes())
}

struct Env { svm: LiteSVM, pid: Pubkey, owner: Keypair, claimer: Keypair, mint: Pubkey }

fn make_mint(svm: &mut LiteSVM, decimals: u8) -> Pubkey {
    let key = Pubkey::new_unique();
    let m = Mint { mint_authority: COption::None, supply: 0, decimals, is_initialized: true, freeze_authority: COption::None };
    let mut data = vec![0u8; Mint::LEN];
    Mint::pack(m, &mut data).unwrap();
    let lamports = svm.minimum_balance_for_rent_exemption(Mint::LEN);
    svm.set_account(key, Account { lamports, data, owner: spl_token::id(), executable: false, rent_epoch: 0 }).unwrap();
    key
}
fn make_token_acc(svm: &mut LiteSVM, mint: Pubkey, owner: Pubkey, amount: u64) -> Pubkey {
    let key = Pubkey::new_unique();
    let a = TokenAccount { mint, owner, amount, delegate: COption::None, state: AccountState::Initialized,
        is_native: COption::None, delegated_amount: 0, close_authority: COption::None };
    let mut data = vec![0u8; TokenAccount::LEN];
    TokenAccount::pack(a, &mut data).unwrap();
    let lamports = svm.minimum_balance_for_rent_exemption(TokenAccount::LEN);
    svm.set_account(key, Account { lamports, data, owner: spl_token::id(), executable: false, rent_epoch: 0 }).unwrap();
    key
}
fn token_balance(svm: &LiteSVM, key: &Pubkey) -> u64 {
    TokenAccount::unpack(&svm.get_account(key).unwrap().data).unwrap().amount
}

fn setup() -> Env {
    let mut svm = LiteSVM::new();
    let pid = Pubkey::new_unique();
    svm.add_program_from_file(pid, SO).unwrap();
    let owner = Keypair::new();
    let claimer = Keypair::new();
    svm.airdrop(&owner.pubkey(), 100 * SOL).unwrap();
    svm.airdrop(&claimer.pubkey(), 10 * SOL).unwrap();
    let mint = make_mint(&mut svm, 6);
    Env { svm, pid, owner, claimer, mint }
}

fn swap_pda(e: &Env, cc: &[u8; 32], rc: &[u8; 32], amount: u64, nonce: u64) -> Pubkey {
    Pubkey::find_program_address(&[b"swap", e.owner.pubkey().as_ref(), e.claimer.pubkey().as_ref(),
        cc, rc, &amount.to_le_bytes(), &nonce.to_le_bytes()], &e.pid).0
}
fn vault_pda(e: &Env, swap: &Pubkey) -> Pubkey {
    Pubkey::find_program_address(&[b"vault", swap.as_ref()], &e.pid).0
}

fn send(e: &mut Env, ixs: Vec<Instruction>, signer: &Keypair) -> Result<u64, String> {
    let bh = e.svm.latest_blockhash();
    let mut all = vec![ComputeBudgetInstruction::set_compute_unit_limit(400_000)];
    all.extend(ixs);
    let tx = Transaction::new_signed_with_payer(&all, Some(&signer.pubkey()), &[signer], bh);
    match e.svm.send_transaction(tx) {
        Ok(m) => Ok(m.compute_units_consumed),
        Err(f) => { eprintln!("TX FAILED {:?}\n{}", f.err, f.meta.logs.join("\n")); Err(format!("{:?}", f.err)) }
    }
}
fn warp(e: &mut Env, secs: i64) {
    let mut c: Clock = e.svm.get_sysvar(); c.unix_timestamp += secs; e.svm.set_sysvar(&c); e.svm.expire_blockhash();
}

fn new_spl_ix(e: &Env, cc: [u8; 32], rc: [u8; 32], amount: u64, nonce: u64, owner_tok: Pubkey, swap: Pubkey, vault: Pubkey) -> Instruction {
    let mut data = vec![4u8];
    data.extend_from_slice(&cc); data.extend_from_slice(&rc);
    data.extend_from_slice(&100u64.to_le_bytes()); data.extend_from_slice(&100u64.to_le_bytes());
    data.extend_from_slice(&amount.to_le_bytes()); data.extend_from_slice(&nonce.to_le_bytes());
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true),
        AccountMeta::new_readonly(e.claimer.pubkey(), false),
        AccountMeta::new(swap, false),
        AccountMeta::new_readonly(e.mint, false),
        AccountMeta::new(owner_tok, false),
        AccountMeta::new(vault, false),
        AccountMeta::new_readonly(spl_token::id(), false),
        AccountMeta::new_readonly(system_program::id(), false)] }
}
fn set_ready_ix(e: &Env, swap: Pubkey) -> Instruction {
    Instruction { program_id: e.pid, data: vec![1u8], accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true), AccountMeta::new(swap, false)] }
}
fn claim_spl_ix(e: &Env, secret: [u8; 32], swap: Pubkey, claimer_tok: Pubkey, vault: Pubkey) -> Instruction {
    let mut data = vec![5u8]; data.extend_from_slice(&secret);
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.claimer.pubkey(), true),
        AccountMeta::new(swap, false),
        AccountMeta::new(claimer_tok, false),
        AccountMeta::new(vault, false),
        AccountMeta::new(e.owner.pubkey(), false),
        AccountMeta::new_readonly(e.mint, false),
        AccountMeta::new_readonly(spl_token::id(), false)] }
}
fn refund_spl_ix(e: &Env, secret: [u8; 32], swap: Pubkey, owner_tok: Pubkey, vault: Pubkey) -> Instruction {
    let mut data = vec![6u8]; data.extend_from_slice(&secret);
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true),
        AccountMeta::new(swap, false),
        AccountMeta::new(owner_tok, false),
        AccountMeta::new(vault, false),
        AccountMeta::new_readonly(e.mint, false),
        AccountMeta::new_readonly(spl_token::id(), false)] }
}

#[test]
fn spl_happy_path() {
    let mut e = setup();
    let (cs, cc) = secret_pair(7);
    let (_rs, rc) = secret_pair(9);
    let (amount, nonce) = (500_000_000u64, 1u64); // 500 USDC @ 6dp
    let p = swap_pda(&e, &cc, &rc, amount, nonce);
    let v = vault_pda(&e, &p);
    let owner_tok = make_token_acc(&mut e.svm, e.mint, e.owner.pubkey(), 1_000_000_000);
    let claimer_tok = make_token_acc(&mut e.svm, e.mint, e.claimer.pubkey(), 0);
    let (o, c) = (e.owner.insecure_clone(), e.claimer.insecure_clone());

    let ix = new_spl_ix(&e, cc, rc, amount, nonce, owner_tok, p, v);
    send(&mut e, vec![ix], &o).unwrap();
    assert_eq!(token_balance(&e.svm, &owner_tok), 500_000_000, "owner debited");
    assert_eq!(token_balance(&e.svm, &v), amount, "vault holds the tokens");

    let ix = set_ready_ix(&e, p);
    send(&mut e, vec![ix], &o).unwrap();
    let ix = claim_spl_ix(&e, cs, p, claimer_tok, v);
    let cu = send(&mut e, vec![ix], &c).unwrap();
    assert_eq!(token_balance(&e.svm, &claimer_tok), amount, "claimer received the tokens");
    assert!(e.svm.get_account(&v).map(|a| a.lamports == 0).unwrap_or(true), "vault closed");
    assert!(e.svm.get_account(&p).map(|a| a.lamports == 0).unwrap_or(true), "state closed");
    println!("SPL CLAIM compute units = {cu}");
}

#[test]
fn spl_wrong_secret_rejected() {
    let mut e = setup();
    let (_cs, cc) = secret_pair(7);
    let (_rs, rc) = secret_pair(9);
    let (amount, nonce) = (100_000_000u64, 2);
    let p = swap_pda(&e, &cc, &rc, amount, nonce);
    let v = vault_pda(&e, &p);
    let owner_tok = make_token_acc(&mut e.svm, e.mint, e.owner.pubkey(), 200_000_000);
    let claimer_tok = make_token_acc(&mut e.svm, e.mint, e.claimer.pubkey(), 0);
    let (o, c) = (e.owner.insecure_clone(), e.claimer.insecure_clone());
    let ix = new_spl_ix(&e, cc, rc, amount, nonce, owner_tok, p, v);
    send(&mut e, vec![ix], &o).unwrap();
    let ix = set_ready_ix(&e, p);
    send(&mut e, vec![ix], &o).unwrap();
    let (bad, _) = secret_pair(8);
    let ix = claim_spl_ix(&e, bad, p, claimer_tok, v);
    assert!(send(&mut e, vec![ix], &c).is_err(), "wrong secret must fail");
}

#[test]
fn spl_refund_after_timeout2() {
    let mut e = setup();
    let (_cs, cc) = secret_pair(7);
    let (rs, rc) = secret_pair(9);
    let (amount, nonce) = (300_000_000u64, 3);
    let p = swap_pda(&e, &cc, &rc, amount, nonce);
    let v = vault_pda(&e, &p);
    let owner_tok = make_token_acc(&mut e.svm, e.mint, e.owner.pubkey(), 300_000_000);
    let o = e.owner.insecure_clone();
    let ix = new_spl_ix(&e, cc, rc, amount, nonce, owner_tok, p, v);
    send(&mut e, vec![ix], &o).unwrap();
    assert_eq!(token_balance(&e.svm, &owner_tok), 0, "owner fully locked");
    let ix = set_ready_ix(&e, p);
    send(&mut e, vec![ix], &o).unwrap();
    let ix = refund_spl_ix(&e, rs, p, owner_tok, v);
    assert!(send(&mut e, vec![ix], &o).is_err(), "refund while ready before t2 fails");
    warp(&mut e, 250);
    let ix = refund_spl_ix(&e, rs, p, owner_tok, v);
    send(&mut e, vec![ix], &o).unwrap();
    assert_eq!(token_balance(&e.svm, &owner_tok), amount, "owner refunded the tokens");
    assert!(e.svm.get_account(&v).map(|a| a.lamports == 0).unwrap_or(true), "vault closed on refund");
}
