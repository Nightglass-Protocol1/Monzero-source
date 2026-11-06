//! End-to-end orchestration: the `route` coordinator drives a real Solana swap leg against
//! the compiled program in LiteSVM, with the XMZ leg simulated. This is the concrete
//! Solana half of the `LegDriver` — it performs the real Solana transactions (lock,
//! set_ready, observe Alice's claim, refund) and feeds the coordinator the events, while
//! the Monero-side events are stand-ins (the real XMZ driver is monero-sys build-gated).
use curve25519_dalek::{edwards::EdwardsPoint, scalar::Scalar};
use litesvm::LiteSVM;
use monzero_sol_route::driver::{drive, LegDriver, Outcome};
use monzero_sol_route::secret::{extract_revealed_secret, reconstruct_spend_key};
use monzero_sol_route::{Action, Event};
use solana_sdk::{
    account::ReadableAccount, instruction::{AccountMeta, Instruction}, pubkey::Pubkey,
    signature::{Keypair, Signer}, system_program, transaction::Transaction,
};

const SO: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../program/target/deploy/monzero_sol_swap.so");
const SOL: u64 = 1_000_000_000;

fn pair(seed: u8) -> ([u8; 32], [u8; 32]) {
    let mut sb = [seed; 32]; sb[31] = 0;
    let s = Scalar::from_bytes_mod_order(sb);
    (s.to_bytes(), EdwardsPoint::mul_base(&s).compress().to_bytes())
}

/// Bob's Solana-leg driver. Bob is the Solana `owner`; Alice (maker) is the `claimer`. The
/// driver holds both keypairs because, in a test, it also stands in for Alice's wallet and
/// the Monero leg. A real driver would observe Alice's claim on-chain and run the eigenwallet
/// engine for the XMZ side.
struct SolanaLeg {
    svm: LiteSVM,
    pid: Pubkey,
    bob: Keypair,     // owner
    alice: Keypair,   // claimer
    pda: Pubkey,
    cc: [u8; 32],     // claim_commitment = S_a
    rc: [u8; 32],     // refund_commitment = S_b
    amount: u64,
    nonce: u64,
    s_a: [u8; 32],    // Alice's claim secret (her Monero share)
    s_b: [u8; 32],    // Bob's own Monero share (= the refund secret)
    phase: u32,
    pub revealed: Option<[u8; 32]>,  // s_a, once read off Alice's claim
    pub swept: bool,                 // XMZ reconstructed+swept
}

impl SolanaLeg {
    fn send(&mut self, ix: Instruction, signer: &Keypair) -> Result<(), String> {
        let bh = self.svm.latest_blockhash();
        let tx = Transaction::new_signed_with_payer(&[ix], Some(&signer.pubkey()), &[signer], bh);
        self.svm.send_transaction(tx).map(|_| ()).map_err(|f| format!("{:?}", f.err))
    }
    fn new_ix(&self) -> Instruction {
        let mut data = vec![0u8];
        data.extend_from_slice(&self.cc); data.extend_from_slice(&self.rc);
        data.extend_from_slice(&100u64.to_le_bytes()); data.extend_from_slice(&100u64.to_le_bytes());
        data.extend_from_slice(&self.amount.to_le_bytes()); data.extend_from_slice(&self.nonce.to_le_bytes());
        Instruction { program_id: self.pid, data, accounts: vec![
            AccountMeta::new(self.bob.pubkey(), true),
            AccountMeta::new_readonly(self.alice.pubkey(), false),
            AccountMeta::new(self.pda, false),
            AccountMeta::new_readonly(system_program::id(), false)] }
    }
    fn set_ready_ix(&self) -> Instruction {
        Instruction { program_id: self.pid, data: vec![1u8], accounts: vec![
            AccountMeta::new(self.bob.pubkey(), true), AccountMeta::new(self.pda, false)] }
    }
    fn claim_ix(&self) -> Instruction {
        let mut data = vec![2u8]; data.extend_from_slice(&self.s_a);
        Instruction { program_id: self.pid, data, accounts: vec![
            AccountMeta::new(self.alice.pubkey(), true),
            AccountMeta::new(self.pda, false),
            AccountMeta::new(self.bob.pubkey(), false)] }
    }
}

