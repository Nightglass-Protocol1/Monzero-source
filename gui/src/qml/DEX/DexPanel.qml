import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: dexPanel
    width: parent.width
    height: parent.height
    color: "#f5f5f5"

    property string selectedPair: "BTC / XMZ"
    property string selectedSide: "BUY"
    property string amount: "0.001"
    property string price: "1172500"
    property int lifetime: 3600

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        Text {
            text: "Monzero DEX"
            font.pixelSize: 24
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Research preview only. Settlement and live trading are unavailable."
        }

        GroupBox {
            title: "Create Offer"
            Layout.fillWidth: true

            GridLayout {
                columns: 4
                width: parent.width

                Label { text: "Pair" }
                ComboBox {
                    model: dexEngine.supportedPairs()
                    currentIndex: 0
                    onCurrentTextChanged: selectedPair = currentText
                }

                Label { text: "Side" }
                ComboBox {
                    model: ["BUY", "SELL"]
                    currentIndex: 0
                    onCurrentTextChanged: selectedSide = currentText
                }

                Label { text: "Amount" }
                TextField {
                    text: amount
                    onTextChanged: amount = text
                    placeholderText: "0.001"
                }

                Label { text: "Price" }
                TextField {
                    text: price
                    onTextChanged: price = text
                    placeholderText: "1172500"
                }

                Label { text: "Lifetime (sec)" }
                TextField {
                    text: lifetime
                    validator: IntValidator { bottom: 60; top: 86400 }
                    onTextChanged: lifetime = parseInt(text) || 3600
                }

                Button {
                    text: "Create Offer"
                    Layout.columnSpan: 4
                    Layout.fillWidth: true
                    onClicked: {
                        console.log("Creating offer:", selectedPair, selectedSide, amount, price)
                    }
                }
            }
        }

        GroupBox {
            title: "Your Offers"
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                anchors.fill: parent
                model: ListModel {
                    ListElement { pair: "BTC / XMZ"; side: "BUY"; amount: "0.001"; price: "1172500"; state: "draft" }
                    ListElement { pair: "LTC / XMZ"; side: "SELL"; amount: "0.01"; price: "100000"; state: "quoted" }
                }
                delegate: Rectangle {
                    width: parent.width
                    height: 40
                    color: index % 2 ? "#f9f9f9" : "white"
                    border.color: "#ddd"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 5
                        Text { text: pair }
                        Text { text: side }
                        Text { text: amount }
                        Text { text: price }
                        Text { text: state }
                        Button {
                            text: "Cancel"
                            visible: state === "draft" || state === "quoted"
                        }
                    }
                }
            }
        }
    }
}
