//! Gasless SPL relayed-claim test (LiteSVM): a claimer with no SOL holds USDC-style tokens;
//! they sign an Ed25519 authorization, a relayer submits [ed25519, claim_relayed_spl] and
//! takes a token fee. Verifies the claimer gets amount-fee and the relayer gets fee.
use curve25519_dalek::{edwards::EdwardsPoint, scalar::Scalar};
use litesvm::LiteSVM;
use solana_program::program_option::COption;
use solana_sdk::{
    account::Account, instruction::{AccountMeta, Instruction}, program_pack::Pack, pubkey::Pubkey,
    signature::{Keypair, Signer}, system_program, transaction::Transaction,
    compute_budget::ComputeBudgetInstruction,
};
use spl_token::state::{Account as TokenAccount, AccountState, Mint};

const SO: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../program/target/deploy/monzero_sol_swap.so");
const SOL: u64 = 1_000_000_000;
const PREFIX: &[u8] = b"monzero-sol-relay-v1";

fn pair(seed: u8) -> ([u8; 32], [u8; 32]) {
    let mut sb = [seed; 32]; sb[31] = 0;
    let s = Scalar::from_bytes_mod_order(sb);
    (s.to_bytes(), EdwardsPoint::mul_base(&s).compress().to_bytes())
}
fn make_mint(svm: &mut LiteSVM, decimals: u8) -> Pubkey {
    let key = Pubkey::new_unique();
    let m = Mint { mint_authority: COption::None, supply: 0, decimals, is_initialized: true, freeze_authority: COption::None };
    let mut data = vec![0u8; Mint::LEN]; Mint::pack(m, &mut data).unwrap();
    let lamports = svm.minimum_balance_for_rent_exemption(Mint::LEN);
    svm.set_account(key, Account { lamports, data, owner: spl_token::id(), executable: false, rent_epoch: 0 }).unwrap();
    key
}
fn make_token_acc(svm: &mut LiteSVM, mint: Pubkey, owner: Pubkey, amount: u64) -> Pubkey {
    let key = Pubkey::new_unique();
    let a = TokenAccount { mint, owner, amount, delegate: COption::None, state: AccountState::Initialized,
        is_native: COption::None, delegated_amount: 0, close_authority: COption::None };
    let mut data = vec![0u8; TokenAccount::LEN]; TokenAccount::pack(a, &mut data).unwrap();
    let lamports = svm.minimum_balance_for_rent_exemption(TokenAccount::LEN);
    svm.set_account(key, Account { lamports, data, owner: spl_token::id(), executable: false, rent_epoch: 0 }).unwrap();
    key
}
fn token_balance(svm: &LiteSVM, key: &Pubkey) -> u64 {
    TokenAccount::unpack(&svm.get_account(key).unwrap().data).unwrap().amount
}
fn ed25519_ix(pubkey: &[u8; 32], signature: &[u8; 64], message: &[u8]) -> Instruction {
    let mut d = vec![1u8, 0u8];
    let (so, po, mo): (u16, u16, u16) = (16, 80, 112);
    d.extend_from_slice(&so.to_le_bytes()); d.extend_from_slice(&u16::MAX.to_le_bytes());
    d.extend_from_slice(&po.to_le_bytes()); d.extend_from_slice(&u16::MAX.to_le_bytes());
    d.extend_from_slice(&mo.to_le_bytes()); d.extend_from_slice(&(message.len() as u16).to_le_bytes());
    d.extend_from_slice(&u16::MAX.to_le_bytes());
    d.extend_from_slice(signature); d.extend_from_slice(pubkey); d.extend_from_slice(message);
    Instruction { program_id: solana_sdk::ed25519_program::id(), accounts: vec![], data: d }
}

