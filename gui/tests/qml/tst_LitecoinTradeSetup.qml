import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
Item {
    width:1000;height:1000
    property var appWindow: app
    property var translationManager: translations
    QtObject {id:app;property bool themeTransition:false}
    QtObject {id:translations;property string emptyString:""}
    function releaseFocus() {}
    QtObject {
        id:backend
        property int reviews:0
        property var imports:[]
        property bool accepted:true
        function reviewLitecoinTradeSetup(path) {
            reviews++
            return {appPath:path,commitment:Array(65).join("a"),signaturesVerified:true,
                terms:{ltcAtoms:"18446744073709551615"},network:"ltc-regtest",tradeId:"fixture",
                role:"native-funder",peerKey:"peer",peerCertificateSha256:"cert",helper:"/local/helper",helperDigest:"hash"}
        }
        function importLitecoinTradeSetup(path,digest,confirmed) {imports.push({path:path,digest:digest,confirmed:confirmed});return accepted}
    }
    M.LitecoinTradeSetupPanel {id:panel;width:950;backend:backend;market:"LTC"}
    M.LitecoinTradeSetupPanel {id:bitcoinPanel;width:950;backend:backend;market:"BTC";coin:"BTC"}
    TestCase {
        name:"LitecoinTradeSetup";when:windowShown
        function control(name){return findChild(panel,name)}
        function init(){panel.market="LTC";panel.appStatus=({});panel.clearReview();panel.notice="";control("tradeSetupPath").text="/local/app.json";backend.imports=[];backend.reviews=0;backend.accepted=true}
        function test_noAutomaticActions(){compare(backend.reviews,0);compare(backend.imports.length,0);verify(!panel.ready)}
        function test_requiresReviewAndConsent(){panel.openSetup();compare(backend.imports.length,0);panel.reviewSetup();panel.openSetup();compare(backend.imports.length,0);control("tradeSetupConsent").checked=true;panel.openSetup();compare(backend.imports.length,1);verify(backend.imports[0].confirmed);verify(!panel.ready);panel.openSetup();compare(backend.imports.length,1)}
        function test_pathChangeInvalidates(){panel.reviewSetup();control("tradeSetupConsent").checked=true;control("tradeSetupPath").text="/another/app.json";verify(!panel.ready);panel.openSetup();compare(backend.imports.length,0)}
        function test_marketChangeInvalidates(){panel.reviewSetup();control("tradeSetupConsent").checked=true;panel.market="BTC";verify(!panel.ready);verify(!control("tradeSetupConsent").checked)}
        function test_atomicPrecisionAndPlainText(){panel.reviewSetup();verify(control("tradeSetupDetails").text.indexOf("18446744073709551615")>=0);compare(control("tradeSetupDetails").textFormat,Text.PlainText)}
        function test_bitcoinStageUsesBitcoinLabels(){verify(bitcoinPanel.visible);compare(bitcoinPanel.coinName,"Bitcoin");compare(bitcoinPanel.chainText("Open a prepared LTC/XMZ trade"),"Open a prepared BTC/XMZ trade");compare(bitcoinPanel.chainText("Litecoin lock"),"Bitcoin lock");compare(panel.chainText("Litecoin lock"),"Litecoin lock");bitcoinPanel.market="LTC";verify(!bitcoinPanel.visible);bitcoinPanel.market="BTC"}
        function test_failureConsumesConsent(){panel.reviewSetup();control("tradeSetupConsent").checked=true;backend.accepted=false;panel.openSetup();verify(panel.notice.length>0);verify(!panel.ready);verify(!control("tradeSetupConsent").checked)}
    }
}
