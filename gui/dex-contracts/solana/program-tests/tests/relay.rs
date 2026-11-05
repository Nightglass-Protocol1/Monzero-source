//! Gasless relayed-claim tests (LiteSVM). The claimer signs an authorization over
//! (prefix, swap_pda, fee, relayer) as an Ed25519 instruction; a separate relayer is the
//! tx fee payer + signer and submits [ed25519, claim_relayed]. The program introspects the
//! Ed25519 instruction and pays claimer (amount-fee) + relayer (fee).
use curve25519_dalek::{edwards::EdwardsPoint, scalar::Scalar};
use litesvm::LiteSVM;
use solana_sdk::{
    instruction::{AccountMeta, Instruction}, pubkey::Pubkey,
    signature::{Keypair, Signer}, system_program, transaction::Transaction,
};

const SO: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../program/target/deploy/monzero_sol_swap.so");
const SOL: u64 = 1_000_000_000;
const PREFIX: &[u8] = b"monzero-sol-relay-v1";

fn secret_pair(seed: u8) -> ([u8; 32], [u8; 32]) {
    let mut sb = [seed; 32]; sb[31] = 0;
    let s = Scalar::from_bytes_mod_order(sb);
    (s.to_bytes(), EdwardsPoint::mul_base(&s).compress().to_bytes())
}

struct Env { svm: LiteSVM, pid: Pubkey, owner: Keypair, claimer: Keypair, relayer: Keypair }
fn setup() -> Env {
    let mut svm = LiteSVM::new();
    let pid = Pubkey::new_unique();
    svm.add_program_from_file(pid, SO).unwrap();
    let (owner, claimer, relayer) = (Keypair::new(), Keypair::new(), Keypair::new());
    svm.airdrop(&owner.pubkey(), 100 * SOL).unwrap();
    svm.airdrop(&relayer.pubkey(), 10 * SOL).unwrap();
    // claimer intentionally has NO SOL — the whole point of a gasless claim.
    Env { svm, pid, owner, claimer, relayer }
}
fn swap_pda(e: &Env, cc: &[u8; 32], rc: &[u8; 32], amount: u64, nonce: u64) -> Pubkey {
    Pubkey::find_program_address(&[b"swap", e.owner.pubkey().as_ref(), e.claimer.pubkey().as_ref(),
        cc, rc, &amount.to_le_bytes(), &nonce.to_le_bytes()], &e.pid).0
}
fn new_ix(e: &Env, cc: [u8; 32], rc: [u8; 32], amount: u64, nonce: u64, p: Pubkey) -> Instruction {
    let mut data = vec![0u8];
    data.extend_from_slice(&cc); data.extend_from_slice(&rc);
    data.extend_from_slice(&100u64.to_le_bytes()); data.extend_from_slice(&100u64.to_le_bytes());
    data.extend_from_slice(&amount.to_le_bytes()); data.extend_from_slice(&nonce.to_le_bytes());
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true),
        AccountMeta::new_readonly(e.claimer.pubkey(), false),
        AccountMeta::new(p, false),
        AccountMeta::new_readonly(system_program::id(), false)] }
}
fn set_ready_ix(e: &Env, p: Pubkey) -> Instruction {
    Instruction { program_id: e.pid, data: vec![1u8], accounts: vec![
        AccountMeta::new(e.owner.pubkey(), true), AccountMeta::new(p, false)] }
}

fn relay_message(swap: &Pubkey, fee: u64, relayer: &Pubkey) -> Vec<u8> {
    let mut m = PREFIX.to_vec();
    m.extend_from_slice(swap.as_ref());
    m.extend_from_slice(&fee.to_le_bytes());
    m.extend_from_slice(relayer.as_ref());
    m
}
/// Build a self-contained Ed25519 program instruction (sig at 16, pubkey at 80, msg at 112).
fn ed25519_ix(pubkey: &[u8; 32], signature: &[u8; 64], message: &[u8]) -> Instruction {
    let mut d = vec![1u8, 0u8];
    let (sig_off, pk_off, msg_off): (u16, u16, u16) = (16, 80, 112);
    d.extend_from_slice(&sig_off.to_le_bytes()); d.extend_from_slice(&u16::MAX.to_le_bytes());
    d.extend_from_slice(&pk_off.to_le_bytes()); d.extend_from_slice(&u16::MAX.to_le_bytes());
    d.extend_from_slice(&msg_off.to_le_bytes()); d.extend_from_slice(&(message.len() as u16).to_le_bytes());
    d.extend_from_slice(&u16::MAX.to_le_bytes());
    d.extend_from_slice(signature); d.extend_from_slice(pubkey); d.extend_from_slice(message);
    Instruction { program_id: solana_sdk::ed25519_program::id(), accounts: vec![], data: d }
}
fn claim_relayed_ix(e: &Env, secret: [u8; 32], fee: u64, p: Pubkey) -> Instruction {
    let mut data = vec![7u8]; data.extend_from_slice(&secret); data.extend_from_slice(&fee.to_le_bytes());
    Instruction { program_id: e.pid, data, accounts: vec![
        AccountMeta::new(e.relayer.pubkey(), true),
        AccountMeta::new(p, false),
        AccountMeta::new(e.claimer.pubkey(), false),
        AccountMeta::new(e.owner.pubkey(), false),
        AccountMeta::new_readonly(solana_sdk::sysvar::instructions::id(), false)] }
}

