import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Dialogs 1.2

import "." as M

// Settings > Layout > Appearance: the user's own colours (a preset or each colour by hand) and a
// picture for the wallet card. Saved in the wallet's settings (persistentSettings.customTheme) and
// applied at once through Style.custom; "Reset" goes back to the theme's own colours.
ColumnLayout {
    id: editor
    spacing: 10

    readonly property var custom: M.Style.custom || ({})
    property string editing: ""          // the colour whose palette is open

    readonly property var roles: [
        { k: "accent", t: qsTr("Accent: buttons and highlights") },
        { k: "accentSoft", t: qsTr("Borders and outlines") },
        { k: "background", t: qsTr("Background") },
        { k: "sidebar", t: qsTr("Side panel and title bar") },
        { k: "surface", t: qsTr("Panels and input boxes") },
        { k: "text", t: qsTr("Text") },
        { k: "dimmed", t: qsTr("Secondary text") },
        { k: "buttonText", t: qsTr("Text on buttons") },
        { k: "positive", t: qsTr("Gains and buying") },
        { k: "warning", t: qsTr("Warnings") },
        { k: "cardStart", t: qsTr("Wallet card, top") },
        { k: "cardEnd", t: qsTr("Wallet card, bottom") }
    ]
    readonly property var presets: [
        { t: qsTr("Monzero"), c: {} },
        { t: qsTr("Ocean"), c: { accent: "#4DA3FF", accentSoft: "#1F4E80", background: "#050B16", sidebar: "#0A1A33", surface: "#0E2240", buttonText: "#04101F", cardStart: "#1D4E89", cardEnd: "#0A1A33" } },
        { t: qsTr("Sunset"), c: { accent: "#FF8A4C", accentSoft: "#8A3B1E", background: "#140806", sidebar: "#2A110B", surface: "#2A1510", buttonText: "#1A0802", cardStart: "#B5452A", cardEnd: "#3A1409" } },
        { t: qsTr("Forest"), c: { accent: "#6BCB77", accentSoft: "#2E6B3A", background: "#06110A", sidebar: "#0D2416", surface: "#10291A", buttonText: "#04140A", cardStart: "#2E7D4F", cardEnd: "#0B2214" } },
        { t: qsTr("Violet"), c: { accent: "#B48CFF", accentSoft: "#5B3E99", background: "#0B0716", sidebar: "#1A1233", surface: "#1E1538", buttonText: "#120A24", cardStart: "#6A3FD1", cardEnd: "#1A1233" } },
        { t: qsTr("Mono"), c: { accent: "#E0E0E0", accentSoft: "#555555", background: "#0A0A0A", sidebar: "#151515", surface: "#1C1C1C", text: "#F2F2F2", dimmed: "#9A9A9A", buttonText: "#111111", cardStart: "#3A3A3A", cardEnd: "#121212" } }
    ]
    readonly property var palette: ["#29D6CB", "#4DA3FF", "#B48CFF", "#FF6FA8", "#FF8A4C", "#FFB454", "#F5E663", "#6BCB77",
                                    "#FFFFFF", "#C9D1D9", "#7D8590", "#30363D", "#161B22", "#0A0A0A", "#14545B", "#08262E"]

    // The colour now in use for a role (the user's, or the theme's)
    function current(k) {
        if (custom[k]) return custom[k]
        var S = M.Style
        var themed = { accent: S.accent, accentSoft: S.accentMuted, background: S.appWindowBackgroundColor, sidebar: S.leftPanelBackgroundGradientStart,
                       surface: S.surfaceRaised, text: S.defaultFontColor, dimmed: S.dimmedFontColor, buttonText: S.buttonTextColor,
                       positive: S.green, warning: S.warningColor,
                       cardStart: S.blackTheme ? "#14545B" : "#CCF3EF", cardEnd: S.blackTheme ? "#08262E" : "#EAFBF8" }
        return themed[k] || "#000000"
    }
    function validColour(text) { return /^#[0-9A-Fa-f]{6}$/.test(String(text).trim()) }
    function save(next) {
        var clean = {}
        for (var k in next) if (next[k] !== undefined && next[k] !== "" && next[k] !== null) clean[k] = next[k]
        M.Style.custom = clean
        if (typeof persistentSettings !== "undefined") persistentSettings.customTheme = JSON.stringify(clean)
    }
    function set(k, value) { var n = JSON.parse(JSON.stringify(custom)); if (value === undefined) delete n[k]; else n[k] = value; save(n) }
    function applyPreset(c) {
        // A preset sets every colour; the card picture and its darkening stay
        var n = JSON.parse(JSON.stringify(c))
        if (custom.cardImage) n.cardImage = custom.cardImage
        if (custom.cardDim !== undefined) n.cardDim = custom.cardDim
        save(n)
    }

    M.TextPlain { text: qsTr("Appearance") + translationManager.emptyString; font.pixelSize: 18; color: M.Style.defaultFontColor; themeTransition: false }
    M.TextPlain {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.dimmedFontColor; themeTransition: false
        text: qsTr("Choose your own colours and a picture for the wallet card. They apply at once and are kept for this computer.") + translationManager.emptyString
    }

    // Presets
    Flow {
        Layout.fillWidth: true; spacing: 8
        Repeater {
            model: editor.presets
            delegate: Rectangle {
                id: presetCard
                objectName: "preset_" + modelData.t
                width: presetRow.implicitWidth + 20; height: 34; radius: 6
                color: "transparent"; border.color: M.Style.accentMuted
                Row {
                    id: presetRow; anchors.centerIn: parent; spacing: 6
                    Repeater {
                        model: ["accent", "background", "cardStart"]
                        delegate: Rectangle { width: 12; height: 12; radius: 6; anchors.verticalCenter: parent.verticalCenter; border.color: "#55FFFFFF"
                            color: presetCard.presetColour(modelData) }
                    }
                    Text { text: modelData.t; font.pixelSize: 13; color: M.Style.defaultFontColor; anchors.verticalCenter: parent.verticalCenter }
                }
                function presetColour(k) {
                    var c = modelData.c[k]
                    return c ? c : k === "accent" ? "#29D6CB" : k === "background" ? "#030E13" : "#14545B"
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: editor.applyPreset(modelData.c) }
            }
        }
    }

    // Each colour by hand
    GridLayout {
        Layout.fillWidth: true
        columns: editor.width > 820 ? 2 : 1
        columnSpacing: 24; rowSpacing: 6
        Repeater {
            model: editor.roles
            delegate: ColumnLayout {
                id: roleRow
                Layout.fillWidth: true; spacing: 4
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Rectangle {
                        objectName: "swatch_" + modelData.k
                        width: 28; height: 28; radius: 6
                        color: editor.current(modelData.k)
                        border.color: editor.editing === modelData.k ? M.Style.defaultFontColor : "#55FFFFFF"
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: editor.editing = editor.editing === modelData.k ? "" : modelData.k }
                    }
                    Text { text: modelData.t; font.pixelSize: 13; color: M.Style.defaultFontColor; Layout.fillWidth: true; elide: Text.ElideRight }
                    M.LineEdit {
                        id: hex; objectName: "hex_" + modelData.k
                        Layout.preferredWidth: 110; Layout.minimumWidth: 110; Layout.maximumWidth: 110
                        Layout.maximumHeight: 34; fontSize: 13; inputPaddingTop: 4; inputPaddingBottom: 4
                        text: editor.custom[modelData.k] || ""
                        placeholderText: editor.current(modelData.k); placeholderFontSize: 13
                        error: text !== "" && !editor.validColour(text)
                        onEditingFinished: {
                            var t = text.trim()
                            if (t === "") editor.set(modelData.k, undefined)
                            else if (editor.validColour(t)) editor.set(modelData.k, t.toUpperCase())
                        }
                    }
                    Text {
                        visible: !!editor.custom[modelData.k]
                        text: "↺"; font.pixelSize: 16; color: M.Style.dimmedFontColor
                        MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: editor.set(modelData.k, undefined) }
                    }
                }
                Flow {
                    visible: editor.editing === modelData.k
                    Layout.fillWidth: true; spacing: 6
                    Repeater {
                        model: editor.palette
                        delegate: Rectangle {
                            width: 22; height: 22; radius: 4; color: modelData; border.color: "#55FFFFFF"
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: editor.set(roleRow.roleKey, modelData) }
                        }
                    }
                }
                property string roleKey: modelData.k
            }
        }
    }

    // The wallet card's picture
    M.TextPlain { text: qsTr("Wallet card picture") + translationManager.emptyString; font.pixelSize: 15; color: M.Style.defaultFontColor; themeTransition: false; Layout.topMargin: 6 }
    RowLayout {
        Layout.fillWidth: true; spacing: 10
        M.StandardButton {
            objectName: "chooseCardPicture"
            small: true
            text: (editor.custom.cardImage ? qsTr("Change picture") : qsTr("Choose a picture")) + translationManager.emptyString
            onClicked: cardDialog.open()
        }
        M.StandardButton {
            visible: !!editor.custom.cardImage
            small: true; primary: false
            text: qsTr("Remove picture") + translationManager.emptyString
            onClicked: editor.set("cardImage", undefined)
        }
        Text {
            Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 12; color: M.Style.dimmedFontColor
            text: editor.custom.cardImage ? decodeURIComponent(String(editor.custom.cardImage).replace(/^file:\/\//, "")) : qsTr("None: the card uses its colours")
        }
    }
    RowLayout {
        visible: !!editor.custom.cardImage
        spacing: 6
        Text { text: qsTr("Darken it for readability"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
        Repeater {
            model: [ { v: 0, t: qsTr("No") }, { v: 0.2, t: qsTr("A little") }, { v: 0.35, t: qsTr("Some") }, { v: 0.55, t: qsTr("A lot") } ]
            delegate: Rectangle {
                readonly property real dim: editor.custom.cardDim !== undefined ? Number(editor.custom.cardDim) : 0.35
                width: dimText.implicitWidth + 16; height: 24; radius: 4
                color: Math.abs(dim - modelData.v) < 0.01 ? M.Style.accentMuted : "transparent"; border.color: M.Style.accentMuted
                Text { id: dimText; anchors.centerIn: parent; text: modelData.t; font.pixelSize: 11; color: M.Style.defaultFontColor }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: editor.set("cardDim", modelData.v) }
            }
        }
    }
    M.TextPlain {
        visible: !!editor.custom.cardImage
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor; themeTransition: false
        text: qsTr("The card shows the picture from where it is now: keep the file in place, or choose it again after moving it.") + translationManager.emptyString
    }

    M.StandardButton {
        objectName: "resetAppearance"
        Layout.topMargin: 6
        small: true; primary: false
        enabled: Object.keys(editor.custom).length > 0
        text: qsTr("Reset to the standard look") + translationManager.emptyString
        onClicked: editor.save({})
    }

    FileDialog {
        id: cardDialog
        title: qsTr("Choose a picture for the wallet card") + translationManager.emptyString
        selectExisting: true
        nameFilters: [ qsTr("Pictures") + " (*.png *.jpg *.jpeg *.webp *.bmp *.gif)" ]
        folder: shortcuts.pictures
        onAccepted: editor.set("cardImage", String(fileUrl))
    }
}
