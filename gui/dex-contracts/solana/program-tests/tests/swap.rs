//! On-chain integration tests for the Monzero XMZ<->Solana swap program, run in LiteSVM
//! against the compiled BPF `.so`. Proves the curve25519-syscall secret check accepts the
//! exact commitment produced host-side by dalek, and that the lock/claim/refund flows and
//! timelocks behave. No validator needed.
use curve25519_dalek::{edwards::EdwardsPoint, scalar::Scalar};
use litesvm::LiteSVM;
use solana_sdk::{
    account::ReadableAccount, clock::Clock, instruction::{AccountMeta, Instruction},
    pubkey::Pubkey, signature::{Keypair, Signer}, system_program, transaction::Transaction,
};

const SO: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../program/target/deploy/monzero_sol_swap.so");
const SOL: u64 = 1_000_000_000;

/// A deterministic (secret, commitment) pair: commitment = compress(secret*B), via dalek.
fn secret_pair(seed: u8) -> ([u8; 32], [u8; 32]) {
    let mut sb = [seed; 32]; sb[31] = 0; // keep it well inside the group order
    let s = Scalar::from_bytes_mod_order(sb);
    (s.to_bytes(), EdwardsPoint::mul_base(&s).compress().to_bytes())
}

struct Env { svm: LiteSVM, pid: Pubkey, owner: Keypair, claimer: Keypair }
fn setup() -> Env {
    let mut svm = LiteSVM::new();
    let pid = Pubkey::new_unique();
    svm.add_program_from_file(pid, SO).unwrap();
    let owner = Keypair::new();
    let claimer = Keypair::new();
    svm.airdrop(&owner.pubkey(), 100 * SOL).unwrap();
    svm.airdrop(&claimer.pubkey(), 10 * SOL).unwrap();
    Env { svm, pid, owner, claimer }
}

fn pda(e: &Env, cc: &[u8; 32], rc: &[u8; 32], amount: u64, nonce: u64) -> Pubkey {
    Pubkey::find_program_address(
        &[b"swap", e.owner.pubkey().as_ref(), e.claimer.pubkey().as_ref(), cc, rc,
          &amount.to_le_bytes(), &nonce.to_le_bytes()], &e.pid).0
}

fn send(e: &mut Env, ix: Instruction, signer: &Keypair) -> Result<u64, String> {
    let bh = e.svm.latest_blockhash();
    let tx = Transaction::new_signed_with_payer(&[ix], Some(&signer.pubkey()), &[signer], bh);
    match e.svm.send_transaction(tx) {
        Ok(m) => Ok(m.compute_units_consumed),
        Err(f) => { eprintln!("TX FAILED {:?}\n--logs--\n{}\n--------", f.err, f.meta.logs.join("\n")); Err(format!("{:?}", f.err)) }
    }
}

fn new_ix(e: &Env, cc: [u8; 32], rc: [u8; 32], d1: u64, d2: u64, amount: u64, nonce: u64, pda: Pubkey) -> Instruction {
    let mut data = vec![0u8];
    data.extend_from_slice(&cc); data.extend_from_slice(&rc);
    data.extend_from_slice(&d1.to_le_bytes()); data.extend_from_slice(&d2.to_le_bytes());
    data.extend_from_slice(&amount.to_le_bytes()); data.extend_from_slice(&nonce.to_le_bytes());
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true),
        AccountMeta::new_readonly(e.claimer.pubkey(), false),
        AccountMeta::new(pda, false),
        AccountMeta::new_readonly(system_program::id(), false)] }
}
fn set_ready_ix(e: &Env, pda: Pubkey) -> Instruction {
    Instruction { program_id: e.pid, data: vec![1u8], accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true), AccountMeta::new(pda, false)] }
}
fn claim_ix(e: &Env, secret: [u8; 32], pda: Pubkey) -> Instruction {
    let mut data = vec![2u8]; data.extend_from_slice(&secret);
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.claimer.pubkey(), true), AccountMeta::new(pda, false),
        AccountMeta::new(e.owner.pubkey(), false)] }
}
fn refund_ix(e: &Env, secret: [u8; 32], pda: Pubkey) -> Instruction {
    let mut data = vec![3u8]; data.extend_from_slice(&secret);
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true), AccountMeta::new(pda, false)] }
}
fn warp(e: &mut Env, secs: i64) {
    let mut c: Clock = e.svm.get_sysvar();
    c.unix_timestamp += secs;
    e.svm.set_sysvar(&c);
    // Force a new blockhash so a retried (otherwise byte-identical) tx is not rejected
    // as AlreadyProcessed.
    e.svm.expire_blockhash();
}

