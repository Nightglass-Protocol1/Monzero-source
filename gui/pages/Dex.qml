// Copyright (c) 2026, The Monzero Project
// Distributed under the BSD 3-Clause license.
import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "../components" as M

Rectangle {
    id: root
    color: "transparent"
    property alias dexHeight: page.height
    property string baseCoin: "BTC"
    property string pairName: baseCoin + " / XMZ"
    property real lastPrice: 1172500
    property real amountValue: Number(amountInput.text)
    property real priceValue: Number(priceInput.text)
    property real totalValue: amountValue * priceValue
    property real tradingFeeValue: totalValue * 0.001
    property real networkFeeXMZ: 0.0025
    property string externalMinerFee: "0.000020 BTC"
    property bool limitOrder: true
    property string previewError: ""
    // Release builds have no dexEngine; the hidden page must not call it.
    property var readiness: dexPrototypeEnabled ? dexEngine.activationReadiness(pairName, {})
        : ({ready:false, checksPassed:0, checksRequired:0, blockers:[]})
    property var marketSnapshot: ({})
    Connections {
        target: dexPrototypeEnabled ? dexEngine : null
        onResearchMarketReady: root.marketSnapshot = snapshot
    }

    ListModel {
        id: asks
        ListElement { p:"0.08512"; a:"8,420" } ListElement { p:"0.08491"; a:"3,870" }
        ListElement { p:"0.08475"; a:"12,530" } ListElement { p:"0.08462"; a:"5,640" }
        ListElement { p:"0.08439"; a:"9,210" }
    }
    ListModel {
        id: bids
        ListElement { p:"0.08410"; a:"6,780" } ListElement { p:"0.08392"; a:"14,120" }
        ListElement { p:"0.08370"; a:"4,330" } ListElement { p:"0.08344"; a:"17,890" }
        ListElement { p:"0.08318"; a:"7,250" }
    }
    ListModel {
        id: trades
        ListElement { p:"0.08421"; a:"1,240"; t:"20:42:18"; up:true }
        ListElement { p:"0.08418"; a:"840"; t:"20:41:54"; up:false }
        ListElement { p:"0.08424"; a:"2,100"; t:"20:41:31"; up:true }
        ListElement { p:"0.08416"; a:"530"; t:"20:40:58"; up:false }
        ListElement { p:"0.08411"; a:"3,720"; t:"20:40:22"; up:true }
        ListElement { p:"0.08407"; a:"910"; t:"20:39:47"; up:false }
    }
    ListModel { id: orders }
    Timer {
        interval: 1000; running: dexPrototypeEnabled; repeat: true
        onTriggered: {
            refreshOffers()
        }
    }
    Component.onCompleted: if (dexPrototypeEnabled) refreshOffers()
    function refreshOffers() {
        var active = dexEngine.localOffers()
        orders.clear()
        for (var i = 0; i < active.length; ++i) {
            var result = active[i]
            if (result.pair !== root.pairName) continue
            orders.append({offerId:result.id, sideText:result.side, amountText:result.amount,
                priceText:result.price, feeText:result.protocolFeeXmz+" protocol + "+result.networkFeeXmz,
                minerFeeText:result.externalMinerFee, settlementLabel:result.settlementLabel,
                settlementText:result.settlementXmz})
        }
    }

    function fixed(value, places) { return isFinite(value) && value > 0 ? value.toFixed(places) : (0).toFixed(places) }
    function preview(side) {
        var result = dexEngine.createLocalOffer(root.pairName, side, amountInput.text, priceInput.text, 3600)
        if (!result.valid) { previewError = result.error; return }
        previewError = ""
        refreshOffers()
    }
    function selectPair(symbol) {
        baseCoin = symbol
        if (symbol === "BTC") { lastPrice = 1172500; externalMinerFee = "0.000020 BTC" }
        else if (symbol === "LTC") { lastPrice = 1325; externalMinerFee = "0.000100 LTC" }
        else { lastPrice = 6840; externalMinerFee = "0.000010 BCH" }
        priceInput.text = lastPrice.toFixed(5)
        amountInput.text = ""
        refreshOffers()
    }

    ColumnLayout {
        id: page
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            M.Label { fontSize: 25; text: qsTr("Monzero Spot Exchange") + translationManager.emptyString }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 126; Layout.preferredHeight: 32; radius: 16
                color: "#40202a"; border.width: 1; border.color: "#b84b6b"
                M.TextPlain { anchors.centerIn: parent; color: "#ffb4c8"; font.bold: true; font.pixelSize: 12; text: qsTr("DEMO MARKET") + translationManager.emptyString }
            }
        }
        M.WarningBox {
            text: qsTr("Simulation only: candles, order book, trades and balances are demo data. Orders cannot be signed or broadcast. Settlement uses XMZ fees plus the selected chain's native miner fee.") + translationManager.emptyString
        }

        ColumnLayout {
            Layout.fillWidth: true
            M.TextPlain { text: qsTr("Isolated receiver snapshot (read-only)"); color: M.Style.defaultFontColor; font.bold: true }
            M.LineEdit {
                id: snapshotPath
                Layout.fillWidth: true
                labelText: qsTr("Local receiver offers.json path")
                placeholderText: "/path/to/dex-receiver-state/offers.json"
                enabled: !(dexPrototypeEnabled && dexEngine.researchMarketBusy)
            }
            M.StandardButton {
                text: dexPrototypeEnabled && dexEngine.researchMarketBusy ? qsTr("Verifying snapshot…") : qsTr("Verify and refresh snapshot")
                enabled: !(dexPrototypeEnabled && dexEngine.researchMarketBusy)
                onClicked: {
                    root.marketSnapshot = ({})
                    dexEngine.refreshResearchMarket(snapshotPath.text)
                }
            }
            M.TextPlain {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                text: root.marketSnapshot.valid
                    ? qsTr("Signatures checked; active offers: ") + root.marketSnapshot.activeCount
                      + qsTr(". Showing up to 10. Snapshot time (Unix): ") + root.marketSnapshot.observedAt
                      + qsTr(". Liquidity is unverified; trading remains disabled. Refresh to check expiry or cancellations.")
                    : (root.marketSnapshot.error || qsTr("No verified snapshot loaded. Demo data below is separate."))
            }
            Repeater {
                model: root.marketSnapshot.valid ? root.marketSnapshot.offers.slice(0, 10) : []
                delegate: M.TextPlain {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: M.Style.defaultFontColor
                    text: modelData.pair + " · " + modelData.side + " · "
                        + qsTr("amount atoms: ") + modelData.amountAtomic + " · "
                        + qsTr("price atoms: ") + modelData.priceAtomic + " · " + modelData.offerId
                }
            }
        }

        Rectangle {
            Layout.fillWidth:true; Layout.preferredHeight:52; radius:8
            color:M.Style.middlePanelBackgroundColor; border.width:1; border.color:M.Style.inputBorderColorInActive
            RowLayout {
                anchors.fill:parent; anchors.margins:10
                M.TextPlain { Layout.fillWidth:true; color:M.Style.defaultFontColor; text:qsTr("Private adapter status") + translationManager.emptyString }
                M.TextPlain { color:"#ef6575"; font.bold:true; text:qsTr("LIVE BLOCKED") + " · " + root.readiness.checksPassed + "/" + root.readiness.checksRequired + " " + qsTr("checks") }
                M.TextPlain { Layout.maximumWidth:620; elide:Text.ElideRight; color:M.Style.defaultFontColor; opacity:.65; text:root.readiness.blockers.join(" · ") }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            M.TextPlain { color:M.Style.defaultFontColor; opacity:.65; text:qsTr("Markets") + translationManager.emptyString }
            M.StandardButton { text:"BTC / XMZ"; primary:root.baseCoin === "BTC"; onClicked:root.selectPair("BTC") }
            M.StandardButton { text:"LTC / XMZ"; primary:root.baseCoin === "LTC"; onClicked:root.selectPair("LTC") }
            M.StandardButton { text:"BCH / XMZ"; primary:root.baseCoin === "BCH"; onClicked:root.selectPair("BCH") }
            Item { Layout.fillWidth:true }
            M.TextPlain { color:M.Style.defaultFontColor; opacity:.55; text:qsTr("UTXO markets only · ETH excluded") + translationManager.emptyString }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 76; radius: 10
            color: M.Style.titleBarButtonHoverColor; border.width: 1; border.color: M.Style.inputBorderColorInActive
            RowLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 30
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("Market") } M.TextPlain { color:M.Style.defaultFontColor; font.pixelSize:19; font.bold:true; text:root.pairName } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("Last price") } M.TextPlain { color:"#49c991"; font.pixelSize:19; font.bold:true; text:root.lastPrice.toFixed(5)+" XMZ" } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("24h change") } M.TextPlain { color:"#49c991"; text:"+6.42%" } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("High / Low") } M.TextPlain { color:M.Style.defaultFontColor; text:"0.08740 / 0.07782" } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("Volume") } M.TextPlain { color:M.Style.defaultFontColor; text:"Demo only" } }
                Item { Layout.fillWidth:true }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Rectangle {
                Layout.fillWidth:true; Layout.preferredHeight:390; radius:10
                color:M.Style.titleBarButtonHoverColor; border.width:1; border.color:M.Style.inputBorderColorInActive
                ColumnLayout {
                    anchors.fill:parent; anchors.margins:12
                    RowLayout { Layout.fillWidth:true; M.TextPlain { color:M.Style.defaultFontColor; font.bold:true; text:root.pairName+" · 1h" } Item { Layout.fillWidth:true } M.TextPlain { color:M.Style.defaultFontColor; opacity:.5; text:qsTr("Demo candles") } }
                    Canvas {
                        Layout.fillWidth:true; Layout.fillHeight:true
                        property var closes:[78,80,79,82,84,83,86,85,88,91,89,92,94,93,97,96,99,101,98,102,105,103,107,109,108,112,111,114,116,115,118,121,119,123,126,124,128,131,129,133]
                        onPaint: {
                            var c=getContext("2d"),w=width,h=height,top=15,bottom=h-25,min=74,max=137,step=w/closes.length;c.reset();
                            c.strokeStyle="#493a58";c.lineWidth=1;for(var g=0;g<5;g++){var gy=top+(bottom-top)*g/4;c.beginPath();c.moveTo(0,gy);c.lineTo(w,gy);c.stroke();}
                            for(var i=0;i<closes.length;i++){var o=i?closes[i-1]:76,cl=closes[i],hi=Math.max(o,cl)+2+i%3,lo=Math.min(o,cl)-2-i%2,x=i*step+step/2,yo=bottom-(o-min)/(max-min)*(bottom-top),yc=bottom-(cl-min)/(max-min)*(bottom-top),yh=bottom-(hi-min)/(max-min)*(bottom-top),yl=bottom-(lo-min)/(max-min)*(bottom-top);c.strokeStyle=cl>=o?"#49c991":"#ef6575";c.fillStyle=c.strokeStyle;c.beginPath();c.moveTo(x,yh);c.lineTo(x,yl);c.stroke();c.fillRect(x-step*.28,Math.min(yo,yc),step*.56,Math.max(2,Math.abs(yc-yo)));}
                            c.strokeStyle="#a77be3";c.lineWidth=2;c.beginPath();for(var j=0;j<closes.length;j++){var av=0,n=0;for(var k=Math.max(0,j-5);k<=j;k++){av+=closes[k];n++;}av/=n;var ax=j*step+step/2,ay=bottom-(av-min)/(max-min)*(bottom-top);j?c.lineTo(ax,ay):c.moveTo(ax,ay);}c.stroke();
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth:300; Layout.preferredHeight:390; radius:10
                color:M.Style.titleBarButtonHoverColor; border.width:1; border.color:M.Style.inputBorderColorInActive
                ColumnLayout {
                    anchors.fill:parent; anchors.margins:11; spacing:4
                    M.TextPlain { color:M.Style.defaultFontColor; font.bold:true; text:qsTr("Order book") }
                    RowLayout { Layout.fillWidth:true; M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;opacity:.5;text:qsTr("Price") } M.TextPlain { color:M.Style.defaultFontColor;opacity:.5;text:qsTr("Amount") } }
                    Repeater { model:asks; delegate:RowLayout { Layout.fillWidth:true; height:22; M.TextPlain { Layout.fillWidth:true;color:"#ef6575";text:p } M.TextPlain { color:M.Style.defaultFontColor;text:a } } }
                    M.TextPlain { Layout.fillWidth:true;color:"#49c991";font.pixelSize:18;font.bold:true;text:"↓ "+root.lastPrice.toFixed(5) }
                    Repeater { model:bids; delegate:RowLayout { Layout.fillWidth:true; height:22; M.TextPlain { Layout.fillWidth:true;color:"#49c991";text:p } M.TextPlain { color:M.Style.defaultFontColor;text:a } } }
                    Item { Layout.fillHeight:true }
                }
            }
        }

        RowLayout {
            Layout.fillWidth:true; spacing:12
            Rectangle {
                Layout.fillWidth:true; Layout.preferredHeight:400; radius:10
                color:M.Style.titleBarButtonHoverColor; border.width:1; border.color:M.Style.inputBorderColorInActive
                GridLayout {
                    anchors.fill:parent; anchors.margins:14; columns:2; columnSpacing:16; rowSpacing:9
                    RowLayout { Layout.columnSpan:2; M.Label { fontSize:19;text:qsTr("Spot order") } Item { Layout.fillWidth:true } M.InlineButton { text:qsTr("Limit");onClicked:root.limitOrder=true } M.InlineButton { text:qsTr("Market");onClicked:root.limitOrder=false } }
                    M.LineEdit { id:priceInput;Layout.fillWidth:true;labelText:qsTr("Price (XMZ)");text:root.limitOrder?root.lastPrice.toFixed(5):qsTr("Market price");readOnly:!root.limitOrder }
                    M.LineEdit { id:amountInput;Layout.fillWidth:true;labelText:qsTr("Amount (")+root.baseCoin+")";placeholderText:"0.01" }
                    M.LineEdit { Layout.fillWidth:true;labelText:qsTr("Order total (XMZ)");text:root.fixed(root.totalValue,5);readOnly:true }
                    M.LineEdit { Layout.fillWidth:true;labelText:qsTr("Protocol fee (XMZ)");text:root.fixed(root.tradingFeeValue,5);readOnly:true }
                    Rectangle {
                        Layout.columnSpan:2;Layout.fillWidth:true;Layout.preferredHeight:82;radius:7;color:M.Style.middlePanelBackgroundColor
                        ColumnLayout { anchors.fill:parent;anchors.margins:10;spacing:4
                            RowLayout { Layout.fillWidth:true;M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;text:qsTr("Monzero network fee") } M.TextPlain { color:"#d5a6ff";font.bold:true;text:root.networkFeeXMZ.toFixed(4)+" XMZ" } }
                            RowLayout { Layout.fillWidth:true;M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;text:qsTr("External-chain miner fee") } M.TextPlain { color:M.Style.defaultFontColor;font.bold:true;text:root.externalMinerFee } }
                            M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;opacity:.55;font.pixelSize:12;text:qsTr("Protocol fee is charged in XMZ only after successful settlement; failed or refunded swaps pay no protocol fee.") }
                        }
                    }
                    M.TextPlain { Layout.columnSpan:2;Layout.fillWidth:true;color:M.Style.defaultFontColor;opacity:.62;text:qsTr("Demo balances only · XMZ pays Monzero settlement fees; native coin pays its chain miner fee") }
                    M.TextPlain { Layout.columnSpan:2;Layout.fillWidth:true;visible:root.previewError!=="";color:"#ef6575";text:root.previewError }
                    M.StandardButton { Layout.fillWidth:true;text:qsTr("Preview buy ")+root.baseCoin;enabled:root.totalValue>0;onClicked:root.preview("BUY") }
                    M.StandardButton { Layout.fillWidth:true;text:qsTr("Preview sell ")+root.baseCoin;enabled:root.totalValue>0;onClicked:root.preview("SELL") }
                    M.StandardButton { Layout.columnSpan:2;Layout.fillWidth:true;text:qsTr("Place live order");enabled:root.readiness.ready;tooltip:root.readiness.ready?qsTr("Readiness checks passed; confirmation flow still required"):root.readiness.blockers.join("\n") }
                }
            }
            Rectangle {
                Layout.preferredWidth:300;Layout.preferredHeight:350;radius:10;color:M.Style.titleBarButtonHoverColor;border.width:1;border.color:M.Style.inputBorderColorInActive
                ColumnLayout { anchors.fill:parent;anchors.margins:11;spacing:6
                    M.TextPlain { color:M.Style.defaultFontColor;font.bold:true;text:qsTr("Recent demo trades") }
                    RowLayout { Layout.fillWidth:true;M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;opacity:.5;text:qsTr("Price") } M.TextPlain { color:M.Style.defaultFontColor;opacity:.5;text:qsTr("Amount · Time") } }
                    Repeater { model:trades;delegate:RowLayout { Layout.fillWidth:true;height:28;M.TextPlain { Layout.fillWidth:true;color:up?"#49c991":"#ef6575";text:p } M.TextPlain { color:M.Style.defaultFontColor;text:a+" · "+t } } }
                    Item { Layout.fillHeight:true }
                }
            }
        }

        M.Label { fontSize:19;text:qsTr("Unsigned local offers · expire after one hour · not shared") }
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:orders.count?orderColumn.height+24:80;radius:10;color:M.Style.titleBarButtonHoverColor;border.width:1;border.color:M.Style.inputBorderColorInActive
            M.TextPlain { visible:!orders.count;anchors.centerIn:parent;color:M.Style.defaultFontColor;opacity:.6;text:qsTr("No local order drafts.") }
            Column { id:orderColumn;visible:orders.count;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:12;spacing:6
                Repeater { model:orders;delegate:Rectangle { width:orderColumn.width;height:52;radius:7;color:M.Style.middlePanelBackgroundColor
                    RowLayout { anchors.fill:parent;anchors.margins:10;M.TextPlain { color:sideText==="BUY"?"#49c991":"#ef6575";font.bold:true;text:sideText+" "+root.baseCoin } M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;text:amountText+" @ "+priceText } M.TextPlain { color:"#d5a6ff";text:settlementLabel+": "+settlementText+" XMZ · fees "+feeText+" XMZ · "+minerFeeText } M.InlineButton { text:qsTr("Cancel");onClicked:{ dexEngine.cancelLocalOffer(offerId); root.refreshOffers() } } }
                } }
            }
        }
    }
}
