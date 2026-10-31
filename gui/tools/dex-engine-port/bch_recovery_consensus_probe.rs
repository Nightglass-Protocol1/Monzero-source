//! Isolated fixture adapter. Fixed public test scalars must never protect real funds.
use anyhow::{Context, Result, ensure};
use bitcoin::{consensus::{deserialize, serialize, encode::serialize_hex}, Transaction, ScriptBuf, OutPoint, secp256k1::PublicKey};
use ecdsa_fun::{ECDSA, fun::{Scalar, Point}};
use swap_core::bch::recovery::{RecoveryPlan, RecoveryTerms};
use serde_json::{Value, json};
use std::io::{Read, Write};
fn scalar(tag:u8)->Scalar {Scalar::from_bytes([tag;32]).unwrap().non_zero().unwrap()}
fn public(key:&Scalar)->Point {ECDSA::verify_only().verification_key_for(key)}
fn main()->Result<()> {
    let mut bytes=Vec::new();std::io::stdin().take(4_100_001).read_to_end(&mut bytes)?;
    ensure!(bytes.len()<=4_100_000,"oversized fixture request");let request:Value=serde_json::from_slice(&bytes)?;
    let decode=|v:&Value|->Result<Vec<u8>> {Ok(hex::decode(v.as_str().context("missing public fixture hex")?)?)};
    let bch=scalar(1);let native=scalar(2);let bch_share=scalar(3);let native_share=scalar(4);
    let plan=RecoveryPlan::new(RecoveryTerms {locked_atoms:100_000,lock_fee_atoms:1000,recovery_fee_atoms:2000,
        minimum_output_atoms:546,cancel_delay_blocks:2,punish_delay_blocks:5,
        bch_funder_signing_key:PublicKey::from_slice(&public(&bch).to_bytes())?,
        native_funder_signing_key:PublicKey::from_slice(&public(&native).to_bytes())?,
        bch_funder_refund:ScriptBuf::from_bytes(decode(&request["refundDestination"])?),
        native_funder_payout:ScriptBuf::from_bytes(decode(&request["payoutDestination"])?)})?;
    let mut output=json!({"schema":"monzero-bch-recovery-consensus-fixture-v1","executable":false,
        "lockScriptPubKey":hex::encode(plan.lock().script_pubkey()),"recoveryScriptPubKey":hex::encode(plan.recovery().script_pubkey()),
        "cancelDelay":2,"punishDelay":5,"lockedAtoms":100000,"recoveryAtoms":99000,"finalRecoveryAtoms":97000});
    if !request["funding"].is_null() {
        let raw=decode(&request["funding"])?;let parent:Transaction=deserialize(&raw)?;
        let vout=u32::try_from(request["vout"].as_u64().context("missing vout")?)?;
        let spend=plan.lock_spend_terms(OutPoint{txid:parent.compute_txid(),vout});
        let encrypted=plan.lock().data_message().encrypted_sign(&bch,&public(&native_share));
        let signature=plan.lock().data_message().decrypt(&public(&bch),&native_share,encrypted.clone())?;
        let success=plan.lock().prepare_redemption(&raw,&spend,&signature)?;
        let cancel=plan.lock().prepare_timeout(&raw,&spend)?;
        let cancel_raw=serialize(&cancel);
        let recovery_spend=plan.recovery_spend_terms(OutPoint{txid:cancel.compute_txid(),vout:0});
        let refund_encrypted=plan.recovery().data_message().encrypted_sign(&native,&public(&bch_share));
        let refund_sig=plan.recovery().data_message().decrypt(&public(&native),&bch_share,refund_encrypted.clone())?;
        let refund=plan.recovery().prepare_redemption(&cancel_raw,&recovery_spend,&refund_sig)?;
        let punish=plan.recovery().prepare_timeout(&cancel_raw,&recovery_spend)?;
        for (name,tx) in [("success",&success),("cancel",&cancel),("refund",&refund),("punish",&punish)] {
            output[name]=json!({"hex":serialize_hex(tx),"txid":tx.compute_txid().to_string()});
        }
        if !request["observed"].is_null() {
            let observed=decode(&request["observed"])?;
            let recovered=match request["branch"].as_str() {
                Some("success")=>{
                    let sig=plan.lock().redemption_signature(&raw,&spend,&observed)?;
                    plan.lock().data_message().recover(&public(&bch),&public(&native_share),&encrypted,&sig)?.to_bytes()==native_share.to_bytes()
                },
                Some("refund")=>{
                    let sig=plan.recovery().redemption_signature(&cancel_raw,&recovery_spend,&observed)?;
                    plan.recovery().data_message().recover(&public(&native),&public(&bch_share),&refund_encrypted,&sig)?.to_bytes()==bch_share.to_bytes()
                },
                Some("punish")=>{plan.recovery().verify_timeout(&cancel_raw,&recovery_spend,&observed)?;true},
                _=>anyhow::bail!("unsupported observed branch"),
            };
            ensure!(recovered,"observed fixture adaptor recovery differs");output["observedVerified"]=json!(true);
        }
    }
    std::io::stdout().write_all(&serde_json::to_vec(&output)?)?;Ok(())
}