#[test]
fn happy_path_claim() {
    let mut e = setup();
    let (cs, cc) = secret_pair(7);   // claim secret/commitment
    let (_rs, rc) = secret_pair(9);  // refund commitment
    let (amount, nonce) = (2 * SOL, 1u64);
    let p = pda(&e, &cc, &rc, amount, nonce);
    let (o, c) = (e.owner.insecure_clone(), e.claimer.insecure_clone());

    let ix = new_ix(&e, cc, rc, 100, 100, amount, nonce, p);
    send(&mut e, ix, &o).unwrap();
    assert!(e.svm.get_balance(&p).unwrap() >= amount, "escrow funded");
    let ix = set_ready_ix(&e, p);
    send(&mut e, ix, &o).unwrap();

    let before = e.svm.get_balance(&c.pubkey()).unwrap();
    let ix = claim_ix(&e, cs, p);
    let cu = send(&mut e, ix, &c).unwrap();
    let after = e.svm.get_balance(&c.pubkey()).unwrap();
    // claimer gains `amount` minus the tx fee (5000 lamports/sig)
    assert!(after > before + amount - 10_000, "claimer paid: {} -> {}", before, after);
    assert!(e.svm.get_account(&p).map(|a| a.lamports() == 0).unwrap_or(true), "escrow closed");
    println!("CLAIM compute units = {cu}");
    assert!(cu < 200_000, "claim within default CU budget: {cu}");
}

#[test]
fn wrong_secret_rejected() {
    let mut e = setup();
    let (_cs, cc) = secret_pair(7);
    let (_rs, rc) = secret_pair(9);
    let (amount, nonce) = (SOL, 2);
    let p = pda(&e, &cc, &rc, amount, nonce);
    let (o, c) = (e.owner.insecure_clone(), e.claimer.insecure_clone());
    let ix = new_ix(&e, cc, rc, 100, 100, amount, nonce, p);
    send(&mut e, ix, &o).unwrap();
    let ix = set_ready_ix(&e, p);
    send(&mut e, ix, &o).unwrap();
    let (bad, _) = secret_pair(8); // not the claim secret
    let ix = claim_ix(&e, bad, p);
    assert!(send(&mut e, ix, &c).is_err(), "wrong secret must fail");
}

#[test]
fn claim_too_early_then_allowed_after_t1() {
    let mut e = setup();
    let (cs, cc) = secret_pair(7);
    let (_rs, rc) = secret_pair(9);
    let (amount, nonce) = (SOL, 3);
    let p = pda(&e, &cc, &rc, amount, nonce);
    let (o, c) = (e.owner.insecure_clone(), e.claimer.insecure_clone());
    let ix = new_ix(&e, cc, rc, 100, 100, amount, nonce, p);
    send(&mut e, ix, &o).unwrap();
    // no set_ready, before timeout1 -> too early
    let ix = claim_ix(&e, cs, p);
    assert!(send(&mut e, ix, &c).is_err(), "claim before t1 without ready fails");
    warp(&mut e, 150); // past timeout1, before timeout2
    let ix = claim_ix(&e, cs, p);
    assert!(send(&mut e, ix, &c).is_ok(), "claim allowed after t1");
}

#[test]
fn refund_after_timeout2() {
    let mut e = setup();
    let (_cs, cc) = secret_pair(7);
    let (rs, rc) = secret_pair(9);
    let (amount, nonce) = (3 * SOL, 4);
    let p = pda(&e, &cc, &rc, amount, nonce);
    let o = e.owner.insecure_clone();
    let ix = new_ix(&e, cc, rc, 100, 100, amount, nonce, p);
    send(&mut e, ix, &o).unwrap();
    // Once READY, the owner can no longer early-cancel; before timeout2 refund is rejected.
    let ix = set_ready_ix(&e, p);
    send(&mut e, ix, &o).unwrap();
    let ix = refund_ix(&e, rs, p);
    assert!(send(&mut e, ix, &o).is_err(), "refund while ready before t2 fails");
    warp(&mut e, 250); // past timeout2
    let before = e.svm.get_balance(&o.pubkey()).unwrap();
    let ix = refund_ix(&e, rs, p);
    send(&mut e, ix, &o).unwrap();
    let after = e.svm.get_balance(&o.pubkey()).unwrap();
    assert!(after > before, "owner refunded: {before} -> {after}");
    assert!(e.svm.get_account(&p).map(|a| a.lamports() == 0).unwrap_or(true), "escrow closed on refund");
}

#[test]
fn early_cancel_while_pending() {
    // Before set_ready, the owner can cancel immediately (reveals the refund secret) and
    // reclaim the lock — the safe "maker backed out before confirming" path.
    let mut e = setup();
    let (_cs, cc) = secret_pair(7);
    let (rs, rc) = secret_pair(9);
    let (amount, nonce) = (2 * SOL, 5);
    let p = pda(&e, &cc, &rc, amount, nonce);
    let o = e.owner.insecure_clone();
    let ix = new_ix(&e, cc, rc, 100, 100, amount, nonce, p);
    send(&mut e, ix, &o).unwrap();
    let before = e.svm.get_balance(&o.pubkey()).unwrap();
    let ix = refund_ix(&e, rs, p); // no set_ready, still Pending, before t1
    send(&mut e, ix, &o).unwrap();
    let after = e.svm.get_balance(&o.pubkey()).unwrap();
    assert!(after > before, "owner reclaimed on early cancel: {before} -> {after}");
    // A wrong refund secret must still be rejected even on the early path.
    let (amount2, nonce2) = (SOL, 6);
    let p2 = pda(&e, &cc, &rc, amount2, nonce2);
    let ix = new_ix(&e, cc, rc, 100, 100, amount2, nonce2, p2);
    send(&mut e, ix, &o).unwrap();
    let (bad, _) = secret_pair(3);
    let ix = refund_ix(&e, bad, p2);
    assert!(send(&mut e, ix, &o).is_err(), "wrong refund secret rejected");
}
