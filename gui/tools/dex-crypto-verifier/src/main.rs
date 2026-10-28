use base64::Engine as _;
use bincode::Options as _;
use curve25519_dalek::edwards::CompressedEdwardsY;
use ecdsa_fun::adaptor::{Adaptor, EncryptedSignature, HashTranscript};
use ecdsa_fun::fun::Point;
use rand_chacha::ChaCha20Rng;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use sigma_fun::ext::dl_secp256k1_ed25519_eq::{CrossCurveDLEQ, CrossCurveDLEQProof};
use sigma_fun::HashTranscript as SigmaHashTranscript;
use std::io::{self, Read};
use std::str::FromStr;

const IMPLEMENTATION: &str = "comit-dc6ba84bbb1fe5ecc69581fec7dd8529567c4e32:ecdsa_fun-0.10.0:sigma_fun-0.7.0:bincode-1.3.3";
const MAX_REQUEST: u64 = 512 * 1024;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Common {
    version: u8,
    implementation: String,
    operation: String,
    request_id: String,
    request_commitment: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct AdaptorRequest {
    version: u8,
    implementation: String,
    operation: String,
    request_id: String,
    request_commitment: String,
    digest_hex: String,
    signing_public_key_hex: String,
    encryption_public_key_hex: String,
    encrypted_signature_hex: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct DleqRequest {
    version: u8,
    implementation: String,
    operation: String,
    request_id: String,
    request_commitment: String,
    secp256k1_public_key_hex: String,
    ed25519_public_key_hex: String,
    proof_base64: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Response<'a> {
    version: u8,
    implementation: &'a str,
    operation: &'a str,
    request_id: &'a str,
    request_commitment: &'a str,
    verified: bool,
    error: &'a str,
    executable: bool,
}

fn decode_exact<const N: usize>(text: &str) -> Option<[u8; N]> {
    if text.len() != N * 2 || text.bytes().any(|b| !matches!(b, b'0'..=b'9' | b'a'..=b'f')) {
        return None;
    }
    let bytes = hex::decode(text).ok()?;
    bytes.try_into().ok()
}

fn field(hash: &mut Sha256, value: &[u8]) {
    hash.update(u32::try_from(value.len()).unwrap_or(u32::MAX).to_be_bytes());
    hash.update(value);
}

fn commitment(operation: &str, request_id: &str, values: &[&[u8]]) -> String {
    let mut hash = Sha256::new();
    hash.update(b"MONZERO-DEX-CRYPTO-REQUEST-V1\n");
    field(&mut hash, IMPLEMENTATION.as_bytes());
    field(&mut hash, operation.as_bytes());
    field(&mut hash, request_id.as_bytes());
    for value in values { field(&mut hash, value); }
    hex::encode(hash.finalize())
}

fn valid_common(version: u8, implementation: &str, operation: &str,
                request_id: &str, claimed: &str) -> bool {
    version == 1 && implementation == IMPLEMENTATION
        && matches!(operation, "verify_ecdsa_adaptor" | "verify_cross_curve_dleq")
        && decode_exact::<32>(request_id).is_some() && decode_exact::<32>(claimed).is_some()
}

fn verify_adaptor(request: &AdaptorRequest) -> Result<bool, &'static str> {
    if !valid_common(request.version, &request.implementation, &request.operation,
                     &request.request_id, &request.request_commitment)
        || request.operation != "verify_ecdsa_adaptor" {
        return Err("invalid_request");
    }
    let digest = decode_exact::<32>(&request.digest_hex).ok_or("invalid_digest")?;
    let signing_bytes = decode_exact::<33>(&request.signing_public_key_hex)
        .ok_or("invalid_signing_key")?;
    let encryption_bytes = decode_exact::<33>(&request.encryption_public_key_hex)
        .ok_or("invalid_encryption_key")?;
    let encrypted_bytes = decode_exact::<162>(&request.encrypted_signature_hex)
        .ok_or("invalid_adaptor_signature")?;
    if signing_bytes == encryption_bytes { return Err("duplicate_keys"); }
    let expected = commitment(&request.operation, &request.request_id,
        &[&digest, &signing_bytes, &encryption_bytes, &encrypted_bytes]);
    if expected != request.request_commitment { return Err("commitment_mismatch"); }
    let signing_key: Point = Point::from_str(&request.signing_public_key_hex)
        .map_err(|_| "invalid_signing_key")?;
    let encryption_key: Point = Point::from_str(&request.encryption_public_key_hex)
        .map_err(|_| "invalid_encryption_key")?;
    let encrypted_signature = EncryptedSignature::from_str(&request.encrypted_signature_hex)
        .map_err(|_| "invalid_adaptor_signature")?;
    let adaptor = Adaptor::<HashTranscript<Sha256>, _>::verify_only();
    Ok(adaptor.verify_encrypted_signature(
        &signing_key, &encryption_key, &digest, &encrypted_signature))
}

fn verify_dleq(request: &DleqRequest) -> Result<bool, &'static str> {
    if !valid_common(request.version, &request.implementation, &request.operation,
                     &request.request_id, &request.request_commitment)
        || request.operation != "verify_cross_curve_dleq" {
        return Err("invalid_request");
    }
    let secp_bytes = decode_exact::<33>(&request.secp256k1_public_key_hex)
        .ok_or("invalid_secp256k1_key")?;
    let ed_bytes = decode_exact::<32>(&request.ed25519_public_key_hex)
        .ok_or("invalid_ed25519_key")?;
    let proof_bytes = base64::engine::general_purpose::STANDARD
        .decode(&request.proof_base64).map_err(|_| "invalid_dleq_proof")?;
    if proof_bytes.is_empty() || proof_bytes.len() > 256 * 1024 {
        return Err("invalid_dleq_proof");
    }
    let expected = commitment(&request.operation, &request.request_id,
        &[&secp_bytes, &ed_bytes, &proof_bytes]);
    if expected != request.request_commitment { return Err("commitment_mismatch"); }
    let secp_key: Point = Point::from_str(&request.secp256k1_public_key_hex)
        .map_err(|_| "invalid_secp256k1_key")?;
    let ed_key = CompressedEdwardsY(ed_bytes).decompress().ok_or("invalid_ed25519_key")?;
    if !ed_key.is_torsion_free() { return Err("invalid_ed25519_key"); }
    let proof: CrossCurveDLEQProof = bincode::DefaultOptions::new()
        .with_fixint_encoding().reject_trailing_bytes()
        .deserialize(&proof_bytes).map_err(|_| "invalid_dleq_proof")?;
    type Transcript = SigmaHashTranscript<Sha256, ChaCha20Rng>;
    let verifier = CrossCurveDLEQ::<Transcript>::new(
        (*ecdsa_fun::fun::G).normalize(),
        curve25519_dalek::constants::ED25519_BASEPOINT_POINT);
    Ok(verifier.verify(&proof, (secp_key, ed_key)))
}

