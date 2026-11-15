import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// The order book: offers selling XMZ (asks, red, best at the bottom) above the spread, offers
// buying XMZ (bids, green, best at the top) below it, each with a depth bar. Clicking a row picks
// that offer for the order form. Your own offers are marked.
Rectangle {
    id: ob
    property string coin: "ETH"
    property var book: ({ asks: [], bids: [], bestAsk: 0, bestBid: 0, spread: 0, mid: 0 })
    property real lastPrice: 0
    property real coinUsd: 0
    property int rows: 10
    property string pickedId: ""
    property var hovered: null
    signal picked(var row)

    readonly property color up: M.Style.green
    readonly property color down: "#E5677A"
    readonly property var asksShown: book.asks.slice(0, rows).reverse()
    readonly property var bidsShown: book.bids.slice(0, rows)
    readonly property real maxTotal: Math.max(book.asks.length ? book.asks[Math.min(rows, book.asks.length) - 1].total : 0,
                                              book.bids.length ? book.bids[Math.min(rows, book.bids.length) - 1].total : 0)

    color: M.Style.surfaceRaised
    radius: 8
    border.color: M.Style.accentMuted
    implicitHeight: col.implicitHeight + 24

    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 12
        spacing: 2
        Text { text: qsTr("Order book"); color: M.Style.defaultFontColor; font.pixelSize: 15; font.family: M.Style.fontMedium.name; Layout.bottomMargin: 6 }
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Price (%1)").arg(ob.coin); color: M.Style.dimmedFontColor; font.pixelSize: 11; Layout.fillWidth: true; Layout.preferredWidth: 3 }
            Text { text: qsTr("XMZ"); color: M.Style.dimmedFontColor; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.fillWidth: true; Layout.preferredWidth: 2 }
            Text { text: qsTr("Total %1").arg(ob.coin); color: M.Style.dimmedFontColor; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.fillWidth: true; Layout.preferredWidth: 2 }
        }
        Text {
            visible: ob.book.asks.length === 0
            text: qsTr("No one is selling XMZ for %1 right now").arg(ob.coin)
            color: M.Style.dimmedFontColor; font.pixelSize: 12; Layout.topMargin: 6; Layout.bottomMargin: 6
        }
        Repeater { model: ob.asksShown; delegate: ob.rowDelegate }
        Rectangle {
            Layout.fillWidth: true; Layout.topMargin: 4; Layout.bottomMargin: 4
            implicitHeight: 34; radius: 4; color: "#0B1E22"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                Text {
                    objectName: "bookLast"
                    text: ob.lastPrice > 0 ? X.price(ob.lastPrice) : ob.book.mid > 0 ? X.price(ob.book.mid) : "—"
                    color: M.Style.defaultFontColor; font.pixelSize: 16; font.family: M.Style.fontMonoMedium.name
                }
                Text {
                    text: ob.coinUsd > 0 && (ob.lastPrice > 0 || ob.book.mid > 0) ? "≈ " + X.usd((ob.lastPrice || ob.book.mid) * ob.coinUsd) : ""
                    color: M.Style.dimmedFontColor; font.pixelSize: 12
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
                Text {
                    visible: ob.book.spread > 0
                    text: qsTr("spread %1%").arg((ob.book.spread / ob.book.bestAsk * 100).toFixed(2))
                    color: M.Style.dimmedFontColor; font.pixelSize: 11
                }
            }
        }
        Repeater { model: ob.bidsShown; delegate: ob.rowDelegate }
        Text {
            // Details of the offer under the pointer
            Layout.fillWidth: true; Layout.topMargin: 6
            wrapMode: Text.WordWrap; color: M.Style.dimmedFontColor; font.pixelSize: 11
            text: !ob.hovered ? qsTr("▪ taken whole only · • your offer · click an offer to trade it")
                : (ob.hovered.mine ? qsTr("Your offer") + " · " : "")
                  + (ob.hovered.partial ? qsTr("any part from %1 XMZ").arg(X.amount(ob.hovered.min, 2)) : qsTr("taken whole only"))
                  + " · " + X.timeLeft(ob.hovered.expiresAt, Date.now() / 1000)
        }
        Text {
            visible: ob.book.bids.length === 0
            text: qsTr("No one is buying XMZ with %1 right now").arg(ob.coin)
            color: M.Style.dimmedFontColor; font.pixelSize: 12; Layout.topMargin: 6
        }
    }

    property Component rowDelegate: Item {
        Layout.fillWidth: true
        implicitHeight: 24
        readonly property bool ask: modelData.side === "sell-xmz"
        Rectangle {
            // Depth: how much XMZ is on offer up to this price
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: ob.maxTotal > 0 ? parent.width * Math.min(1, modelData.total / ob.maxTotal) : 0
            color: parent.ask ? ob.down : ob.up; opacity: 0.12
        }
        Rectangle {
            anchors.fill: parent; color: "transparent"; radius: 3
            border.color: ob.pickedId === modelData.id ? M.Style.accent : rowMouse.containsMouse ? M.Style.accentMuted : "transparent"
        }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4
            Text {
                text: X.price(modelData.price) + (modelData.mine ? "  •" : "")
                color: parent.parent.ask ? ob.down : ob.up; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name
                Layout.fillWidth: true; Layout.preferredWidth: 3
            }
            Text {
                text: X.amount(modelData.xmz, 2) + (modelData.partial ? "" : " ▪")
                color: M.Style.defaultFontColor; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name
                horizontalAlignment: Text.AlignRight; Layout.fillWidth: true; Layout.preferredWidth: 2
            }
            Text {
                text: X.plain(modelData.coin, 6)
                color: M.Style.dimmedFontColor; font.pixelSize: 12; font.family: M.Style.fontMonoRegular.name
                horizontalAlignment: Text.AlignRight; Layout.fillWidth: true; Layout.preferredWidth: 2
            }
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: ob.picked(modelData)
            onContainsMouseChanged: ob.hovered = containsMouse ? modelData : null
        }
    }
}
