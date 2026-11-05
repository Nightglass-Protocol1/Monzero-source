//! Cross-language reference vector: the SolSwapClient (JS) must derive the same PDA and
//! encode the same `New` instruction bytes, or funds would be stranded. Fixed inputs.
use solana_sdk::pubkey::Pubkey;
#[test]
fn reference_vector() {
    let pid = Pubkey::new_from_array([1u8; 32]);
    let owner = Pubkey::new_from_array([2u8; 32]);
    let claimer = Pubkey::new_from_array([3u8; 32]);
    let cc = [0x11u8; 32];
    let rc = [0x22u8; 32];
    let (amount, nonce): (u64, u64) = (2_000_000_000, 1);
    let (pda, bump) = Pubkey::find_program_address(
        &[b"swap", owner.as_ref(), claimer.as_ref(), &cc, &rc, &amount.to_le_bytes(), &nonce.to_le_bytes()], &pid);
    let mut data = vec![0u8];
    data.extend_from_slice(&cc); data.extend_from_slice(&rc);
    data.extend_from_slice(&100u64.to_le_bytes()); data.extend_from_slice(&100u64.to_le_bytes());
    data.extend_from_slice(&amount.to_le_bytes()); data.extend_from_slice(&nonce.to_le_bytes());
    println!("VECTOR pid={pid} owner={owner} claimer={claimer}");
    println!("VECTOR pda={pda} bump={bump}");
    let (vault, vbump) = Pubkey::find_program_address(&[b"vault", pda.as_ref()], &pid);
    println!("VECTOR vault={vault} vbump={vbump}");
    println!("VECTOR new_data_hex={}", data.iter().map(|b| format!("{:02x}", b)).collect::<String>());
}

#[test]
fn secret_vector() {
    use curve25519_dalek::{edwards::EdwardsPoint, scalar::Scalar};
    let mut sb = [7u8; 32]; sb[31] = 0;
    let s = Scalar::from_bytes_mod_order(sb);
    let secret = s.to_bytes();
    let commitment = EdwardsPoint::mul_base(&s).compress().to_bytes();
    let hex = |b: &[u8]| b.iter().map(|x| format!("{:02x}", x)).collect::<String>();
    println!("VECTOR secret_hex={}", hex(&secret));
    println!("VECTOR commitment_hex={}", hex(&commitment));
}
