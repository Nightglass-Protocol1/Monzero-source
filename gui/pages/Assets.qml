// Copyright (c) 2026, The Monzero Project
// Distributed under the BSD 3-Clause license.

import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0

import "../components" as MonzeroComponents

Rectangle {
    id: root
    color: "transparent"
    property alias assetsHeight: mainLayout.height
    property bool assetProtocolActive: currentWallet ? currentWallet.useForkRules(17, 0) : false

    // ------------------------------------------------------------ Studio state
    // Mirrors the website Studio (monzero.org/app): the metadata hash is the
    // SHA-256 of canonical monzero-metadata/1 JSON, so both produce the same hash.
    readonly property string metadataStandard: "monzero-metadata/1"
    readonly property real maxImageBytes: 25 * 1024 * 1024
    readonly property var allowedImageTypes: ["image/png", "image/jpeg", "image/webp", "image/gif", "image/avif"]
    property string kind: "nft"
    property var art: null              // { sha256, mime, bytes, width, height, url, name }
    property string artMessage: ""
    property string metadataJson: ""
    property string metadataHash: ""
    property var checks: []
    property bool blocking: true
    property string createCommand: ""
    property string issueCommand: ""
    property int studioRevision: 0      // bumped to recompute bindings

    ListModel { id: holdingsModel }
    ListModel { id: traitsModel }
    ListModel { id: verifyModel }

    ListModel {
        id: licenseModel
        ListElement { column1: "All rights reserved"; value: "All rights reserved" }
        ListElement { column1: "CC BY 4.0"; value: "CC BY 4.0" }
        ListElement { column1: "CC BY-SA 4.0"; value: "CC BY-SA 4.0" }
        ListElement { column1: "CC BY-NC 4.0"; value: "CC BY-NC 4.0" }
        ListElement { column1: "CC0 (public domain)"; value: "CC0 1.0" }
    }

    function shorten(value) {
        if (!value || value.length <= 24) return value
        return value.substring(0, 12) + "…" + value.substring(value.length - 10)
    }

    function refreshAssets() {
        holdingsModel.clear()
        if (!currentWallet) return
        var balances = currentWallet.assetBalances(false)
        for (var i = 0; i < balances.length; ++i)
            holdingsModel.append(balances[i])
    }

    function onPageCompleted() {
        refreshAssets()
        if (traitsModel.count === 0)
            traitsModel.append({ "traitType": "Background", "traitValue": "" })
        rebuild()
    }

    // ------------------------------------------------------------ Studio logic

    function kindLabel(k) {
        return { "nft": qsTr("NFT"), "edition": qsTr("Edition"), "collection": qsTr("Collection"), "fungible": qsTr("Token") }[k]
    }

    function canonical(value) {
        if (Array.isArray(value))
            return "[" + value.map(canonical).join(",") + "]"
        if (value !== null && typeof value === "object")
            return "{" + Object.keys(value).sort().map(function(k) { return JSON.stringify(k) + ":" + canonical(value[k]) }).join(",") + "}"
        return JSON.stringify(value)
    }

    function utf8Length(text) {
        var n = 0
        for (var i = 0; i < text.length; ++i) {
            var c = text.charCodeAt(i)
            if (c < 0x80) n += 1
            else if (c < 0x800) n += 2
            else if (c >= 0xd800 && c <= 0xdbff) { n += 4; ++i }
            else n += 3
        }
        return n
    }

    function referenceProblem(value) {
        if (!value) return ""
        if (/\s/.test(value)) return qsTr("must not contain spaces")
        if (value.indexOf("\u0000") !== -1) return qsTr("contains an invalid character")
        if (utf8Length(value) > 256) return qsTr("must be at most 256 bytes")
        return ""
    }

    // Decimal supply -> atomic units as a digit string, without floating point.
    function atomicSupply(text, decimals) {
        var clean = String(text || "").trim().replace(/[,_\s]/g, "")
        if (!/^\d+(\.\d+)?$/.test(clean)) return ""
        var parts = clean.split(".")
        var frac = parts.length > 1 ? parts[1] : ""
        if (frac.length > decimals) return ""
        while (frac.length < decimals) frac += "0"
        var digits = (parts[0] + frac).replace(/^0+/, "")
        var max = "18446744073709551615"
        if (digits.length === 0) return ""
        if (digits.length > max.length || (digits.length === max.length && digits > max)) return ""
        return digits
    }

    function traitList() {
        var list = []
        for (var i = 0; i < traitsModel.count; ++i) {
            var t = traitsModel.get(i)
            var type = String(t.traitType).trim(), val = String(t.traitValue).trim()
            if (type && val) list.push({ "trait_type": type, "value": val })
        }
        return list
    }

    function slug(text) {
        var base = String(text || "asset").toLowerCase()
        if (base.normalize) base = base.normalize("NFKD")
        var s = base.replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").substring(0, 40)
        return s || "asset"
    }

    function buildMetadata() {
        var meta = { "standard": root.metadataStandard, "class": root.kind, "name": nameInput.text.trim() }
        var optional = { "description": descriptionInput.text.trim(), "creator": creatorInput.text.trim(),
                         "external_url": websiteInput.text.trim(), "image": imageAddressInput.text.trim() }
        for (var key in optional)
            if (optional[key]) meta[key] = optional[key]
        meta.license = licenseModel.get(licenseDropdown.currentIndex).value
        if (root.art) {
            meta.image_sha256 = root.art.sha256
            meta.image_mime = root.art.mime
            meta.image_bytes = root.art.bytes
            meta.image_width = root.art.width
            meta.image_height = root.art.height
        }
        if (root.kind !== "fungible") {
            var traits = traitList()
            if (traits.length) meta.attributes = traits
        }
        if (root.kind === "edition") meta.edition_size = parseInt(editionInput.text, 10) || 0
        if (root.kind === "fungible") {
            if (symbolInput.text.trim()) meta.symbol = symbolInput.text.trim().toUpperCase()
            meta.decimals = parseInt(decimalsInput.text, 10)
        }
        if ((root.kind === "nft" || root.kind === "edition") && collectionInput.text.trim())
            meta.collection = collectionInput.text.trim().toLowerCase()
        return meta
    }

    function computeChecks() {
        var out = []
        function add(level, message) { out.push({ "level": level, "message": message }) }
        nameInput.text.trim() ? add("ok", qsTr("Name set")) : add("bad", qsTr("Add a name"))
        if (root.kind === "nft" || root.kind === "edition")
            root.art ? add("ok", qsTr("Artwork fingerprinted")) : add("bad", qsTr("Add the artwork"))
        if (root.art && !imageAddressInput.text.trim()) add("warn", qsTr("Add the image address once it is uploaded"))
        var imageProblem = referenceProblem(imageAddressInput.text.trim())
        if (imageProblem) add("bad", qsTr("Image address ") + imageProblem)
        var metaUri = metadataAddressInput.text.trim()
        var metaProblem = referenceProblem(metaUri)
        if (metaProblem) add("bad", qsTr("Metadata address ") + metaProblem)
        else if (!metaUri) add("warn", qsTr("Add the metadata address so wallets can find it"))
        else add("ok", qsTr("Metadata address set"))
        var collection = collectionInput.text.trim()
        if ((root.kind === "nft" || root.kind === "edition") && collection && !/^[0-9a-fA-F]{64}$/.test(collection))
            add("bad", qsTr("Collection ID must be 64 hexadecimal characters"))
        if (root.kind === "edition") {
            var n = Number(editionInput.text);
            if (n === Math.floor(n) && n >= 2 && n <= 1000000) add("ok", n + qsTr(" numbered copies"));
            else add("bad", qsTr("Copies must be between 2 and 1,000,000"));
        }
        if (root.kind === "fungible") {
            var d = Number(decimalsInput.text)
            if (!(d === Math.floor(d) && d >= 0 && d <= 11)) add("bad", qsTr("Decimals must be 0 to 11"))
            else if (!atomicSupply(supplyInput.text, d)) add("bad", qsTr("Enter a total supply that fits the chosen decimals"))
            else add("ok", qsTr("Supply is fixed forever: nobody can mint more"))
        }
        return out
    }

    function descriptor() {
        var supply = "1", decimals = "0"
        if (root.kind === "edition") supply = String(parseInt(editionInput.text, 10))
        if (root.kind === "fungible") {
            decimals = String(parseInt(decimalsInput.text, 10))
            supply = atomicSupply(supplyInput.text, parseInt(decimals, 10))
        }
        var collection = (root.kind === "nft" || root.kind === "edition") && collectionInput.text.trim()
                         ? collectionInput.text.trim().toLowerCase() : ""
        return { "assetType": root.kind, "atomicSupply": supply, "displayDecimals": decimals,
                 "metadataHash": root.metadataHash, "metadataReference": metadataAddressInput.text.trim(),
                 "collectionId": collection }
    }

    property bool studioReady: false
    Component.onCompleted: { root.studioReady = true; rebuild() }

    function rebuild() {
        if (!root.studioReady) return
        var meta = buildMetadata()
        root.metadataJson = canonical(meta)
        root.metadataHash = oshelper.sha256Utf8(root.metadataJson)
        var list = computeChecks()
        root.checks = list
        root.blocking = list.some(function(c) { return c.level === "bad" })
        if (root.blocking) {
            root.createCommand = ""
            root.issueCommand = ""
        } else {
            var d = descriptor()
            var args = [d.assetType, d.atomicSupply, d.displayDecimals, d.metadataHash,
                        d.metadataReference || "none", d.collectionId || "none"].join(" ")
            root.createCommand = "asset_create " + args + " " + slug(meta.name) + ".mzi"
            root.issueCommand = "asset_issue YOUR_ADDRESS " + args
        }
        root.studioRevision++
    }

    function setKind(k) {
        root.kind = k
        rebuild()
    }

    function chooseArtwork() {
        var path = oshelper.openFileDialog(qsTr("Choose artwork"), qsTr("Images (*.png *.jpg *.jpeg *.webp *.gif *.avif)"))
        if (!path) return
        var info = oshelper.fileFingerprint(path, root.maxImageBytes)
        if (!info.ok) {
            root.artMessage = info.error
            return
        }
        if (root.allowedImageTypes.indexOf(info.mime) === -1) {
            root.artMessage = qsTr("Please choose a PNG, JPEG, WebP, GIF or AVIF image.")
            return
        }
        root.art = { "sha256": info.sha256, "mime": info.mime, "bytes": info.bytes,
                     "width": info.width, "height": info.height, "url": info.url, "name": info.name }
        root.artMessage = info.name + " · " + info.width + "×" + info.height + " · " + formatBytes(info.bytes)
        rebuild()
    }

    function formatBytes(n) {
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB"
        return (n / 1024 / 1024).toFixed(1) + " MB"
    }

    function saveMetadata() {
        var path = oshelper.openSaveFileDialog(qsTr("Save metadata"), oshelper.downloadLocation(), slug(nameInput.text) + ".metadata.json")
        if (!path) return
        if (oshelper.writeUtf8File(path, root.metadataJson))
            appWindow.showStatusMessage(qsTr("metadata.json saved. Upload it to your metadata address."), 4)
        else
            appWindow.showStatusMessage(qsTr("Could not save the metadata file."), 4)
    }

    function copyText(text) {
        if (!text) return
        clipboard.setText(text)
        appWindow.showStatusMessage(qsTr("Copied to clipboard"), 3)
    }

    function issueOnChain() {
        var d = descriptor()
        var recipient = recipientInput.text.trim()
        if (!recipient)
            recipient = currentWallet.address(currentWallet.currentSubaddressAccount, 0)
        d.recipientAddress = recipient
        appWindow.handleAssetIssuance(d)
    }

    // ------------------------------------------------------------ verify

    property var verifyMeta: null       // { sha256, text, name }
    property var verifyImage: null      // { sha256, name }

    function chooseVerifyFile(isImage) {
        var path = oshelper.openFileDialog(isImage ? qsTr("Choose the image") : qsTr("Choose the metadata file"),
                                           isImage ? qsTr("Images (*.png *.jpg *.jpeg *.webp *.gif *.avif)") : qsTr("JSON (*.json)"))
        if (!path) return
        var info = oshelper.fileFingerprint(path, isImage ? root.maxImageBytes : 1024 * 1024)
        if (!info.ok) {
            appWindow.showStatusMessage(info.error, 4)
            return
        }
        if (isImage) root.verifyImage = { "sha256": info.sha256, "name": info.name }
        else root.verifyMeta = { "sha256": info.sha256, "name": info.name, "text": oshelper.readTextFile(path, 1024 * 1024) }
        runVerify()
    }

    function runVerify() {
        verifyModel.clear()
        if (!root.verifyMeta) return
        function add(level, message) { verifyModel.append({ "level": level, "message": message }) }
        var expected = verifyExpectedInput.text.trim().toLowerCase()
        add("info", qsTr("Metadata hash: ") + root.verifyMeta.sha256)
        if (expected) {
            if (!/^[0-9a-f]{64}$/.test(expected)) add("warn", qsTr("The expected hash should be 64 hexadecimal characters"))
            else if (expected === root.verifyMeta.sha256) add("ok", qsTr("Metadata matches the on-chain hash"))
            else add("bad", qsTr("Metadata does NOT match the expected hash: it has been changed or is the wrong file"))
        } else {
            add("warn", qsTr("Paste the asset's metadata hash to check this file against the chain"))
        }
        var meta = null
        try { meta = JSON.parse(root.verifyMeta.text) } catch (e) { add("bad", qsTr("The file is not valid JSON")) }
        if (!meta) return
        meta.standard === root.metadataStandard ? add("ok", qsTr("Follows ") + root.metadataStandard)
                                                : add("warn", qsTr("Does not declare monzero-metadata/1; fields may differ"))
        if (typeof meta.name === "string") add("info", qsTr("Name: ") + meta.name.substring(0, 120))
        if (root.verifyImage) {
            if (!meta.image_sha256) add("warn", qsTr("The metadata has no image fingerprint to check against"))
            else if (String(meta.image_sha256).toLowerCase() === root.verifyImage.sha256) add("ok", qsTr("Image matches the fingerprint in the metadata"))
            else add("bad", qsTr("Image does NOT match the fingerprint in the metadata"))
        }
    }

    function levelColor(level) {
        if (level === "ok") return MonzeroComponents.Style.green
        if (level === "warn") return MonzeroComponents.Style.warningColor
        if (level === "bad") return MonzeroComponents.Style.errorColor
        return MonzeroComponents.Style.dimmedFontColor
    }
    function levelMark(level) {
        return { "ok": "✓", "warn": "!", "bad": "✕", "info": "·" }[level]
    }

    // ------------------------------------------------------------ layout

    ColumnLayout {
        id: mainLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 18

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                MonzeroComponents.Label {
                    fontSize: 25
                    text: qsTr("Assets & NFT Studio") + translationManager.emptyString
                }
                MonzeroComponents.TextPlain {
                    Layout.fillWidth: true
                    color: MonzeroComponents.Style.defaultFontColor
                    opacity: 0.72
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                    text: qsTr("Mint NFTs, numbered editions, collections and fixed-supply tokens. Your artwork never leaves this computer.") + translationManager.emptyString
                }
            }

            MonzeroComponents.StandardButton {
                text: qsTr("Refresh") + translationManager.emptyString
                primary: false
                onClicked: root.refreshAssets()
            }
        }

        MonzeroComponents.WarningBox {
            visible: !root.assetProtocolActive
            text: qsTr("Assets are not active on this network yet (hard fork 17). You can design assets and save their metadata now; issuing unlocks when the upgrade activates.") + translationManager.emptyString
        }

        MonzeroComponents.Label {
            fontSize: 19
            text: qsTr("Portfolio") + translationManager.emptyString
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: holdingsModel.count === 0 ? 92 : holdingsColumn.height + 24
            radius: 10
            color: MonzeroComponents.Style.titleBarButtonHoverColor
            border.width: 1
            border.color: MonzeroComponents.Style.inputBorderColorInActive

            MonzeroComponents.TextPlain {
                visible: holdingsModel.count === 0
                anchors.centerIn: parent
                color: MonzeroComponents.Style.defaultFontColor
                opacity: 0.65
                font.pixelSize: 15
                text: qsTr("No confirmed asset holdings in this wallet yet.") + translationManager.emptyString
            }

            Column {
                id: holdingsColumn
                visible: holdingsModel.count > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 8

                Repeater {
                    model: holdingsModel
                    delegate: Rectangle {
                        width: holdingsColumn.width
                        height: 100
                        radius: 8
                        color: MonzeroComponents.Style.middlePanelBackgroundColor
                        border.width: 1
                        border.color: MonzeroComponents.Style.inputBorderColorInActive

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 13
                            spacing: 16

                            Rectangle {
                                Layout.preferredWidth: 46
                                Layout.preferredHeight: 46
                                radius: 23
                                color: MonzeroComponents.Style.buttonBackgroundColor
                                MonzeroComponents.TextPlain {
                                    anchors.centerIn: parent
                                    color: MonzeroComponents.Style.buttonTextColor
                                    font.pixelSize: 19
                                    font.bold: true
                                    text: "A"
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                MonzeroComponents.TextPlain {
                                    color: MonzeroComponents.Style.defaultFontColor
                                    font.pixelSize: 15
                                    font.bold: true
                                    text: root.shorten(assetId)
                                }
                                MonzeroComponents.TextPlain {
                                    color: MonzeroComponents.Style.defaultFontColor
                                    opacity: 0.62
                                    font.pixelSize: 12
                                    text: outputCount + qsTr(" outputs · newest block ") + newestHeight + translationManager.emptyString
                                }
                            }

                            ColumnLayout {
                                Layout.alignment: Qt.AlignRight
                                spacing: 3
                                MonzeroComponents.TextPlain {
                                    Layout.alignment: Qt.AlignRight
                                    color: MonzeroComponents.Style.defaultFontColor
                                    font.pixelSize: 16
                                    font.bold: true
                                    text: atomicBalance
                                }
                                MonzeroComponents.TextPlain {
                                    Layout.alignment: Qt.AlignRight
                                    color: MonzeroComponents.Style.defaultFontColor
                                    opacity: 0.62
                                    font.pixelSize: 12
                                    text: qsTr("atomic units · ") + spendableOutputCount + qsTr(" spendable") + translationManager.emptyString
                                }
                            }
                        }
                    }
                }
            }
        }

        // ======================================================== Creator studio

        MonzeroComponents.Label {
            Layout.topMargin: 8
            fontSize: 19
            text: qsTr("What are you making?") + translationManager.emptyString
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 760 ? 4 : 2
            columnSpacing: 10
            rowSpacing: 10

            Repeater {
                model: [
                    { "k": "nft", "title": qsTr("NFT"), "sub": qsTr("One of one") },
                    { "k": "edition", "title": qsTr("Edition"), "sub": qsTr("Numbered copies") },
                    { "k": "collection", "title": qsTr("Collection"), "sub": qsTr("Groups NFTs together") },
                    { "k": "fungible", "title": qsTr("Token"), "sub": qsTr("Fixed supply") }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    radius: 10
                    property bool selected: root.kind === modelData.k
                    color: selected ? Qt.rgba(0.41, 0.84, 0.76, 0.12) : MonzeroComponents.Style.titleBarButtonHoverColor
                    border.width: selected ? 2 : 1
                    border.color: selected ? MonzeroComponents.Style.accent : MonzeroComponents.Style.inputBorderColorInActive

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        MonzeroComponents.TextPlain {
                            text: modelData.title
                            font.pixelSize: 15
                            font.bold: true
                            color: MonzeroComponents.Style.defaultFontColor
                        }
                        MonzeroComponents.TextPlain {
                            text: modelData.sub
                            font.pixelSize: 12
                            color: MonzeroComponents.Style.dimmedFontColor
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.setKind(modelData.k)
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 900 ? 2 : 1
            columnSpacing: 20
            rowSpacing: 16

            // ---------------- form
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 14

                // 1 Artwork
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: artColumn.height + 30
                    radius: 10
                    color: MonzeroComponents.Style.titleBarButtonHoverColor
                    border.width: 1
                    border.color: MonzeroComponents.Style.inputBorderColorInActive

                    ColumnLayout {
                        id: artColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 15
                        spacing: 10

                        MonzeroComponents.Label {
                            fontSize: 16
                            text: "1  " + (root.kind === "collection" ? qsTr("Cover image")
                                         : root.kind === "fungible" ? qsTr("Logo (optional)") : qsTr("Artwork")) + translationManager.emptyString
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            MonzeroComponents.StandardButton {
                                text: (root.art ? qsTr("Replace image…") : qsTr("Choose image…")) + translationManager.emptyString
                                primary: !root.art
                                onClicked: root.chooseArtwork()
                            }
                            MonzeroComponents.TextPlain {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: 12
                                color: MonzeroComponents.Style.dimmedFontColor
                                text: root.artMessage || qsTr("PNG, JPEG, WebP, GIF or AVIF · up to 25 MB · stays on this computer")
                            }
                        }
                    }
                }

                // 2 Details
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: detailsColumn.height + 30
                    radius: 10
                    color: MonzeroComponents.Style.titleBarButtonHoverColor
                    border.width: 1
                    border.color: MonzeroComponents.Style.inputBorderColorInActive

                    ColumnLayout {
                        id: detailsColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 15
                        spacing: 12

                        MonzeroComponents.Label { fontSize: 16; text: "2  " + qsTr("Details") + translationManager.emptyString }

                        MonzeroComponents.LineEdit {
                            id: nameInput
                            Layout.fillWidth: true
                            labelText: qsTr("Name") + translationManager.emptyString
                            placeholderText: root.kind === "fungible" ? "Nightshade Credit" : root.kind === "collection" ? "Nightshade Originals" : "Nightshade #001"
                            onTextChanged: root.rebuild()
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.kind === "fungible"
                            spacing: 12
                            MonzeroComponents.LineEdit {
                                id: symbolInput
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                labelText: qsTr("Symbol") + translationManager.emptyString
                                placeholderText: "NSC"
                                onTextChanged: root.rebuild()
                            }
                            MonzeroComponents.LineEdit {
                                id: decimalsInput
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                labelText: qsTr("Decimals") + translationManager.emptyString
                                text: "8"
                                onTextChanged: root.rebuild()
                            }
                        }
                        MonzeroComponents.LineEdit {
                            id: supplyInput
                            Layout.fillWidth: true
                            visible: root.kind === "fungible"
                            labelText: qsTr("Total supply") + translationManager.emptyString
                            placeholderText: "1000000"
                            onTextChanged: root.rebuild()
                        }
                        MonzeroComponents.LineEdit {
                            id: editionInput
                            Layout.fillWidth: true
                            visible: root.kind === "edition"
                            labelText: qsTr("Number of copies") + translationManager.emptyString
                            text: "100"
                            onTextChanged: root.rebuild()
                        }
                        MonzeroComponents.LineEditMulti {
                            id: descriptionInput
                            Layout.fillWidth: true
                            labelText: qsTr("Description") + translationManager.emptyString
                            placeholderText: qsTr("What is it, and why does it exist?") + translationManager.emptyString
                            onTextChanged: root.rebuild()
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            MonzeroComponents.LineEdit {
                                id: creatorInput
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                labelText: qsTr("Creator") + translationManager.emptyString
                                placeholderText: qsTr("Your name or handle") + translationManager.emptyString
                                onTextChanged: root.rebuild()
                            }
                            MonzeroComponents.StandardDropdown {
                                id: licenseDropdown
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                dataModel: licenseModel
                                currentIndex: 0
                                labelText: qsTr("License") + translationManager.emptyString
                                onChanged: root.rebuild()
                            }
                        }
                        MonzeroComponents.LineEdit {
                            id: websiteInput
                            Layout.fillWidth: true
                            labelText: qsTr("Website") + translationManager.emptyString
                            placeholderText: "https://"
                            onTextChanged: root.rebuild()
                        }
                        MonzeroComponents.LineEdit {
                            id: collectionInput
                            Layout.fillWidth: true
                            visible: root.kind === "nft" || root.kind === "edition"
                            labelText: qsTr("Part of collection (optional)") + translationManager.emptyString
                            placeholderText: qsTr("Collection asset ID") + translationManager.emptyString
                            onTextChanged: root.rebuild()
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: root.kind !== "fungible"
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                MonzeroComponents.TextPlain {
                                    Layout.fillWidth: true
                                    text: qsTr("Traits") + translationManager.emptyString
                                    font.pixelSize: 14
                                    color: MonzeroComponents.Style.defaultFontColor
                                }
                                MonzeroComponents.StandardButton {
                                    small: true
                                    primary: false
                                    text: qsTr("+ Add trait") + translationManager.emptyString
                                    onClicked: traitsModel.append({ "traitType": "", "traitValue": "" })
                                }
                            }
                            Repeater {
                                model: traitsModel
                                delegate: RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    MonzeroComponents.LineEdit {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 100
                                        placeholderText: qsTr("Trait, e.g. Background") + translationManager.emptyString
                                        text: traitType
                                        onTextChanged: { traitsModel.setProperty(index, "traitType", text); root.rebuild() }
                                    }
                                    MonzeroComponents.LineEdit {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 100
                                        placeholderText: qsTr("Value, e.g. Midnight") + translationManager.emptyString
                                        text: traitValue
                                        onTextChanged: { traitsModel.setProperty(index, "traitValue", text); root.rebuild() }
                                    }
                                    MonzeroComponents.StandardButton {
                                        small: true
                                        primary: false
                                        text: "×"
                                        onClicked: { traitsModel.remove(index); root.rebuild() }
                                    }
                                }
                            }
                        }
                    }
                }

                // 3 Hosting
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hostingColumn.height + 30
                    radius: 10
                    color: MonzeroComponents.Style.titleBarButtonHoverColor
                    border.width: 1
                    border.color: MonzeroComponents.Style.inputBorderColorInActive

                    ColumnLayout {
                        id: hostingColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 15
                        spacing: 12

                        MonzeroComponents.Label { fontSize: 16; text: "3  " + qsTr("Hosting") + translationManager.emptyString }
                        MonzeroComponents.TextPlain {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 13
                            color: MonzeroComponents.Style.dimmedFontColor
                            text: qsTr("Upload the image and the metadata file anywhere: IPFS, Arweave or your own website. The hashes prove they haven't been changed, so the host doesn't need to be trusted.") + translationManager.emptyString
                        }
                        MonzeroComponents.LineEdit {
                            id: imageAddressInput
                            Layout.fillWidth: true
                            labelText: qsTr("Image address") + translationManager.emptyString
                            placeholderText: "ipfs://… or https://…"
                            onTextChanged: root.rebuild()
                        }
                        MonzeroComponents.LineEdit {
                            id: metadataAddressInput
                            Layout.fillWidth: true
                            labelText: qsTr("Metadata address") + translationManager.emptyString
                            placeholderText: qsTr("Where metadata.json will live") + translationManager.emptyString
                            onTextChanged: root.rebuild()
                        }
                    }
                }

                // 4 Export
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: exportColumn.height + 30
                    radius: 10
                    color: MonzeroComponents.Style.titleBarButtonHoverColor
                    border.width: 1
                    border.color: MonzeroComponents.Style.inputBorderColorInActive

                    ColumnLayout {
                        id: exportColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 15
                        spacing: 12

                        MonzeroComponents.Label { fontSize: 16; text: "4  " + qsTr("Save and issue") + translationManager.emptyString }

                        Repeater {
                            model: root.checks
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                MonzeroComponents.TextPlain {
                                    text: root.levelMark(modelData.level)
                                    font.bold: true
                                    font.pixelSize: 13
                                    color: root.levelColor(modelData.level)
                                }
                                MonzeroComponents.TextPlain {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: modelData.message
                                    font.pixelSize: 13
                                    color: modelData.level === "bad" ? MonzeroComponents.Style.errorColor : MonzeroComponents.Style.defaultFontColor
                                }
                            }
                        }

                        MonzeroComponents.TextPlain {
                            text: qsTr("Metadata hash") + translationManager.emptyString
                            font.pixelSize: 12
                            color: MonzeroComponents.Style.dimmedFontColor
                        }
                        TextEdit {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            font.family: isWindows ? "Consolas" : "monospace"
                            font.pixelSize: 13
                            color: MonzeroComponents.Style.accentBright
                            text: root.metadataHash
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 10
                            MonzeroComponents.StandardButton {
                                text: qsTr("Save metadata.json") + translationManager.emptyString
                                enabled: nameInput.text.trim().length > 0
                                onClicked: root.saveMetadata()
                            }
                            MonzeroComponents.StandardButton {
                                text: qsTr("Copy CLI command") + translationManager.emptyString
                                primary: false
                                enabled: root.createCommand.length > 0
                                tooltip: root.createCommand
                                onClicked: root.copyText(root.createCommand)
                            }
                            MonzeroComponents.StandardButton {
                                text: qsTr("Copy metadata hash") + translationManager.emptyString
                                primary: false
                                onClicked: root.copyText(root.metadataHash)
                            }
                        }

                        MonzeroComponents.LineEdit {
                            id: recipientInput
                            Layout.fillWidth: true
                            labelText: qsTr("Issue to address") + translationManager.emptyString
                            placeholderText: qsTr("Defaults to this wallet") + translationManager.emptyString
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            MonzeroComponents.TextPlain {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: 13
                                opacity: 0.7
                                color: MonzeroComponents.Style.defaultFontColor
                                text: (root.assetProtocolActive
                                       ? qsTr("Issuing always shows a transaction confirmation before anything is signed or sent.")
                                       : qsTr("Issuing unlocks when assets activate. Save the metadata now and keep it with your artwork.")) + translationManager.emptyString
                            }
                            MonzeroComponents.StandardButton {
                                text: qsTr("Issue on chain") + translationManager.emptyString
                                enabled: root.assetProtocolActive && currentWallet && !currentWallet.viewOnly && !root.blocking
                                tooltip: root.assetProtocolActive ? qsTr("Build the issuance transaction for review")
                                                                  : qsTr("Requires hard fork 17")
                                onClicked: root.issueOnChain()
                            }
                        }
                    }
                }
            }

            // ---------------- live preview card
            Rectangle {
                id: previewCard
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: root.width > 900 ? 320 : Math.min(root.width, 360)
                Layout.preferredHeight: previewColumn.height
                radius: 16
                clip: true
                color: MonzeroComponents.Style.middlePanelBackgroundColor
                border.width: 1
                border.color: MonzeroComponents.Style.inputBorderColorInActive

                Column {
                    id: previewColumn
                    width: parent.width

                    Rectangle {
                        width: parent.width
                        height: width
                        color: MonzeroComponents.Style.inputBoxBackground
                        radius: 16
                        Image {
                            anchors.fill: parent
                            anchors.margins: root.kind === "fungible" ? parent.width * 0.19 : 0
                            fillMode: root.kind === "fungible" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                            source: root.art ? root.art.url : ""
                            asynchronous: true
                            sourceSize.width: 640
                            visible: !!root.art
                        }
                        MonzeroComponents.TextPlain {
                            anchors.centerIn: parent
                            visible: !root.art
                            text: (root.kind === "fungible" ? qsTr("Token logo") : qsTr("Your artwork")) + translationManager.emptyString
                            color: MonzeroComponents.Style.dimmedFontColor
                            font.pixelSize: 14
                        }
                    }

                    Column {
                        width: parent.width
                        padding: 16
                        spacing: 6

                        RowLayout {
                            width: parent.width - 32
                            MonzeroComponents.TextPlain {
                                Layout.fillWidth: true
                                text: root.kindLabel(root.kind).toUpperCase()
                                font.pixelSize: 11
                                font.bold: true
                                color: MonzeroComponents.Style.accent
                            }
                            MonzeroComponents.TextPlain {
                                text: root.studioRevision >= 0 && root.kind === "edition" && parseInt(editionInput.text, 10) > 0
                                      ? qsTr("Edition of ") + parseInt(editionInput.text, 10)
                                      : (root.kind === "fungible" ? symbolInput.text.trim().toUpperCase() : "")
                                font.pixelSize: 11
                                color: MonzeroComponents.Style.dimmedFontColor
                            }
                        }
                        MonzeroComponents.TextPlain {
                            width: parent.width - 32
                            wrapMode: Text.Wrap
                            text: nameInput.text.trim() || qsTr("Untitled")
                            textFormat: Text.PlainText
                            font.pixelSize: 19
                            font.bold: true
                            color: MonzeroComponents.Style.defaultFontColor
                        }
                        MonzeroComponents.TextPlain {
                            width: parent.width - 32
                            visible: creatorInput.text.trim().length > 0
                            text: qsTr("by ") + creatorInput.text.trim()
                            textFormat: Text.PlainText
                            font.pixelSize: 12
                            color: MonzeroComponents.Style.dimmedFontColor
                        }
                        MonzeroComponents.TextPlain {
                            width: parent.width - 32
                            visible: descriptionInput.text.trim().length > 0
                            wrapMode: Text.Wrap
                            maximumLineCount: 5
                            elide: Text.ElideRight
                            text: descriptionInput.text.trim()
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            color: MonzeroComponents.Style.defaultFontColor
                        }
                        Flow {
                            width: parent.width - 32
                            spacing: 6
                            visible: root.kind !== "fungible"
                            Repeater {
                                model: root.studioRevision >= 0 ? root.traitList() : []
                                delegate: Rectangle {
                                    width: traitText.implicitWidth + 18
                                    height: traitText.implicitHeight + 12
                                    radius: 7
                                    color: Qt.rgba(0.41, 0.84, 0.76, 0.1)
                                    border.width: 1
                                    border.color: Qt.rgba(0.41, 0.84, 0.76, 0.3)
                                    MonzeroComponents.TextPlain {
                                        id: traitText
                                        anchors.centerIn: parent
                                        textFormat: Text.PlainText
                                        text: modelData.trait_type + ": " + modelData.value
                                        font.pixelSize: 12
                                        color: MonzeroComponents.Style.defaultFontColor
                                    }
                                }
                            }
                        }
                        Rectangle { width: parent.width - 32; height: 1; color: MonzeroComponents.Style.inputBorderColorInActive }
                        RowLayout {
                            width: parent.width - 32
                            MonzeroComponents.TextPlain {
                                Layout.fillWidth: true
                                text: qsTr("Fingerprint") + translationManager.emptyString
                                font.pixelSize: 11
                                color: MonzeroComponents.Style.dimmedFontColor
                            }
                            MonzeroComponents.TextPlain {
                                text: root.metadataHash ? root.metadataHash.substring(0, 8) + "…" + root.metadataHash.substring(56) : "—"
                                font.family: isWindows ? "Consolas" : "monospace"
                                font.pixelSize: 11
                                color: MonzeroComponents.Style.accentBright
                            }
                        }
                    }
                }
            }
        }

        // ======================================================== Verify

        MonzeroComponents.Label {
            Layout.topMargin: 8
            fontSize: 19
            text: qsTr("Verify an NFT") + translationManager.emptyString
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: verifyColumn.height + 30
            radius: 10
            color: MonzeroComponents.Style.titleBarButtonHoverColor
            border.width: 1
            border.color: MonzeroComponents.Style.inputBorderColorInActive

            ColumnLayout {
                id: verifyColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 15
                spacing: 12

                MonzeroComponents.TextPlain {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: MonzeroComponents.Style.dimmedFontColor
                    text: qsTr("Check an asset's metadata file and image against their hashes before trusting what they show.") + translationManager.emptyString
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    MonzeroComponents.StandardButton {
                        primary: false
                        text: (root.verifyMeta ? root.verifyMeta.name : qsTr("Choose metadata file…")) + translationManager.emptyString
                        onClicked: root.chooseVerifyFile(false)
                    }
                    MonzeroComponents.StandardButton {
                        primary: false
                        text: (root.verifyImage ? root.verifyImage.name : qsTr("Choose image (optional)…")) + translationManager.emptyString
                        onClicked: root.chooseVerifyFile(true)
                    }
                }
                MonzeroComponents.LineEdit {
                    id: verifyExpectedInput
                    Layout.fillWidth: true
                    labelText: qsTr("Expected metadata hash") + translationManager.emptyString
                    placeholderText: qsTr("64-character hash from the asset") + translationManager.emptyString
                    onTextChanged: root.runVerify()
                }
                Repeater {
                    model: verifyModel
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MonzeroComponents.TextPlain {
                            text: root.levelMark(level)
                            font.bold: true
                            font.pixelSize: 13
                            color: root.levelColor(level)
                        }
                        MonzeroComponents.TextPlain {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            textFormat: Text.PlainText
                            text: message
                            font.pixelSize: 13
                            color: level === "bad" ? MonzeroComponents.Style.errorColor : MonzeroComponents.Style.defaultFontColor
                        }
                    }
                }
            }
        }
    }
}