fn main() {
    let mut input = Vec::new();
    if io::stdin().take(MAX_REQUEST + 1).read_to_end(&mut input).is_err()
        || input.is_empty() || input.len() as u64 > MAX_REQUEST {
        std::process::exit(2);
    }
    let value: serde_json::Value = match serde_json::from_slice(&input) {
        Ok(value) => value,
        Err(_) => std::process::exit(2),
    };
    let common: Common = match serde_json::from_value(value.clone()) {
        Ok(common) => common,
        Err(_) => {
            // Common intentionally rejects operation-specific fields. Extract the
            // response binding without accepting it as a complete request.
            let object = match value.as_object() { Some(object) => object, None => std::process::exit(2) };
            Common {
                version: object.get("version").and_then(|v| v.as_u64()).unwrap_or(0) as u8,
                implementation: object.get("implementation").and_then(|v| v.as_str()).unwrap_or("").to_owned(),
                operation: object.get("operation").and_then(|v| v.as_str()).unwrap_or("").to_owned(),
                request_id: object.get("requestId").and_then(|v| v.as_str()).unwrap_or("").to_owned(),
                request_commitment: object.get("requestCommitment").and_then(|v| v.as_str()).unwrap_or("").to_owned(),
            }
        }
    };
    if !valid_common(common.version, &common.implementation, &common.operation,
                     &common.request_id, &common.request_commitment) {
        std::process::exit(2);
    }
    let result = match common.operation.as_str() {
        "verify_ecdsa_adaptor" => serde_json::from_value::<AdaptorRequest>(value)
            .map_err(|_| "invalid_request").and_then(|request| verify_adaptor(&request)),
        "verify_cross_curve_dleq" => serde_json::from_value::<DleqRequest>(value)
            .map_err(|_| "invalid_request").and_then(|request| verify_dleq(&request)),
        _ => Err("invalid_operation"),
    };
    let (verified, error) = match result {
        Ok(true) => (true, ""),
        Ok(false) => (false, "verification_failed"),
        Err(error) => (false, error),
    };
    let response = Response { version: 1, implementation: IMPLEMENTATION,
        operation: &common.operation, request_id: &common.request_id,
        request_commitment: &common.request_commitment, verified, error, executable: false };
    if serde_json::to_writer(io::stdout(), &response).is_err() { std::process::exit(3); }
}

