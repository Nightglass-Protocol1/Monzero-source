import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
Item {
 width:900;height:800
 property var appWindow: app
 property var translationManager: translations
 function releaseFocus() {}
 QtObject { id:app; property bool themeTransition:false }
 QtObject { id:translations; property string emptyString:"" }
 QtObject {
  id:wallet
  property int nettype:0
  property bool synced:true
  property var saved:({})
  property string signed:""
  function publicProfileSnapshot(){return {address:"F"+Array(95).join("1"),balance:"123456789012345678",height:"12000",synchronized:synced};}
  function getCacheAttribute(k){return saved[k]||"";}
  function setCacheAttribute(k,v){saved[k]=v;return true;}
  function signMessage(m){signed=m;return "SigV2fixture";}
  function storeAsync(cb){cb(true);}
 }
 M.PublicWalletProfile { id:profile; width:850; wallet:wallet }
 TestCase {
  name:"PublicWalletPrivacy";when:windowShown
  property var calls:[]
  function init(){wallet.saved={};wallet.nettype=0;wallet.synced=true;wallet.signed="";profile.wallet=null;profile.wallet=wallet;profile.busy=false;calls=[];profile.transport=function(action,body,callback){calls.push({action:action,body:body,callback:callback});};}
  function test_no_request_on_open(){compare(calls.length,0);compare(wallet.signed,"");}
  function test_remove_requires_ack(){profile.submit(true);compare(calls.length,1);compare(wallet.getCacheAttribute("monzero.publicProfile.mode"),"");calls[0].callback({nonce:Array(65).join("a"),expires:Math.floor(Date.now()/1000)+300,revision:0});compare(calls.length,2);verify(wallet.signed.indexOf("Mode:private\nBalance:-\nHeight:-\nName:\n")>0);compare(wallet.getCacheAttribute("monzero.publicProfile.mode"),"");calls[1].callback({ok:true,mode:"private"});compare(wallet.getCacheAttribute("monzero.publicProfile.mode"),"private");verify(profile.notice.indexOf("confirmed")>=0);}
  function test_testnet_never_publishes(){wallet.nettype=1;profile.submit(true);compare(calls.length,0);}
  function test_invalid_challenge_never_signs(){profile.submit(true);var rejected=false;try{calls[0].callback({nonce:"wrong",expires:0,revision:0});}catch(e){rejected=true;}verify(rejected);compare(wallet.signed,"");compare(calls.length,1);}
  function test_balance_snapshot_precision(){wallet.saved={"monzero.publicProfile.mode":"balance"};profile.wallet=null;profile.wallet=wallet;profile.submit(false);compare(calls[0].body.balance,"123456789012345678");}
  function test_unsynchronized_balance_not_sent(){wallet.saved={"monzero.publicProfile.mode":"balance"};profile.wallet=null;profile.wallet=wallet;wallet.synced=false;profile.submit(false);compare(calls.length,0);}
  function test_address_only_no_balance(){wallet.saved={"monzero.publicProfile.mode":"address"};profile.wallet=null;profile.wallet=wallet;profile.submit(false);compare(calls[0].body.balance,"-");compare(calls[0].body.height,"-");}
 }
}
