//! Read-only adapter for a caller-owned isolated BCHN fixture; no relay operations.
use anyhow::{Context, Result, ensure};
use bitcoin::{BlockHash, Txid, ScriptBuf, consensus::encode::serialize_hex};
use swap_machine::bch_rpc::{BchNetwork, BchRpc};
use serde_json::{Value, json};
use std::{io::{Read, Write}, str::FromStr};
fn string<'a>(v:&'a Value,key:&str)->Result<&'a str>{v[key].as_str().context("missing fixture string")}
fn main()->Result<()> {
    let mut bytes=Vec::new();std::io::stdin().take(500_001).read_to_end(&mut bytes)?;
    ensure!(bytes.len()<=500_000,"oversized fixture request");let request:Value=serde_json::from_slice(&bytes)?;
    let (user,password)=string(&request,"cookie")?.split_once(':').context("invalid fixture cookie")?;
    let rpc=BchRpc::new(u16::try_from(request["port"].as_u64().context("missing port")?)?,
        user.into(),password.into(),BchNetwork::Regtest {checkpoint_height:1,
            checkpoint:BlockHash::from_str(string(&request,"checkpoint")?)?},"fixture".into(),
        string(&request,"anchor")?.into(),ScriptBuf::from_bytes(hex::decode(string(&request,"anchorScript")?)?),
        string(&request,"subversion")?.into())?;
    let txid=Txid::from_str(string(&request,"txid")?)?;
    let block=BlockHash::from_str(string(&request,"block")?)?;
    let minimum=request["minimum"].as_u64().context("missing minimum")?;
    let vout=u32::try_from(request["vout"].as_u64().context("missing vout")?)?;
    let mut result=json!({"schema":"monzero-bch-observation-fixture-v1","executable":false,
        "confirmed":false,"unspent":false,"refundSafe":false,"punishMature":false});
    tokio::runtime::Builder::new_current_thread().enable_all().build()?.block_on(async {
        let observed=if request["indexed"]==true {rpc.indexed_confirmed_transaction(txid,minimum).await}
            else {rpc.confirmed_transaction(txid,block,minimum).await};
        if let Ok(parent)=observed {
            if serialize_hex(parent.transaction())!=string(&request,"expectedHex")? {return Ok::<(),anyhow::Error>(());}
            result["confirmed"]=json!(true);result["confirmations"]=json!(parent.confirmations());
            result["block"]=json!(parent.block().to_string());result["tip"]=json!(parent.tip().to_string());
            if let Ok(output)=rpc.confirmed_unspent_output(&parent,vout).await {
                result["unspent"]=json!(true);
                result["refundSafe"]=json!(output.require_refund_window(1,5,1).is_ok());
                result["punishMature"]=json!(output.require_punish_maturity(1,5).is_ok());
            }
        }
        Ok(())
    })?;
    std::io::stdout().write_all(&serde_json::to_vec(&result)?)?;Ok(())
}
