import QtQuick 2.9

import "." as MonzeroComponents

// Pair tabs, as on an exchange: one per market, each with its last price and 24 h change from
// completed trades (`market` is the DexMarketPanel whose helpers compute them).
Flow {
    id: tabs
    property var coins: []
    property string coin: ""
    property var market
    property var coinDecimals: ({})
    signal picked(string coin)
    spacing: 6
    Repeater {
        model: tabs.coins
        delegate: Rectangle {
            objectName: "pairTab_" + modelData
            property bool active: tabs.coin === modelData
            property var st: tabs.market ? tabs.market.dayStats(tabs.market.tradesFor(tabs.market.trades, modelData), tabs.market.now, tabs.coinDecimals[modelData] || 8)
                                         : ({ last: 0, change: NaN })
            width: 170; height: 52; radius: 6
            color: active ? MonzeroComponents.Style.surfaceRaised : "transparent"
            border.color: active ? MonzeroComponents.Style.accent : MonzeroComponents.Style.accentMuted
            Column {
                anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text { text: modelData + " / XMZ"; font.family: MonzeroComponents.Style.fontMedium.name; font.pixelSize: 14; color: MonzeroComponents.Style.defaultFontColor }
                Row {
                    spacing: 8
                    Text { text: st.last > 0 && tabs.market ? tabs.market.priceText(st.last) : "—"; font.pixelSize: 11; color: MonzeroComponents.Style.dimmedFontColor }
                    Text {
                        visible: !isNaN(st.change)
                        text: isNaN(st.change) ? "" : (st.change >= 0 ? "+" : "") + st.change.toFixed(2) + "%"
                        font.pixelSize: 11; color: st.change >= 0 ? MonzeroComponents.Style.green : "#E5677A"
                    }
                }
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: tabs.picked(modelData) }
        }
    }
}