#[cfg(test)]
mod tests {
    use super::*;
    use curve25519_dalek::scalar::Scalar as EdScalar;
    use ecdsa_fun::fun::Scalar;
    use ecdsa_fun::nonce::Deterministic;
    use ecdsa_fun::ECDSA;
    use rand_core::SeedableRng;

    fn adaptor_request() -> AdaptorRequest {
        let mut rng = ChaCha20Rng::from_seed([7u8; 32]);
        let signing_secret = Scalar::random(&mut rng);
        let encryption_secret = Scalar::random(&mut rng);
        let ecdsa = ECDSA::<()>::default();
        let signing_key = ecdsa.verification_key_for(&signing_secret);
        let encryption_key = ecdsa.verification_key_for(&encryption_secret);
        let digest = [9u8; 32];
        let signer = Adaptor::<HashTranscript<Sha256, ChaCha20Rng>,
                             Deterministic<Sha256>>::default();
        let encrypted = signer.encrypted_sign(&signing_secret, &encryption_key, &digest);
        let request_id = hex::encode([1u8; 32]);
        let signing_bytes = signing_key.to_bytes();
        let encryption_bytes = encryption_key.to_bytes();
        let encrypted_text = encrypted.to_string();
        let encrypted_bytes = hex::decode(&encrypted_text).unwrap();
        let request_commitment = commitment("verify_ecdsa_adaptor", &request_id,
            &[&digest, &signing_bytes, &encryption_bytes, &encrypted_bytes]);
        AdaptorRequest { version: 1, implementation: IMPLEMENTATION.to_owned(),
            operation: "verify_ecdsa_adaptor".to_owned(), request_id, request_commitment,
            digest_hex: hex::encode(digest), signing_public_key_hex: signing_key.to_string(),
            encryption_public_key_hex: encryption_key.to_string(),
            encrypted_signature_hex: encrypted_text }
    }

    #[test]
    fn verifies_and_binds_adaptor_signature() {
        let mut request = adaptor_request();
        assert_eq!(request.encrypted_signature_hex.len(), 324);
        assert_eq!(verify_adaptor(&request), Ok(true));
        request.digest_hex.replace_range(0..2, "08");
        assert_eq!(verify_adaptor(&request), Err("commitment_mismatch"));
    }

    fn published_adaptor_request(encryption_key: &str, adaptor_sig: &str) -> AdaptorRequest {
        // ecdsa_fun 0.10.0 tests/test_vectors.json (DLC adaptor vectors).
        let request_id = hex::encode([4u8; 32]);
        let digest_hex = "8131e6f4b45754f2c90bd06688ceeabc0c45055460729928b4eecf11026a9e2d";
        let signing_key = "035be5e9478209674a96e60f1f037f6176540fd001fa1d64694770c56a7709c42c";
        let digest = hex::decode(digest_hex).unwrap();
        let signing = hex::decode(signing_key).unwrap();
        let encryption = hex::decode(encryption_key).unwrap();
        let encrypted = hex::decode(adaptor_sig).unwrap();
        let request_commitment = commitment("verify_ecdsa_adaptor", &request_id,
            &[&digest, &signing, &encryption, &encrypted]);
        AdaptorRequest { version: 1, implementation: IMPLEMENTATION.to_owned(),
            operation: "verify_ecdsa_adaptor".to_owned(), request_id, request_commitment,
            digest_hex: digest_hex.to_owned(), signing_public_key_hex: signing_key.to_owned(),
            encryption_public_key_hex: encryption_key.to_owned(),
            encrypted_signature_hex: adaptor_sig.to_owned() }
    }

