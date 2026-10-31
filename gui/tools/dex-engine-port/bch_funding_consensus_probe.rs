//! Owned-node preparation/signature-verification fixture. No lock, signing or relay RPC.
use anyhow::{Context, Result, ensure};
use bitcoin::{BlockHash, ScriptBuf, secp256k1::PublicKey};
use swap_core::bch::{covenant::{Covenant,CovenantTerms}, funding::FundingPreview};
use swap_machine::bch_rpc::{BchNetwork,BchRpc,preparation::FundingPreparationRequest};
use serde_json::{Value,json};
use std::{io::{Read,Write},str::FromStr};
fn string<'a>(v:&'a Value,key:&str)->Result<&'a str>{v[key].as_str().context("missing fixture string")}
fn main()->Result<()> {
    let mut bytes=Vec::new();std::io::stdin().take(1_000_001).read_to_end(&mut bytes)?;
    ensure!(bytes.len()<=1_000_000,"oversized fixture request");let r:Value=serde_json::from_slice(&bytes)?;
    let reference=&r["reference"];
    let covenant=Covenant::new(CovenantTerms {fee_atoms:1000,delay_blocks:2,
        signing_key:PublicKey::from_slice(&hex::decode(string(reference,"publicKey")?)?)?,
        redeem_destination:ScriptBuf::from_bytes(hex::decode(string(reference,"redeemDestination")?)?),
        timeout_destination:ScriptBuf::from_bytes(hex::decode(string(reference,"timeoutDestination")?)?)})?;
    let change=ScriptBuf::from_bytes(hex::decode(string(&r,"changeScript")?)?);
    let maximum=r["maximumFee"].as_u64().context("missing fee cap")?;
    let output=if r["action"]=="prepare" {
        let (user,password)=string(&r,"cookie")?.split_once(':').context("invalid fixture cookie")?;
        let rpc=BchRpc::new(u16::try_from(r["port"].as_u64().context("missing port")?)?,user.into(),password.into(),
            BchNetwork::Regtest {checkpoint_height:1,checkpoint:BlockHash::from_str(string(&r,"checkpoint")?)?},
            "fixture".into(),string(&r,"anchor")?.into(),ScriptBuf::from_bytes(hex::decode(string(&r,"anchorScript")?)?),
            string(&r,"subversion")?.into())?;
        let candidate=tokio::runtime::Builder::new_current_thread().enable_all().build()?.block_on(
            rpc.prepare_funding(FundingPreparationRequest {covenant:&covenant,principal:100_000,
                change_address:string(&r,"changeAddress")?,change_script:&change,fee_rate_atoms_per_kb:1000,
                maximum_fee:maximum,minimum_change:546,minimum_confirmations:2}))?;
        json!({"schema":"monzero-bch-funding-consensus-fixture-v1","executable":false,
            "unsigned":hex::encode(candidate.unsigned()),"sources":candidate.sources().iter().map(hex::encode).collect::<Vec<_>>(),
            "feeAtoms":candidate.preview().fee().to_sat(),"lockVout":candidate.preview().lock_vout(),
            "tip":candidate.observation().tip().to_string(),"inputs":candidate.preview().transaction().input.iter()
                .map(|i|json!({"txid":i.previous_output.txid.to_string(),"vout":i.previous_output.vout})).collect::<Vec<_>>()})
    } else {
        ensure!(r["action"]=="verify-signed","unsupported fixture action");
        let raw=hex::decode(string(&r,"unsigned")?)?;
        let sources:Vec<Vec<u8>>=r["sources"].as_array().context("missing sources")?.iter()
            .map(|s|Ok(hex::decode(s.as_str().context("invalid source")?)?)).collect::<Result<_>>()?;
        let refs:Vec<&[u8]>=sources.iter().map(Vec::as_slice).collect();
        let preview=FundingPreview::verify(&raw,&refs,&covenant,100_000,&change,maximum,546)?;
        let signed=preview.verify_signed(&hex::decode(string(&r,"signed")?)?)?;
        json!({"schema":"monzero-bch-funding-consensus-fixture-v1","executable":false,
            "signedVerified":true,"txid":signed.lock_outpoint().txid.to_string(),"lockVout":signed.lock_outpoint().vout})
    };
    std::io::stdout().write_all(&serde_json::to_vec(&output)?)?;Ok(())
}