#[test]
fn spl_relayed_claim_pays_claimer_and_relayer_in_tokens() {
    let mut svm = LiteSVM::new();
    let pid = Pubkey::new_unique();
    svm.add_program_from_file(pid, SO).unwrap();
    let owner = Keypair::new();       // maker / SOL owner
    let claimer = Keypair::new();     // NO SOL — gasless
    let relayer = Keypair::new();
    svm.airdrop(&owner.pubkey(), 100 * SOL).unwrap();
    svm.airdrop(&relayer.pubkey(), 10 * SOL).unwrap();
    let mint = make_mint(&mut svm, 6);
    let owner_tok = make_token_acc(&mut svm, mint, owner.pubkey(), 1_000_000_000);
    let claimer_tok = make_token_acc(&mut svm, mint, claimer.pubkey(), 0);
    let relayer_tok = make_token_acc(&mut svm, mint, relayer.pubkey(), 0);

    let (cs, cc) = pair(7);
    let (_rs, rc) = pair(9);
    let (amount, nonce, fee) = (500_000_000u64, 1u64, 2_000_000u64); // 500 USDC, 2 USDC fee
    let p = Pubkey::find_program_address(&[b"swap", owner.pubkey().as_ref(), claimer.pubkey().as_ref(),
        &cc, &rc, &amount.to_le_bytes(), &nonce.to_le_bytes()], &pid).0;
    let vault = Pubkey::find_program_address(&[b"vault", p.as_ref()], &pid).0;

    let budget = ComputeBudgetInstruction::set_compute_unit_limit(400_000);
    // NewSpl (lock)
    let mut data = vec![4u8]; data.extend_from_slice(&cc); data.extend_from_slice(&rc);
    data.extend_from_slice(&100u64.to_le_bytes()); data.extend_from_slice(&100u64.to_le_bytes());
    data.extend_from_slice(&amount.to_le_bytes()); data.extend_from_slice(&nonce.to_le_bytes());
    let new_ix = Instruction { program_id: pid, data, accounts: vec![
        AccountMeta::new(owner.pubkey(), true), AccountMeta::new_readonly(claimer.pubkey(), false),
        AccountMeta::new(p, false), AccountMeta::new_readonly(mint, false),
        AccountMeta::new(owner_tok, false), AccountMeta::new(vault, false),
        AccountMeta::new_readonly(spl_token::id(), false), AccountMeta::new_readonly(system_program::id(), false)] };
    let bh = svm.latest_blockhash();
    svm.send_transaction(Transaction::new_signed_with_payer(&[budget.clone(), new_ix], Some(&owner.pubkey()), &[&owner], bh)).unwrap();
    // SetReady
    let bh = svm.latest_blockhash();
    svm.send_transaction(Transaction::new_signed_with_payer(
        &[Instruction { program_id: pid, data: vec![1u8], accounts: vec![AccountMeta::new(owner.pubkey(), true), AccountMeta::new(p, false)] }],
        Some(&owner.pubkey()), &[&owner], bh)).unwrap();

    // claimer signs the relay authorization
    let mut msg = PREFIX.to_vec();
    msg.extend_from_slice(p.as_ref()); msg.extend_from_slice(&fee.to_le_bytes()); msg.extend_from_slice(relayer.pubkey().as_ref());
    let sig = claimer.sign_message(&msg);
    let ed = ed25519_ix(&claimer.pubkey().to_bytes(), &sig.as_ref().try_into().unwrap(), &msg);
    // ClaimRelayedSpl (opcode 8)
    let mut cdata = vec![8u8]; cdata.extend_from_slice(&cs); cdata.extend_from_slice(&fee.to_le_bytes());
    let claim = Instruction { program_id: pid, data: cdata, accounts: vec![
        AccountMeta::new(relayer.pubkey(), true),
        AccountMeta::new(p, false),
        AccountMeta::new(claimer_tok, false),
        AccountMeta::new(relayer_tok, false),
        AccountMeta::new(vault, false),
        AccountMeta::new(owner.pubkey(), false),
        AccountMeta::new_readonly(mint, false),
        AccountMeta::new_readonly(spl_token::id(), false),
        AccountMeta::new_readonly(solana_sdk::sysvar::instructions::id(), false)] };
    let bh = svm.latest_blockhash();
    svm.send_transaction(Transaction::new_signed_with_payer(&[budget, ed, claim], Some(&relayer.pubkey()), &[&relayer], bh)).unwrap();

    assert_eq!(token_balance(&svm, &claimer_tok), amount - fee, "claimer got amount - fee in tokens");
    assert_eq!(token_balance(&svm, &relayer_tok), fee, "relayer got the fee in tokens");
    assert!(svm.get_account(&vault).map(|a| a.lamports == 0).unwrap_or(true), "vault closed");
}
