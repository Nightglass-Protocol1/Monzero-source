//! Owned-regtest adapter only: public reference terms, no wallet keys or network I/O.
use anyhow::{Result, ensure, Context};
use bitcoin::{consensus::{deserialize, encode::serialize_hex}, Transaction, ScriptBuf, OutPoint, secp256k1::PublicKey};
use swap_core::bch::{covenant::{Covenant, CovenantTerms}, spend::SpendTerms};
use serde_json::{Value, json};
use std::io::{Read, Write};
fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    ensure!(args.len()==3, "expected public covenant and signature fixtures");
    let reference: Value=serde_json::from_slice(&std::fs::read(&args[1])?)?;
    let signature: Value=serde_json::from_slice(&std::fs::read(&args[2])?)?;
    let decode=|value:&Value| -> Result<Vec<u8>> {Ok(hex::decode(value.as_str().context("missing reference hex")?)?)};
    let covenant=Covenant::new(CovenantTerms {fee_atoms:1000,delay_blocks:2,
        signing_key:PublicKey::from_slice(&decode(&reference["publicKey"])?)?,
        redeem_destination:ScriptBuf::from_bytes(decode(&reference["redeemDestination"])?),
        timeout_destination:ScriptBuf::from_bytes(decode(&reference["timeoutDestination"])?)})?;
    ensure!(covenant.redeem_script().as_bytes()==decode(&reference["script"])?
        && covenant.script_pubkey().as_bytes()==decode(&reference["scriptPubKey"])?,"reference covenant differs");
    let mut input=Vec::new();std::io::stdin().take(4_100_001).read_to_end(&mut input)?;
    ensure!(input.len()<=4_100_000,"oversized fixture request");
    let request:Value=serde_json::from_slice(&input)?;
    let mut output=json!({"schema":"monzero-bch-consensus-fixture-v1","executable":false,
        "redeemScript":hex::encode(covenant.redeem_script()),"scriptPubKey":hex::encode(covenant.script_pubkey()),
        "redeemDestination":reference["redeemDestination"],"timeoutDestination":reference["timeoutDestination"],
        "feeAtoms":1000,"delayBlocks":2});
    if !request["funding"].is_null() {
        let raw=decode(&request["funding"])?;let parent:Transaction=deserialize(&raw)?;
        let vout=u32::try_from(request["vout"].as_u64().context("missing vout")?)?;
        let locked_atoms=parent.output.get(vout as usize).context("missing output")?.value.to_sat();
        let terms=SpendTerms {outpoint:OutPoint{txid:parent.compute_txid(),vout},locked_atoms,
            maximum_fee_atoms:1000,minimum_output_atoms:546};
        let redeem=covenant.prepare_redemption(&raw,&terms,&decode(&signature["signature"])?)?;
        let timeout=covenant.prepare_timeout(&raw,&terms)?;
        for (name, tx, wrong_destination) in [
            ("redemption", redeem, covenant.terms().timeout_destination.clone()),
            ("timeout", timeout, covenant.terms().redeem_destination.clone())] {
            let mut wrong_amount=tx.clone();
            wrong_amount.output[0].value=bitcoin::Amount::from_sat(tx.output[0].value.to_sat()+1);
            let mut wrong_script=tx.clone();wrong_script.output[0].script_pubkey=wrong_destination;
            output[name]=json!({"hex":serialize_hex(&tx),"txid":tx.compute_txid().to_string(),
                "invalidAmount":serialize_hex(&wrong_amount),"invalidDestination":serialize_hex(&wrong_script)});
        }
    }
    std::io::stdout().write_all(&serde_json::to_vec(&output)?)?;Ok(())
}
