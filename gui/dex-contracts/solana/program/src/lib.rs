//! Monzero XMZ<->Solana atomic-swap program (native SOL).
//!
//! Mirrors the proven EVM/BTC design: a swap moves through New (lock) -> SetReady ->
//! Claim / Refund, each party revealing an ed25519 secret at the right time, bounded by
//! two timelocks. All funds-critical checks live in the host-tested `swap-logic` crate;
//! this program is the thin on-chain wrapper that adds the PDA escrow + lamport moves.
//!
//! Non-custodial like the EVM contract: the escrow is a program-derived account, there is
//! no admin/owner/upgrade authority in the logic, and settlement is deterministic.
//! SPL-token (USDC/USDT) support is a follow-up layer; this is the native-SOL core.
#![allow(unexpected_cfgs)]
use borsh::{BorshDeserialize, BorshSerialize};
use monzero_sol_swap_logic as logic;
use solana_curve25519::edwards::{multiply_edwards, PodEdwardsPoint};
use solana_curve25519::scalar::PodScalar;
use solana_program::{
    account_info::{next_account_info, AccountInfo},
    entrypoint,
    entrypoint::ProgramResult,
    msg,
    program::invoke_signed,
    program_error::ProgramError,
    pubkey::Pubkey,
    rent::Rent,
    system_instruction,
    system_program,
    sysvar::{clock::Clock, Sysvar},
};

/// Persistent swap state, stored in the escrow PDA.
#[derive(BorshSerialize, BorshDeserialize, Clone, Debug)]
pub struct Swap {
    pub owner: Pubkey,            // locks the funds; can set_ready / refund
    pub claimer: Pubkey,         // receives the funds on claim
    pub claim_commitment: [u8; 32],  // ed25519 A = s_claim * B
    pub refund_commitment: [u8; 32], // ed25519 A = s_owner * B
    pub timeout1: u64,           // absolute unix time: cancel/claim boundary
    pub timeout2: u64,           // absolute unix time: refund boundary (> timeout1)
    pub amount: u64,             // native: lamports; spl: token base units
    pub stage: u8,               // 1=Pending 2=Ready 3=Completed
    pub bump: u8,                // swap PDA bump
    pub mint: Pubkey,            // spl mint (default = native SOL)
    pub kind: u8,                // 0 = native SOL, 1 = SPL token
    pub vault_bump: u8,          // spl vault PDA bump (0 for native)
    pub nonce: u64,              // kept so claim/refund can re-derive the PDA to sign
}
const SWAP_LEN: usize = 32 + 32 + 32 + 32 + 8 + 8 + 8 + 1 + 1 + 32 + 1 + 1 + 8; // 196

fn stage_to_u8(s: logic::Stage) -> u8 { s as u8 }
fn stage_from_u8(v: u8) -> logic::Stage {
    match v { 1 => logic::Stage::Pending, 2 => logic::Stage::Ready, 3 => logic::Stage::Completed, _ => logic::Stage::Invalid }
}
fn map_err(_e: logic::SwapError) -> ProgramError { ProgramError::Custom(1) }

/// Compressed ed25519 basepoint (0x58 then 0x66*31) — the point the syscall multiplies by.
const BASEPOINT_BYTES: [u8; 32] = { let mut b = [0x66u8; 32]; b[0] = 0x58; b };

/// Verify an ed25519 secret commitment on-chain via the curve25519 syscall: `s*B == A`.
/// This matches the host-side dalek `commit()` (checked by the integration tests); dalek's
/// basepoint tables cannot run in BPF (4KB stack), so the syscall is the on-chain path.
fn verify_secret(secret: &[u8; 32], commitment: &[u8; 32]) -> bool {
    if secret == &[0u8; 32] { return false; }
    match multiply_edwards(&PodScalar(*secret), &PodEdwardsPoint(BASEPOINT_BYTES)) {
        Some(p) => &p.0 == commitment,
        None => false,
    }
}

