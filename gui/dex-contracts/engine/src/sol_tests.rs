use super::*;

#[test]
fn base58_round_trip_and_known_addresses() {
    assert_eq!(to_base58(&SYSTEM_PROGRAM), "11111111111111111111111111111111");
    let devnet = from_base58("9idXkjjEeyu6nLrYSnCcX6PjmRemKQvuz7VwwwYMtihh").unwrap();
    assert_eq!(to_base58(&devnet), "9idXkjjEeyu6nLrYSnCcX6PjmRemKQvuz7VwwwYMtihh");
    assert!(from_base58("not an address").is_err());
    assert!(from_base58("1111").is_err());
}

#[test]
fn compact_lengths_round_trip() {
    for n in [0usize, 1, 127, 128, 255, 300, 16383] {
        let mut out = Vec::new();
        compact(n, &mut out);
        let mut at = 0;
        assert_eq!(read_compact(&out, &mut at), Some(n));
        assert_eq!(at, out.len());
    }
}

#[test]
fn program_addresses_are_off_the_curve_and_depend_on_every_seed() {
    let program = from_base58("9idXkjjEeyu6nLrYSnCcX6PjmRemKQvuz7VwwwYMtihh").unwrap();
    let b = SolBinding { owner: [1; 32], claimer: [2; 32], claim_point: [3; 32], refund_point: [4; 32] };
    let a = b.swap_address(&program, 1_000_000, 7).unwrap();
    assert!(CompressedEdwardsY(a).decompress().is_none());
    assert_eq!(b.swap_address(&program, 1_000_000, 7), Some(a));
    assert_ne!(b.swap_address(&program, 1_000_001, 7), Some(a));
    assert_ne!(b.swap_address(&program, 1_000_000, 8), Some(a));
    assert_ne!(SolBinding { claimer: [9; 32], ..b }.swap_address(&program, 1_000_000, 7), Some(a));
}

#[test]
fn a_transaction_is_signed_by_the_account_and_its_blockhash_can_be_found() {
    let key = account_key(&[7; 32]);
    let me = key.verifying_key().to_bytes();
    let program = [9u8; 32];
    let m = message(&me, &program, &[meta(me, true, true), meta([5; 32], false, false), meta([6; 32], false, true)], &[1, 2, 3], &[0xAB; 32]);
    // 1 signer, no read-only signer, 2 read-only accounts (one account and the program)
    assert_eq!(&m[..3], &[1, 0, 2]);
    // signer, writable, read-only, program
    assert_eq!(m[3], 4);
    assert_eq!(&m[4..36], &me);
    assert_eq!(&m[36..68], &[6u8; 32]);
    assert_eq!(&m[68..100], &[5u8; 32]);
    assert_eq!(&m[100..132], &program);
    let raw = signed(&key, m.clone());
    let (signature, at) = parts(&raw).unwrap();
    assert_eq!(&raw[at..at + 32], &[0xAB; 32]);
    use ed25519_dalek::Verifier;
    key.verifying_key().verify(&m, &ed25519_dalek::Signature::from_bytes(&signature)).unwrap();
    // The instruction: program index 3, accounts [0, 2, 1], data
    assert_eq!(&raw[at + 32..], &[1, 3, 3, 0, 2, 1, 3, 1, 2, 3]);
}

#[test]
fn the_stored_swap_is_read_as_the_program_writes_it() {
    let mut data = vec![0u8; SWAP_LEN];
    data[0..32].copy_from_slice(&[1; 32]);
    data[32..64].copy_from_slice(&[2; 32]);
    data[64..96].copy_from_slice(&[3; 32]);
    data[96..128].copy_from_slice(&[4; 32]);
    data[128..136].copy_from_slice(&1_800_000_000u64.to_le_bytes());
    data[136..144].copy_from_slice(&1_800_007_200u64.to_le_bytes());
    data[144..152].copy_from_slice(&5_000_000u64.to_le_bytes());
    data[152] = 2;
    data[188..196].copy_from_slice(&42u64.to_le_bytes());
    let s = stored(&data).unwrap();
    assert_eq!((s.owner, s.claimer, s.claim_point, s.refund_point), ([1; 32], [2; 32], [3; 32], [4; 32]));
    assert_eq!((s.timeout1, s.timeout2, s.amount, s.stage, s.native, s.nonce), (1_800_000_000, 1_800_007_200, 5_000_000, 2, true, 42));
    data[186] = 1;
    assert!(!stored(&data).unwrap().native);
    // A closed swap is all zeroes
    assert!(stored(&vec![0u8; SWAP_LEN]).is_none());
    assert!(stored(&data[..100]).is_none());
}

#[test]
fn the_program_secret_is_the_xmz_share_little_endian() {
    let share = SecretShare::from_ed25519_le({
        let mut k = [0u8; 32];
        k[0] = 5;
        k
    })
    .unwrap();
    assert_eq!(program_secret(share.to_evm()), share.to_ed25519_le());
    // ...and the point the program checks it against is the share's XMZ public key
    let point = (curve25519_dalek::Scalar::from(5u8) * curve25519_dalek::constants::ED25519_BASEPOINT_POINT).compress().to_bytes();
    assert_eq!(share.ed25519_public(), point);
}