    #[test]
    fn matches_published_dlc_adaptor_vectors() {
        let valid = published_adaptor_request(
            "02c2662c97488b07b6e819124b8989849206334a4c2fbdf691f7b34d2b16e9c293",
            concat!("03424d14a5471c048ab87b3b83f6085d125d5864249ae4297a57c84e74710bb673",
            "0223f325042fce535d040fee52ec13231bf709ccd84233c6944b90317e62528b25",
            "27dff9d659a96db4c99f9750168308633c1867b70f3a18fb0f4539a1aecedcd1f",
            "c0148fc22f36b6303083ece3f872b18e35d368b3958efe5fb081f7716736ccb598",
            "d269aa3084d57e1855e1ea9a45efc10463bbf32ae378029f5763ceb40173f"));
        assert_eq!(verify_adaptor(&valid), Ok(true));

        let invalid_proof = published_adaptor_request(
            "0214ccb756249ad6e733c80285ea7ac2ee12ffebbcee4e556e6810793a60c45ad4",
            concat!("03f94dca206d7582c015fb9bffe4e43b14591b30ef7d2b464d103ec5e116595dba",
            "03127f8ac3533d249280332474339000922eb6a58e3b9bf4fc7e01e4b4df2b7a4",
            "100a1e089f16e5d70bb89f961516f1de0684cc79db978495df2f399b0d01ed724",
            "0fa6e3252aedb58bdc6b5877b0c602628a235dd1ccaebdddcbe96198c0c21bead",
            "7b05f423b673d14d206fa1507b2dbe2722af792b8c266fc25a2d901d7e2c335"));
        assert_eq!(verify_adaptor(&invalid_proof), Ok(false));
    }

    fn dleq_request() -> DleqRequest {
        let mut rng = ChaCha20Rng::from_seed([11u8; 32]);
        let secret = EdScalar::from_bytes_mod_order([3u8; 32]);
        type Transcript = SigmaHashTranscript<Sha256, ChaCha20Rng>;
        let system = CrossCurveDLEQ::<Transcript>::new(
            (*ecdsa_fun::fun::G).normalize(),
            curve25519_dalek::constants::ED25519_BASEPOINT_POINT);
        let (proof, (secp_key, ed_key)) = system.prove(&secret, &mut rng);
        let proof_bytes = bincode::serialize(&proof).unwrap();
        let secp_bytes = secp_key.to_bytes();
        let ed_bytes = ed_key.compress().to_bytes();
        let request_id = hex::encode([2u8; 32]);
        let request_commitment = commitment("verify_cross_curve_dleq", &request_id,
            &[&secp_bytes, &ed_bytes, &proof_bytes]);
        DleqRequest { version: 1, implementation: IMPLEMENTATION.to_owned(),
            operation: "verify_cross_curve_dleq".to_owned(), request_id, request_commitment,
            secp256k1_public_key_hex: secp_key.to_string(),
            ed25519_public_key_hex: hex::encode(ed_bytes),
            proof_base64: base64::engine::general_purpose::STANDARD.encode(proof_bytes) }
    }

    #[test]
    fn verifies_and_binds_cross_curve_dleq() {
        let mut request = dleq_request();
        assert_eq!(verify_dleq(&request), Ok(true));
        request.ed25519_public_key_hex.replace_range(0..2, "04");
        assert_eq!(verify_dleq(&request), Err("commitment_mismatch"));
    }

    #[test]
    fn emits_local_cross_curve_regression_fixture() {
        // Public, deterministic test material, NOT an independent review vector.
        // Keep generation behind cfg(test); the production verifier cannot prove/sign.
        let request = dleq_request();
        assert_eq!(verify_dleq(&request), Ok(true));
        println!("LOCAL_DLEQ_FIXTURE={}", serde_json::json!({
            "provenance": "local deterministic sigma_fun 0.7.0 regression; not independent",
            "secp256k1PublicKeyHex": request.secp256k1_public_key_hex,
            "ed25519PublicKeyHex": request.ed25519_public_key_hex,
            "proofBase64": request.proof_base64
        }));
    }

    #[test]
    fn rejects_trailing_dleq_bytes() {
        let mut request = dleq_request();
        let mut proof = base64::engine::general_purpose::STANDARD
            .decode(&request.proof_base64).unwrap();
        proof.push(0);
        let secp = hex::decode(&request.secp256k1_public_key_hex).unwrap();
        let ed = hex::decode(&request.ed25519_public_key_hex).unwrap();
        request.request_commitment = commitment(&request.operation, &request.request_id,
            &[&secp, &ed, &proof]);
        request.proof_base64 = base64::engine::general_purpose::STANDARD.encode(proof);
        assert_eq!(verify_dleq(&request), Err("invalid_dleq_proof"));
    }
}