#[derive(BorshSerialize, BorshDeserialize, Debug)]
pub enum Instruction {
    /// Lock `amount` lamports; timeouts are now+duration1 and now+duration1+duration2.
    New { claim_commitment: [u8; 32], refund_commitment: [u8; 32], duration1: u64, duration2: u64, amount: u64, nonce: u64 },
    SetReady,
    Claim { secret: [u8; 32] },
    Refund { secret: [u8; 32] },
    /// Lock `amount` base units of an SPL token into a PDA-owned vault.
    NewSpl { claim_commitment: [u8; 32], refund_commitment: [u8; 32], duration1: u64, duration2: u64, amount: u64, nonce: u64 },
    ClaimSpl { secret: [u8; 32] },
    RefundSpl { secret: [u8; 32] },
}

entrypoint!(process);
pub fn process(program_id: &Pubkey, accounts: &[AccountInfo], data: &[u8]) -> ProgramResult {
    match Instruction::try_from_slice(data).map_err(|_| ProgramError::InvalidInstructionData)? {
        Instruction::New { claim_commitment, refund_commitment, duration1, duration2, amount, nonce } =>
            new_swap(program_id, accounts, claim_commitment, refund_commitment, duration1, duration2, amount, nonce),
        Instruction::SetReady => set_ready(program_id, accounts),
        Instruction::Claim { secret } => claim(program_id, accounts, secret),
        Instruction::Refund { secret } => refund(program_id, accounts, secret),
        Instruction::NewSpl { claim_commitment, refund_commitment, duration1, duration2, amount, nonce } =>
            spl::new_spl(program_id, accounts, claim_commitment, refund_commitment, duration1, duration2, amount, nonce),
        Instruction::ClaimSpl { secret } => spl::claim_spl(program_id, accounts, secret),
        Instruction::RefundSpl { secret } => spl::refund_spl(program_id, accounts, secret),
    }
}

fn pda_seeds<'a>(owner: &'a Pubkey, claimer: &'a Pubkey, cc: &'a [u8; 32], rc: &'a [u8; 32],
    amount_le: &'a [u8; 8], nonce_le: &'a [u8; 8]) -> [&'a [u8]; 7] {
    [b"swap", owner.as_ref(), claimer.as_ref(), cc, rc, amount_le, nonce_le]
}

#[allow(clippy::too_many_arguments)]
fn new_swap(program_id: &Pubkey, accounts: &[AccountInfo], cc: [u8; 32], rc: [u8; 32],
    dur1: u64, dur2: u64, amount: u64, nonce: u64) -> ProgramResult {
    let it = &mut accounts.iter();
    let owner = next_account_info(it)?;        // signer, payer
    let claimer = next_account_info(it)?;      // read-only
    let swap_ai = next_account_info(it)?;      // PDA to create (writable)
    let system = next_account_info(it)?;

    if !owner.is_signer { return Err(ProgramError::MissingRequiredSignature); }
    if system.key != &system_program::ID { return Err(ProgramError::IncorrectProgramId); }
    logic::check_new(amount, dur1, dur2).map_err(map_err)?;
    if !swap_ai.data_is_empty() { return Err(ProgramError::AccountAlreadyInitialized); }

    let amount_le = amount.to_le_bytes();
    let nonce_le = nonce.to_le_bytes();
    let seeds = pda_seeds(owner.key, claimer.key, &cc, &rc, &amount_le, &nonce_le);
    let (pda, bump) = Pubkey::find_program_address(&seeds, program_id);
    if &pda != swap_ai.key { return Err(ProgramError::InvalidSeeds); }

    let now = Clock::get()?.unix_timestamp as u64;
    let timeout1 = now.checked_add(dur1).ok_or(ProgramError::ArithmeticOverflow)?;
    let timeout2 = timeout1.checked_add(dur2).ok_or(ProgramError::ArithmeticOverflow)?;

    // Fund the escrow with rent + the swap amount, from the owner.
    let rent = Rent::get()?.minimum_balance(SWAP_LEN);
    let lamports = rent.checked_add(amount).ok_or(ProgramError::ArithmeticOverflow)?;
    let bump_arr = [bump];
    let signer: [&[u8]; 8] = [b"swap", owner.key.as_ref(), claimer.key.as_ref(), &cc, &rc, &amount_le, &nonce_le, &bump_arr];
    invoke_signed(
        &system_instruction::create_account(owner.key, swap_ai.key, lamports, SWAP_LEN as u64, program_id),
        &[owner.clone(), swap_ai.clone(), system.clone()],
        &[&signer],
    )?;

    let swap = Swap { owner: *owner.key, claimer: *claimer.key, claim_commitment: cc, refund_commitment: rc,
        timeout1, timeout2, amount, stage: stage_to_u8(logic::Stage::Pending), bump,
        mint: Pubkey::default(), kind: 0, vault_bump: 0, nonce };
    swap.serialize(&mut &mut swap_ai.data.borrow_mut()[..])?;
    msg!("monzero-sol: locked {} lamports, t1={} t2={}", amount, timeout1, timeout2);
    Ok(())
}