impl LegDriver for SolanaLeg {
    fn act(&mut self, action: Action) -> Result<(), String> {
        match action {
            Action::PublishReady => { let ix = self.set_ready_ix(); self.send(ix, &self.bob.insecure_clone()) }
            Action::RedeemXmr => {
                // Bob has read s_a off Alice's claim; combine with his own s_b -> spend key.
                let s_a = self.revealed.ok_or("no revealed secret yet")?;
                let key = reconstruct_spend_key(&s_a, &self.s_b).ok_or("bad shares")?;
                if key == self.s_a || key == self.s_b { return Err("key is just a share".into()); }
                self.swept = true; // a real driver would now sweep the XMZ 2-of-2
                Ok(())
            }
            Action::PublishRefund => Err("happy path should not refund".into()),
            Action::None => Ok(()),
        }
    }
    fn next_event(&mut self) -> Result<Event, String> {
        self.phase += 1;
        match self.phase {
            1 => { let ix = self.new_ix(); self.send(ix, &self.bob.insecure_clone())?; Ok(Event::SolLockPublished) }
            2 => {
                // lock confirmed: the escrow holds the amount
                if self.svm.get_balance(&self.pda).unwrap_or(0) < self.amount { return Err("escrow not funded".into()); }
                Ok(Event::SolLockConfirmed)
            }
            3 => Ok(Event::XmrLockSeen),       // (simulated Monero leg)
            4 => Ok(Event::XmrLockConfirmed),  // (simulated)
            5 => Ok(Event::ReadyPublished),    // set_ready was just sent by act(PublishReady)
            6 => {
                // Alice claims the SOL; Bob reads s_a off the claim instruction.
                let ix = self.claim_ix();
                self.revealed = extract_revealed_secret(&ix.data);
                let before = self.svm.get_balance(&self.alice.pubkey()).unwrap_or(0);
                self.send(ix, &self.alice.insecure_clone())?;
                let after = self.svm.get_balance(&self.alice.pubkey()).unwrap_or(0);
                if after <= before { return Err("claimer was not paid".into()); }
                Ok(Event::AliceClaimedSol)
            }
            7 => Ok(Event::XmrRedeemed),       // (simulated sweep confirmation)
            _ => Err("no more events".into()),
        }
    }
}

fn setup_leg(nonce: u64) -> SolanaLeg {
    let mut svm = LiteSVM::new();
    let pid = Pubkey::new_unique();
    svm.add_program_from_file(pid, SO).unwrap();
    let bob = Keypair::new();
    let alice = Keypair::new();
    svm.airdrop(&bob.pubkey(), 100 * SOL).unwrap();
    svm.airdrop(&alice.pubkey(), 1 * SOL).unwrap(); // tx fees for her claim
    let (s_a, cc) = pair(7);
    let (s_b, rc) = pair(9);
    let amount = 2 * SOL;
    let pda = Pubkey::find_program_address(
        &[b"swap", bob.pubkey().as_ref(), alice.pubkey().as_ref(), &cc, &rc,
          &amount.to_le_bytes(), &nonce.to_le_bytes()], &pid).0;
    SolanaLeg { svm, pid, bob, alice, pda, cc, rc, amount, nonce, s_a, s_b,
        phase: 0, revealed: None, swept: false }
}

#[test]
fn coordinator_drives_the_real_solana_leg_to_completion() {
    let mut leg = setup_leg(1);
    let alice = leg.alice.pubkey();
    let out = drive(&mut leg).expect("drive");
    assert_eq!(out, Outcome::Completed);
    // real on-chain effects: Alice holds the SOL, the escrow PDA is closed
    assert!(leg.svm.get_balance(&alice).unwrap_or(0) > 2 * SOL, "Alice received the escrow");
    assert!(leg.svm.get_account(&leg.pda).map(|a| a.lamports() == 0).unwrap_or(true), "escrow closed");
    // Bob read s_a and reconstructed the spend key (the XMZ sweep stand-in ran)
    assert_eq!(leg.revealed, Some(leg.s_a), "Bob read s_a off the claim");
    assert!(leg.swept, "XMZ sweep step ran");
}
