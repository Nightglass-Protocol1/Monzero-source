import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// "Sell what I mine": the settings of DexAutoSell (main.qml) and what it last did.
Rectangle {
    id: card
    property var ex
    property var controller: null
    readonly property var settings: controller ? controller.settings : null
    readonly property bool on: !!controller && controller.on
    readonly property var st: settings ? controller.state() : ({})
    property bool open: false

    radius: 8; color: M.Style.surfaceRaised; border.color: on ? M.Style.accent : M.Style.accentMuted
    implicitHeight: col.implicitHeight + 28

    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Sell what I mine"); font.pixelSize: 17; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor; Layout.fillWidth: true }
            Text { text: card.on ? qsTr("On") : qsTr("Off"); font.pixelSize: 13; color: card.on ? M.Style.green : M.Style.dimmedFontColor }
            Text {
                objectName: "autoSellToggleOpen"
                text: card.open ? qsTr("Hide") : qsTr("Settings"); font.pixelSize: 12; font.underline: true; color: M.Style.accent
                MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: card.open = !card.open }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
            text: qsTr("Sell XMZ you mine (block rewards and Monzero pool payouts) on the DEX by itself, never below your minimum price. Other XMZ in your wallet is not touched.")
        }
        Text {
            objectName: "autoSellStatus"
            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
            visible: card.on || (card.controller && card.controller.status !== "")
            text: (card.controller ? card.controller.status : "")
                + (card.on && card.st.sold > 0 ? (card.controller.status ? " · " : "") + qsTr("%1 XMZ sold so far.").arg(X.amount(card.st.sold / 1e11, 2)) : "")
        }

        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            visible: card.open || !card.on
            Text { text: qsTr("Sell for"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
            Flow {
                Layout.fillWidth: true; spacing: 8
                Repeater {
                    model: card.ex.allCoins.filter(function (c) { return (card.ex.markets[c] || {}).configured === true })
                    delegate: Rectangle {
                        objectName: "autoSellCoin_" + modelData
                        readonly property bool picked: card.settings && card.settings.dexAutoSellCoin === modelData
                        width: 110; height: 32; radius: 6
                        color: picked ? M.Style.accentMuted : "transparent"
                        border.color: picked ? M.Style.accent : M.Style.accentMuted
                        Text { anchors.centerIn: parent; text: card.ex.label(modelData); font.pixelSize: 13; color: M.Style.defaultFontColor }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.settings.dexAutoSellCoin = modelData }
                    }
                }
            }
            GridLayout {
                columns: 2; columnSpacing: 12; rowSpacing: 6
                Text { text: qsTr("Minimum price (USD per XMZ)"); font.pixelSize: 13; color: M.Style.defaultFontColor }
                M.LineEdit {
                    id: minUsd
                    objectName: "autoSellMinUsd"
                    Layout.preferredWidth: 160; fontSize: 14; inputPaddingTop: 6; inputPaddingBottom: 6
                    text: card.settings ? card.settings.dexAutoSellMinUsd : ""
                    placeholderText: card.ex.noir && Number(card.ex.noir.last) > 0 ? qsTr("now ≈ %1").arg(Number(card.ex.noir.last).toPrecision(2)) : "0.001"
                    onTextChanged: if (card.settings && /^[0-9]*\.?[0-9]*$/.test(text)) card.settings.dexAutoSellMinUsd = text
                }
                Text { text: qsTr("Share of mined XMZ to sell (%)"); font.pixelSize: 13; color: M.Style.defaultFontColor }
                M.LineEdit {
                    objectName: "autoSellPercent"
                    Layout.preferredWidth: 160; fontSize: 14; inputPaddingTop: 6; inputPaddingBottom: 6
                    text: card.settings ? String(card.settings.dexAutoSellPercent) : "100"
                    onTextChanged: { var n = parseInt(text); if (card.settings && n >= 1 && n <= 100) card.settings.dexAutoSellPercent = n }
                }
                Text { text: qsTr("Sell in batches of at least (XMZ)"); font.pixelSize: 13; color: M.Style.defaultFontColor }
                M.LineEdit {
                    objectName: "autoSellBatch"
                    Layout.preferredWidth: 160; fontSize: 14; inputPaddingTop: 6; inputPaddingBottom: 6
                    text: card.settings ? String(card.settings.dexAutoSellBatch) : "500"
                    onTextChanged: { var n = parseInt(text); if (card.settings && n >= 1 && n <= 1000000) card.settings.dexAutoSellBatch = n }
                }
            }
            M.CheckBox {
                objectName: "autoSellPool"
                text: qsTr("Include my Monzero pool payouts (the wallet asks pool.monzero.org for its payouts to this address)")
                toggleOnClick: false
                checked: !card.settings || card.settings.dexAutoSellPool !== false
                onClicked: card.settings.dexAutoSellPool = !card.settings.dexAutoSellPool
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                text: qsTr("While this is on, the wallet moves mined XMZ to your trading wallet by itself (no confirmation each time) and sells it there. What sells is paid into your trade account; send it on from the Funds tab. Keep the wallet open for it to work.")
            }
            M.StandardButton {
                objectName: "autoSellSwitch"
                text: card.on ? qsTr("Turn off") : qsTr("Turn on")
                primary: !card.on
                enabled: card.on || Number(minUsd.text) > 0
                onClicked: card.controller.enable(!card.on)
            }
        }
    }
}