fn load(program_id: &Pubkey, swap_ai: &AccountInfo) -> Result<Swap, ProgramError> {
    if swap_ai.owner != program_id { return Err(ProgramError::IllegalOwner); }
    Swap::try_from_slice(&swap_ai.data.borrow()).map_err(|_| ProgramError::InvalidAccountData)
}

fn set_ready(program_id: &Pubkey, accounts: &[AccountInfo]) -> ProgramResult {
    let it = &mut accounts.iter();
    let owner = next_account_info(it)?;
    let swap_ai = next_account_info(it)?;
    if !owner.is_signer { return Err(ProgramError::MissingRequiredSignature); }
    let mut swap = load(program_id, swap_ai)?;
    if &swap.owner != owner.key { return Err(ProgramError::IllegalOwner); }
    let now = Clock::get()?.unix_timestamp as u64;
    logic::check_set_ready(stage_from_u8(swap.stage), now, swap.timeout1).map_err(map_err)?;
    swap.stage = stage_to_u8(logic::Stage::Ready);
    swap.serialize(&mut &mut swap_ai.data.borrow_mut()[..])?;
    msg!("monzero-sol: ready");
    Ok(())
}

fn claim(program_id: &Pubkey, accounts: &[AccountInfo], secret: [u8; 32]) -> ProgramResult {
    let it = &mut accounts.iter();
    let claimer = next_account_info(it)?;   // signer, receives `amount`
    let swap_ai = next_account_info(it)?;   // PDA (writable)
    let owner = next_account_info(it)?;     // receives the rent on close
    let swap = load(program_id, swap_ai)?;
    if !claimer.is_signer { return Err(ProgramError::MissingRequiredSignature); }
    if &swap.claimer != claimer.key { return Err(ProgramError::InvalidAccountData); }
    if &swap.owner != owner.key { return Err(ProgramError::InvalidAccountData); }
    let now = Clock::get()?.unix_timestamp as u64;
    let ok = verify_secret(&secret, &swap.claim_commitment);
    logic::check_claim(stage_from_u8(swap.stage), now, swap.timeout1, swap.timeout2, ok).map_err(map_err)?;
    settle(swap_ai, swap.amount, claimer, owner)?;
    // Reveal the secret in the logs (it is also in the tx), so the counterparty sees it.
    msg!("monzero-sol: claimed");
    solana_program::log::sol_log_data(&[b"claim_secret", &secret]);
    Ok(())
}