/// Claimer signs the authorization; relayer submits [ed25519, claim_relayed].
fn submit_relayed(e: &mut Env, secret: [u8; 32], signed_fee: u64, submit_fee: u64, p: Pubkey) -> Result<(), String> {
    let msg = relay_message(&p, signed_fee, &e.relayer.pubkey());
    let sig = e.claimer.sign_message(&msg);
    let sig64: [u8; 64] = sig.as_ref().try_into().unwrap();
    let pk: [u8; 32] = e.claimer.pubkey().to_bytes();
    let ed = ed25519_ix(&pk, &sig64, &msg);
    let claim = claim_relayed_ix(e, secret, submit_fee, p);
    let bh = e.svm.latest_blockhash();
    let tx = Transaction::new_signed_with_payer(&[ed, claim], Some(&e.relayer.pubkey()), &[&e.relayer], bh);
    e.svm.send_transaction(tx).map(|_| ()).map_err(|f| format!("{:?}", f.err))
}

#[test]
fn relayed_claim_pays_claimer_and_relayer() {
    let mut e = setup();
    let (cs, cc) = secret_pair(7);
    let (_rs, rc) = secret_pair(9);
    let (amount, nonce, fee) = (3 * SOL, 1u64, 20_000u64);
    let p = swap_pda(&e, &cc, &rc, amount, nonce);
    let o = e.owner.insecure_clone();
    { let ix = new_ix(&e, cc, rc, amount, nonce, p); let bh = e.svm.latest_blockhash();
      e.svm.send_transaction(Transaction::new_signed_with_payer(&[ix], Some(&o.pubkey()), &[&o], bh)).unwrap(); }
    { let ix = set_ready_ix(&e, p); let bh = e.svm.latest_blockhash();
      e.svm.send_transaction(Transaction::new_signed_with_payer(&[ix], Some(&o.pubkey()), &[&o], bh)).unwrap(); }

    let claimer_before = e.svm.get_balance(&e.claimer.pubkey()).unwrap_or(0); // no account yet
    let relayer_before = e.svm.get_balance(&e.relayer.pubkey()).unwrap();
    submit_relayed(&mut e, cs, fee, fee, p).unwrap();
    let claimer_after = e.svm.get_balance(&e.claimer.pubkey()).unwrap_or(0);
    let relayer_after = e.svm.get_balance(&e.relayer.pubkey()).unwrap();
    assert_eq!(claimer_after - claimer_before, amount - fee, "claimer got amount - fee");
    // relayer gains fee, pays the 5000-lamport tx fee
    assert!(relayer_after > relayer_before, "relayer net positive: {relayer_before} -> {relayer_after}");
    assert!(relayer_after >= relayer_before + fee - 10_000);
    assert!(e.svm.get_account(&p).map(|a| a.lamports == 0).unwrap_or(true), "swap closed");
}

#[test]
fn relayed_rejects_fee_tampering() {
    // Claimer signs fee=20_000 but the relayer submits a claim with fee=2_000_000_000.
    let mut e = setup();
    let (cs, cc) = secret_pair(7);
    let (_rs, rc) = secret_pair(9);
    let (amount, nonce) = (3 * SOL, 2u64);
    let p = swap_pda(&e, &cc, &rc, amount, nonce);
    let o = e.owner.insecure_clone();
    { let ix = new_ix(&e, cc, rc, amount, nonce, p); let bh = e.svm.latest_blockhash();
      e.svm.send_transaction(Transaction::new_signed_with_payer(&[ix], Some(&o.pubkey()), &[&o], bh)).unwrap(); }
    { let ix = set_ready_ix(&e, p); let bh = e.svm.latest_blockhash();
      e.svm.send_transaction(Transaction::new_signed_with_payer(&[ix], Some(&o.pubkey()), &[&o], bh)).unwrap(); }
    assert!(submit_relayed(&mut e, cs, 20_000, 2_000_000_000, p).is_err(), "tampered fee must fail");
}

#[test]
fn relayed_requires_ed25519_instruction() {
    // Submit claim_relayed alone (no preceding Ed25519 auth) -> rejected.
    let mut e = setup();
    let (cs, cc) = secret_pair(7);
    let (_rs, rc) = secret_pair(9);
    let (amount, nonce, fee) = (3 * SOL, 3u64, 20_000u64);
    let p = swap_pda(&e, &cc, &rc, amount, nonce);
    let o = e.owner.insecure_clone();
    { let ix = new_ix(&e, cc, rc, amount, nonce, p); let bh = e.svm.latest_blockhash();
      e.svm.send_transaction(Transaction::new_signed_with_payer(&[ix], Some(&o.pubkey()), &[&o], bh)).unwrap(); }
    { let ix = set_ready_ix(&e, p); let bh = e.svm.latest_blockhash();
      e.svm.send_transaction(Transaction::new_signed_with_payer(&[ix], Some(&o.pubkey()), &[&o], bh)).unwrap(); }
    let r = e.relayer.insecure_clone();
    let claim = claim_relayed_ix(&e, cs, fee, p);
    let bh = e.svm.latest_blockhash();
    let tx = Transaction::new_signed_with_payer(&[claim], Some(&r.pubkey()), &[&r], bh);
    assert!(e.svm.send_transaction(tx).is_err(), "claim without Ed25519 auth must fail");
}