fn refund(program_id: &Pubkey, accounts: &[AccountInfo], secret: [u8; 32]) -> ProgramResult {
    let it = &mut accounts.iter();
    let owner = next_account_info(it)?;     // signer, receives amount + rent
    let swap_ai = next_account_info(it)?;
    let swap = load(program_id, swap_ai)?;
    if !owner.is_signer { return Err(ProgramError::MissingRequiredSignature); }
    if &swap.owner != owner.key { return Err(ProgramError::IllegalOwner); }
    let now = Clock::get()?.unix_timestamp as u64;
    let ok = verify_secret(&secret, &swap.refund_commitment);
    logic::check_refund(stage_from_u8(swap.stage), now, swap.timeout1, swap.timeout2, ok).map_err(map_err)?;
    // Everything (amount + rent) goes back to the owner; close the PDA.
    settle(swap_ai, swap.amount, owner, owner)?;
    msg!("monzero-sol: refunded");
    solana_program::log::sol_log_data(&[b"refund_secret", &secret]);
    Ok(())
}

/// Pay `amount` lamports to `recipient`, the remaining rent to `rent_to`, and close the
/// PDA (zero its lamports + data). No CPI -> no reentrancy; closing prevents replay.
fn settle(swap_ai: &AccountInfo, amount: u64, recipient: &AccountInfo, rent_to: &AccountInfo) -> ProgramResult {
    let total = **swap_ai.lamports.borrow();
    let rent = total.checked_sub(amount).ok_or(ProgramError::InsufficientFunds)?;
    **swap_ai.lamports.borrow_mut() = 0;
    **recipient.lamports.borrow_mut() = recipient.lamports().checked_add(amount).ok_or(ProgramError::ArithmeticOverflow)?;
    if rent > 0 {
        **rent_to.lamports.borrow_mut() = rent_to.lamports().checked_add(rent).ok_or(ProgramError::ArithmeticOverflow)?;
    }
    // Wipe the data so the (now zero-lamport) account cannot be reused.
    let mut d = swap_ai.data.borrow_mut();
    for b in d.iter_mut() { *b = 0; }
    Ok(())
}

/// SPL-token swaps (USDC/USDT, classic SPL token program). The escrow is a token account
/// owned by the swap PDA; the funds-critical checks are the same `swap-logic` + syscall
/// secret verify as the native path — only the fund movement is a token-program CPI.
mod spl {
    use super::*;
    use solana_program::program::invoke;
    use solana_program::program_pack::Pack;
    use spl_token::state::{Account as TokenAccount, Mint};

    const VAULT_SEED: &[u8] = b"vault";

    fn mint_decimals(mint_ai: &AccountInfo) -> Result<u8, ProgramError> {
        Ok(Mint::unpack(&mint_ai.data.borrow()).map_err(|_| ProgramError::InvalidAccountData)?.decimals)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn new_spl(program_id: &Pubkey, accounts: &[AccountInfo], cc: [u8; 32], rc: [u8; 32],
        dur1: u64, dur2: u64, amount: u64, nonce: u64) -> ProgramResult {
        let it = &mut accounts.iter();
        let owner = next_account_info(it)?;        // signer, payer
        let claimer = next_account_info(it)?;      // readonly
        let swap_ai = next_account_info(it)?;      // state PDA (writable)
        let mint_ai = next_account_info(it)?;      // readonly
        let owner_token = next_account_info(it)?;  // source token account (writable)
        let vault_ai = next_account_info(it)?;     // vault PDA token account (writable)
        let token_prog = next_account_info(it)?;
        let system = next_account_info(it)?;

        if !owner.is_signer { return Err(ProgramError::MissingRequiredSignature); }
        if token_prog.key != &spl_token::ID { return Err(ProgramError::IncorrectProgramId); }
        if system.key != &system_program::ID { return Err(ProgramError::IncorrectProgramId); }
        logic::check_new(amount, dur1, dur2).map_err(map_err)?;
        if !swap_ai.data_is_empty() { return Err(ProgramError::AccountAlreadyInitialized); }

        let amount_le = amount.to_le_bytes();
        let nonce_le = nonce.to_le_bytes();
        let (pda, bump) = Pubkey::find_program_address(
            &pda_seeds(owner.key, claimer.key, &cc, &rc, &amount_le, &nonce_le), program_id);
        if &pda != swap_ai.key { return Err(ProgramError::InvalidSeeds); }
        let (vault_pda, vault_bump) = Pubkey::find_program_address(&[VAULT_SEED, swap_ai.key.as_ref()], program_id);
        if &vault_pda != vault_ai.key { return Err(ProgramError::InvalidSeeds); }

        let now = Clock::get()?.unix_timestamp as u64;
        let timeout1 = now.checked_add(dur1).ok_or(ProgramError::ArithmeticOverflow)?;
        let timeout2 = timeout1.checked_add(dur2).ok_or(ProgramError::ArithmeticOverflow)?;
        let rent = Rent::get()?;

        // State PDA.
        let bump_arr = [bump];
        let state_signer: [&[u8]; 8] = [b"swap", owner.key.as_ref(), claimer.key.as_ref(), &cc, &rc, &amount_le, &nonce_le, &bump_arr];
        invoke_signed(
            &system_instruction::create_account(owner.key, swap_ai.key, rent.minimum_balance(SWAP_LEN), SWAP_LEN as u64, program_id),
            &[owner.clone(), swap_ai.clone(), system.clone()], &[&state_signer])?;

        // Vault token account (owned by the token program; authority = swap PDA).
        let vbump_arr = [vault_bump];
        let vault_signer: [&[u8]; 3] = [VAULT_SEED, swap_ai.key.as_ref(), &vbump_arr];
        invoke_signed(
            &system_instruction::create_account(owner.key, vault_ai.key, rent.minimum_balance(TokenAccount::LEN), TokenAccount::LEN as u64, &spl_token::ID),
            &[owner.clone(), vault_ai.clone(), system.clone()], &[&vault_signer])?;
        invoke(&spl_token::instruction::initialize_account3(&spl_token::ID, vault_ai.key, mint_ai.key, swap_ai.key)?,
            &[vault_ai.clone(), mint_ai.clone(), token_prog.clone()])?;

        // Deposit owner -> vault.
        let decimals = mint_decimals(mint_ai)?;
        invoke(&spl_token::instruction::transfer_checked(&spl_token::ID, owner_token.key, mint_ai.key, vault_ai.key, owner.key, &[], amount, decimals)?,
            &[owner_token.clone(), mint_ai.clone(), vault_ai.clone(), owner.clone(), token_prog.clone()])?;

        let swap = Swap { owner: *owner.key, claimer: *claimer.key, claim_commitment: cc, refund_commitment: rc,
            timeout1, timeout2, amount, stage: stage_to_u8(logic::Stage::Pending), bump,
            mint: *mint_ai.key, kind: 1, vault_bump, nonce };
        swap.serialize(&mut &mut swap_ai.data.borrow_mut()[..])?;
        msg!("monzero-sol: locked {} spl units, t1={} t2={}", amount, timeout1, timeout2);
        Ok(())
    }

    pub fn claim_spl(program_id: &Pubkey, accounts: &[AccountInfo], secret: [u8; 32]) -> ProgramResult {
        let it = &mut accounts.iter();
        let claimer = next_account_info(it)?;       // signer
        let swap_ai = next_account_info(it)?;
        let claimer_token = next_account_info(it)?; // destination (writable)
        let vault_ai = next_account_info(it)?;
        let owner = next_account_info(it)?;         // rent refund (writable)
        let mint_ai = next_account_info(it)?;
        let token_prog = next_account_info(it)?;
        let swap = load(program_id, swap_ai)?;
        if !claimer.is_signer { return Err(ProgramError::MissingRequiredSignature); }
        if token_prog.key != &spl_token::ID { return Err(ProgramError::IncorrectProgramId); }
        if swap.kind != 1 { return Err(ProgramError::InvalidAccountData); }
        if &swap.claimer != claimer.key || &swap.owner != owner.key || &swap.mint != mint_ai.key {
            return Err(ProgramError::InvalidAccountData);
        }
        let now = Clock::get()?.unix_timestamp as u64;
        let ok = verify_secret(&secret, &swap.claim_commitment);
        logic::check_claim(stage_from_u8(swap.stage), now, swap.timeout1, swap.timeout2, ok).map_err(map_err)?;
        settle_spl(&swap, swap_ai, vault_ai, mint_ai, claimer_token, owner, token_prog)?;
        msg!("monzero-sol: spl claimed");
        solana_program::log::sol_log_data(&[b"claim_secret", &secret]);
        Ok(())
    }

    pub fn refund_spl(program_id: &Pubkey, accounts: &[AccountInfo], secret: [u8; 32]) -> ProgramResult {
        let it = &mut accounts.iter();
        let owner = next_account_info(it)?;        // signer, rent refund + destination
        let swap_ai = next_account_info(it)?;
        let owner_token = next_account_info(it)?;  // destination (writable)
        let vault_ai = next_account_info(it)?;
        let mint_ai = next_account_info(it)?;
        let token_prog = next_account_info(it)?;
        let swap = load(program_id, swap_ai)?;
        if !owner.is_signer { return Err(ProgramError::MissingRequiredSignature); }
        if token_prog.key != &spl_token::ID { return Err(ProgramError::IncorrectProgramId); }
        if swap.kind != 1 { return Err(ProgramError::InvalidAccountData); }
        if &swap.owner != owner.key || &swap.mint != mint_ai.key { return Err(ProgramError::InvalidAccountData); }
        let now = Clock::get()?.unix_timestamp as u64;
        let ok = verify_secret(&secret, &swap.refund_commitment);
        logic::check_refund(stage_from_u8(swap.stage), now, swap.timeout1, swap.timeout2, ok).map_err(map_err)?;
        settle_spl(&swap, swap_ai, vault_ai, mint_ai, owner_token, owner, token_prog)?;
        msg!("monzero-sol: spl refunded");
        solana_program::log::sol_log_data(&[b"refund_secret", &secret]);
        Ok(())
    }

    /// Move the whole vault to `dest_token`, close the vault + state (rent to `rent_to`).
    /// The swap PDA is the vault authority, so it signs via its seeds. No reentrancy on
    /// Solana; closing both accounts prevents replay.
    fn settle_spl<'a>(swap: &Swap, swap_ai: &AccountInfo<'a>, vault_ai: &AccountInfo<'a>, mint_ai: &AccountInfo<'a>,
        dest_token: &AccountInfo<'a>, rent_to: &AccountInfo<'a>, token_prog: &AccountInfo<'a>) -> ProgramResult {
        let amount_le = swap.amount.to_le_bytes();
        let nonce_le = swap.nonce.to_le_bytes();
        let bump_arr = [swap.bump];
        let signer: [&[u8]; 8] = [b"swap", swap.owner.as_ref(), swap.claimer.as_ref(),
            &swap.claim_commitment, &swap.refund_commitment, &amount_le, &nonce_le, &bump_arr];
        let decimals = mint_decimals(mint_ai)?;
        invoke_signed(
            &spl_token::instruction::transfer_checked(&spl_token::ID, vault_ai.key, mint_ai.key, dest_token.key, swap_ai.key, &[], swap.amount, decimals)?,
            &[vault_ai.clone(), mint_ai.clone(), dest_token.clone(), swap_ai.clone(), token_prog.clone()], &[&signer])?;
        invoke_signed(
            &spl_token::instruction::close_account(&spl_token::ID, vault_ai.key, rent_to.key, swap_ai.key, &[])?,
            &[vault_ai.clone(), rent_to.clone(), swap_ai.clone(), token_prog.clone()], &[&signer])?;
        // Close the state account: lamports to rent_to, wipe data.
        let lamports = **swap_ai.lamports.borrow();
        **swap_ai.lamports.borrow_mut() = 0;
        **rent_to.lamports.borrow_mut() = rent_to.lamports().checked_add(lamports).ok_or(ProgramError::ArithmeticOverflow)?;
        for b in swap_ai.data.borrow_mut().iter_mut() { *b = 0; }
        Ok(())
    }
}
