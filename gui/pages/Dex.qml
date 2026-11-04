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
    function formatLtcAtomic(value) {
        if (typeof value !== "string" || !/^(0|[1-9][0-9]{0,19})$/.test(value)) return "—"
        while (value.length < 9) value = "0" + value
        return value.slice(0, -8) + "." + value.slice(-8)
    }
    function parseLtcAtomic(value) {
        if (typeof value !== "string") return ""
        var match = /^(0|[1-9][0-9]{0,11})(?:\.([0-9]{1,8}))?$/.exec(value.trim())
        if (!match) return ""
        var fraction = match[2] || ""
        while (fraction.length < 8) fraction += "0"
        var atomic = (match[1] + fraction).replace(/^0+(?=[0-9])/, "")
        if (atomic.length > 20 || (atomic.length === 20 && atomic > "18446744073709551615")) return ""
        return atomic
    }
    property var ltcPaymentScan: ({gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000})
    property var ltcLoadedRequest: ({})
    property string ltcLoadedContext: ""
    property string ltcQuoteContext: ""
    property string ltcSubmittedContext: ""
    property string ltcPaymentError: ""
    property string ltcSendAttemptContext: ""
    property string ltcChainCheckContext: ""
    property string baseCoin: "LTC"
    property string pairName: baseCoin + " / XMZ"
    // Chain of the trade flow below, fixed at startup (Litecoin unless a Bitcoin stage was selected).
    // Release builds compile the DEX out and have no dexEngine: every read below goes through
    // this alias, which is then an empty object (fields read as undefined, no errors).
    readonly property var dex: dexPrototypeEnabled ? dexEngine : ({})
    property string swapCoin: root.dex.swapCoin || "LTC"
    property string swapCoinName: root.dex.swapCoinName || "Litecoin"
    function chainText(text) {
        return root.swapCoin === "LTC" ? text : text.replace(/Litecoin/g, root.swapCoinName).replace(/\bLTC\b/g, root.swapCoin)
    }
    // Consent applies only to the market the user was reviewing.
    onBaseCoinChanged: {
        ltcSubmittedContext = ""
        ltcQuoteContext = ""
        ltcLoadedRequest = ({})
        ltcLoadedContext = ""
        ltcPaymentError = ""
        ltcSendAttemptContext = ""
        ltcChainCheckContext = ""
        if (ltcBackupPassword) ltcBackupPassword.text=""
        if (ltcRestoreConsent) ltcRestoreConsent.checked=false
        if (ltcCreatePassword) ltcCreatePassword.text=""
        if (ltcCreateConfirmation) ltcCreateConfirmation.text=""
        if (ltcCreateConsent) ltcCreateConsent.checked=false
        if (quoteAcknowledgement) quoteAcknowledgement.checked = false
        if (bchAdmissionConsent) bchAdmissionConsent.checked = false
        if (bchRefundProtectionConsent) bchRefundProtectionConsent.checked = false
        bchRefundProtectionError = ""
        if (bchNativePreparationConsent) bchNativePreparationConsent.checked = false
        bchNativePreparationError = ""
        if (bchNativeRelayConsent) bchNativeRelayConsent.checked = false
        bchNativeRelayError = ""
        if (bchNativeProofConsent) bchNativeProofConsent.checked = false
        bchNativeProofError = ""
        bchNativeObservationError = ""
        bchSuccessSignatureReviewError = ""
        bchSuccessSignedReleaseReviewError = ""
        bchSuccessReleaseReviewError = ""
        bchPublicationRepairError = ""
        if (bchPublicationRepairConsent) bchPublicationRepairConsent.checked = false
        bchSuccessPreparationError = ""
        if (bchSuccessPreparationConsent) bchSuccessPreparationConsent.checked = false
        if (bchFundingReceiptConsent) bchFundingReceiptConsent.checked = false
        bchFundingReceiptError = ""
        if (bchRecoveryRetentionConsent) bchRecoveryRetentionConsent.checked = false
        bchRecoveryRetentionError = ""
        if (bchCancellationConsent) bchCancellationConsent.checked = false
        bchCancellationError = ""
        bchPunishmentError = ""
        bchRefundWalletError = ""
        if (bchPunishmentConsent) bchPunishmentConsent.checked = false
        if (bchRefundReleaseConsent) bchRefundReleaseConsent.checked = false
        bchRefundReleaseError = ""
        if (bchFundingConsent) bchFundingConsent.checked = false
        if (bchLockConsent) bchLockConsent.checked = false
        if (bchSigningConsent) bchSigningConsent.checked = false
        if (bchSigningRecoveryConsent) bchSigningRecoveryConsent.checked = false
        if (ltcStartConsent) ltcStartConsent.checked = false
        if (ltcLockConsent) ltcLockConsent.checked = false
        if (ltcRecoveryConsent) ltcRecoveryConsent.checked = false
        if (ltcRedeemConsent) ltcRedeemConsent.checked = false
        if (ltcTransactionConsent) ltcTransactionConsent.checked = false
        if (ltcFundingConsent) ltcFundingConsent.checked = false
        if (ltcRefundConsent) ltcRefundConsent.checked = false
        if (ltcPayoutConsent) ltcPayoutConsent.checked = false
    }
    property real lastPrice: 0
    property bool hasMarketData: false
    property real amountValue: Number(amountInput.text)
    property real priceValue: Number(priceInput.text)
    property real totalValue: amountValue * priceValue
    property real tradingFeeValue: 0 // Approved first release: no app trading fee.
    property real networkFeeXMZ: 0.0025
    property bool limitOrder: true
    property string previewError: ""
    // Release builds have no dexEngine; the hidden page must not call it.
    property var supportedPairs: dexPrototypeEnabled ? root.dex.supportedPairs() : []
    property var marketInfo: dexPrototypeEnabled ? (root.dex.marketInfo(pairName) || ({})) : ({})
    property string externalFeeLabel: qsTr("External network fee (") + baseCoin + ")"
    property var readiness: dexPrototypeEnabled ? dexEngine.activationReadiness(pairName, {})
        : ({ready:false, checksPassed:0, checksRequired:0, blockers:[]})
    property var marketSnapshot: ({})
    property var walletBackend: root.dex.nativeWalletStatus
    property var nativePreview: root.dex.nativeFeePreview
    property var nativeRecovery: root.dex.nativeRecoveryStatus
    property var litecoinLight: root.dex.litecoinLightStatus || ({})
    onLitecoinLightChanged: {
        if (ltcBackupPassword) ltcBackupPassword.text=""
        if (ltcCreatePassword) ltcCreatePassword.text=""
        if (ltcCreateConfirmation) ltcCreateConfirmation.text=""
        if (ltcCreateConsent) ltcCreateConsent.checked=false
    }
    property var bchPreparation: root.dex.bchPreparationStatus || ({})
    onBchPreparationChanged: { if (bchLockConsent) bchLockConsent.checked = false
        if (bchSigningConsent) bchSigningConsent.checked = false
        if (bchSigningRecoveryConsent) bchSigningRecoveryConsent.checked = false }
    property var bchFunding: root.dex.bchFundingStatus || ({})
    onBchFundingChanged: { if (bchFundingConsent) bchFundingConsent.checked = false }
    property var bchNativePreparation: root.dex.bchNativePreparationStatus || ({})
    property string bchNativePreparationError: ""
    onBchNativePreparationChanged: {
        if (bchNativePreparationConsent) bchNativePreparationConsent.checked = false
        bchNativePreparationError = ""
    }
    property var bchNativeRelay: root.dex.bchNativeRelayStatus || ({})
    property string bchNativeRelayError: ""
    onBchNativeRelayChanged: {
        if (bchNativeRelayConsent) bchNativeRelayConsent.checked = false
        bchNativeRelayError = ""
    }
    property var bchNativeProof: root.dex.bchNativeProofStatus || ({})
    property string bchNativeProofError: ""
    onBchNativeProofChanged: {
        if (bchNativeProofConsent) bchNativeProofConsent.checked = false
        bchNativeProofError = ""
    }
    property var bchSuccessSignatureReview: root.dex.bchSuccessSignatureReviewStatus || ({})
    property string bchSuccessSignatureReviewError: ""
    onBchSuccessSignatureReviewChanged: bchSuccessSignatureReviewError = ""
    property var bchSuccessSignedReleaseReview: root.dex.bchSuccessSignedReleaseReviewStatus || ({})
    property string bchSuccessSignedReleaseReviewError: ""
    onBchSuccessSignedReleaseReviewChanged: bchSuccessSignedReleaseReviewError = ""
    property var bchSuccessReleaseReview: root.dex.bchSuccessReleaseReviewStatus || ({})
    property string bchSuccessReleaseReviewError: ""
    onBchSuccessReleaseReviewChanged: {
        bchSuccessReleaseReviewError = ""
        if (bchSuccessPreparationConsent) bchSuccessPreparationConsent.checked = false
    }
    property var bchPublicationRepair: root.dex.bchPublicationRepairStatus || ({})
    property string bchPublicationRepairError: ""
    onBchPublicationRepairChanged: {
        bchPublicationRepairError = ""
        if (bchPublicationRepairConsent) bchPublicationRepairConsent.checked = false
    }
    property var bchSuccessPreparation: root.dex.bchSuccessPreparationStatus || ({})
    property string bchSuccessPreparationError: ""
    onBchSuccessPreparationChanged: {
        bchSuccessPreparationError = ""
        if (bchSuccessPreparationConsent) bchSuccessPreparationConsent.checked = false
    }
    property var bchNativeObservation: root.dex.bchNativeObservationStatus || ({})
    property var evmTrade: root.dex.evmTradeStatus || ({})
    // The lab panels are for testers; traders see the trade card first.
    property bool showAdvanced: false
    // The user's own wallets (optional), per coin; BNB keeps its earlier MetaMask setting
    property var externalWallets: {
        var m = {}
        try { m = JSON.parse(persistentSettings.dexExternalWallets || "{}") || {} } catch (e) { m = {} }
        if (!m.BNB && persistentSettings.dexExternalWallet) m.BNB = persistentSettings.dexExternalWallet
        return m
    }
    function setExternalWallet(coin, address) {
        var m = JSON.parse(JSON.stringify(root.externalWallets))
        if (address) m[coin] = address; else delete m[coin]
        persistentSettings.dexExternalWallets = JSON.stringify(m)
        if (coin === "BNB") persistentSettings.dexExternalWallet = address || ""
    }
    property string evmTradeError: ""
    function evmStageText(role, stage) {
        var owner = {
            "Start": qsTr("Ready to lock your BNB in the swap contract once you approve."),
            "LockSent": qsTr("Locking your BNB in the swap contract…"),
            "Locked": qsTr("Your BNB is locked. Waiting for the seller's XMZ to confirm."),
            "ReadySent": qsTr("XMZ confirmed. Letting the seller claim the BNB…"),
            "Ready": qsTr("XMZ confirmed. The seller can now claim the BNB."),
            "RefundSent": qsTr("Refunding your BNB…"),
            "Sweeping": qsTr("The seller claimed. Moving the XMZ into your wallet…"),
            "Done(Swapped)": qsTr("Swap complete: the XMZ is in your wallet."),
            "Done(Refunded)": qsTr("Refunded: your BNB is back in your account."),
            "Done(Aborted)": qsTr("Stopped before anything was locked.")
        }
        var claimer = {
            "WaitingForEvmLock": qsTr("Waiting for the buyer to lock their BNB."),
            "XmzLockSent": qsTr("Locking your XMZ…"),
            "XmzLocked": qsTr("Your XMZ is locked. Waiting for the buyer to confirm it."),
            "ClaimSent": qsTr("Claiming the BNB…"),
            "WaitingForRefund": qsTr("Too close to the deadline to claim safely. Waiting for the buyer's refund."),
            "SweepingBack": qsTr("The buyer refunded. Taking your XMZ back…"),
            "Done(Swapped)": qsTr("Swap complete: the BNB is in your account."),
            "Done(Refunded)": qsTr("Refunded: your XMZ is back in your wallet."),
            "Done(Aborted)": qsTr("Stopped before anything was locked.")
        }
        var table = role === "owner" ? owner : claimer
        return table[stage] || stage
    }
    property string bchNativeObservationError: ""
    onBchNativeObservationChanged: bchNativeObservationError = ""
    property var bchFundingReceipt: root.dex.bchFundingReceiptStatus || ({})
    property string bchFundingReceiptError: ""
    onBchFundingReceiptChanged: {
        if (bchFundingReceiptConsent) bchFundingReceiptConsent.checked = false
        bchFundingReceiptError = ""
    }
    property var bchRefundProtection: root.dex.bchRefundProtectionStatus || ({})
    property string bchRefundProtectionError: ""
    onBchRefundProtectionChanged: {
        if (bchRefundProtectionConsent) bchRefundProtectionConsent.checked = false
        bchRefundProtectionError = ""
    }
    property var bchRecoveryRetention: root.dex.bchRecoveryRetentionStatus || ({})
    property string bchRecoveryRetentionError: ""
    onBchRecoveryRetentionChanged: {
        if (bchRecoveryRetentionConsent) bchRecoveryRetentionConsent.checked = false
        bchRecoveryRetentionError = ""
    }
    property var bchRefundWallet: root.dex.bchRefundWalletStatus || ({})
    property string bchRefundWalletError: ""
    onBchRefundWalletChanged: {
        bchRefundWalletError = ""
        if (bchRefundWalletApproval) bchRefundWalletApproval.clear()
    }
    property var bchPunishment: root.dex.bchPunishmentStatus || ({})
    property string bchPunishmentError: ""
    onBchPunishmentChanged: {
        bchPunishmentError = ""
        if (bchPunishmentConsent) bchPunishmentConsent.checked = false
    }
    property var bchCancellation: root.dex.bchCancellationStatus || ({})
    property string bchCancellationError: ""
    onBchCancellationChanged: {
        if (bchCancellationConsent) bchCancellationConsent.checked = false
        bchCancellationError = ""
    }
    property var bchRefundRelease: root.dex.bchRefundReleaseStatus || ({})
    property string bchRefundReleaseError: ""
    onBchRefundReleaseChanged: {
        if (bchRefundReleaseConsent) bchRefundReleaseConsent.checked = false
        bchRefundReleaseError = ""
    }
    property var bchLightFunding: root.dex.bchLightFundingStatus || ({})
    property var bchAdmission: root.dex.bchAdmissionStatus || ({})
    onBchAdmissionChanged: { if (bchAdmissionConsent) bchAdmissionConsent.checked = false }
    property var litecoinNegotiation: root.dex.litecoinNegotiationStatus
    property var lightDexFunding: root.dex.litecoinLightFundingStatus || ({})
    property var litecoinReservation: root.dex.litecoinReservationStatus || ({})
    property var litecoinRedemption: root.dex.litecoinRedemptionStatus || ({})
    onLitecoinRedemptionChanged: { if (ltcTransactionConsent) ltcTransactionConsent.checked = false }
    property var litecoinJournal: root.dex.litecoinJournalStatus
    property var litecoinApp: root.dex.litecoinAppStatus || ({})
    property var litecoinFunding: root.dex.litecoinFundingStatus || litecoinJournal
    property var litecoinPayment: root.dex.litecoinPaymentStatus || litecoinJournal
    onLitecoinFundingChanged: { if (ltcFundingConsent) ltcFundingConsent.checked = false }
    onLitecoinPaymentChanged: { if (ltcRefundConsent) ltcRefundConsent.checked = false; if (ltcPayoutConsent) ltcPayoutConsent.checked = false }
    onLitecoinNegotiationChanged: { if (ltcRedeemConsent) ltcRedeemConsent.checked = false }
    onLitecoinReservationChanged: { if (ltcLockConsent) ltcLockConsent.checked = false; if (ltcRecoveryConsent) ltcRecoveryConsent.checked = false }
    onLitecoinJournalChanged: { if (ltcStartConsent) ltcStartConsent.checked = false; if (ltcFundingConsent) ltcFundingConsent.checked = false; if (ltcRefundConsent) ltcRefundConsent.checked = false; if (ltcPayoutConsent) ltcPayoutConsent.checked = false }
    property var quoteReview: root.dex.nativeQuoteReview || ({})
    onQuoteReviewChanged: { if (quoteAcknowledgement) quoteAcknowledgement.checked = false }
    property string nativePreviewError: ""
    function exactAtomicText(value, decimals) {
        if (typeof value !== "string" || !/^[0-9]+$/.test(value)) return qsTr("Unavailable")
        var digits = value.replace(/^0+/, "") || "0"
        while (digits.length <= decimals) digits = "0" + digits
        var fraction = digits.slice(-decimals).replace(/0+$/, "")
        return digits.slice(0, -decimals) + (fraction.length ? "." + fraction : "")
    }
    function walletBackendText() {
        switch (walletBackend.state) {
        case "unknown": return qsTr("Swap wallet: identity not checked");
        case "closed": return qsTr("Swap wallet: closed");
        case "open": return qsTr("Swap wallet: identity verified — trading remains disabled");
        case "busy": return qsTr("Swap wallet: operation in progress");
        case "recovery-required": return qsTr("Swap wallet: recovery required — do not retry funding");
        case "stopped": return qsTr("Swap wallet: service stopped");
        default: return qsTr("Swap wallet backend not attached — research preview only");
        }
    }
    Connections {
        target: dexPrototypeEnabled ? dexEngine : null
        onResearchMarketReady: { root.marketSnapshot = snapshot; root.rebuildOrderBook() }
    }

    ListModel { id: asks }
    ListModel { id: bids }
    ListModel { id: orders }
    Timer {
        interval: 1000; running: dexPrototypeEnabled; repeat: true
        property int tick: 0
        onTriggered: {
            refreshOffers()
            tick++
            if (tick % 5 === 0 && snapshotPath.text.trim().length > 0 && !root.dex.researchMarketBusy)
                root.dex.refreshResearchMarket(snapshotPath.text)
        }
    }
    Component.onCompleted: { if (!dexPrototypeEnabled) return; if (root.swapCoin !== "LTC" || (root.litecoinJournal && root.litecoinJournal.startConfigured === true)) selectPair(root.swapCoin + " / XMZ"); refreshOffers(); rebuildOrderBook() }

    function atomicToNumber(text, decimals) {
        var n = Number(text)
        return isFinite(n) ? n / Math.pow(10, decimals) : 0
    }
    function normalizedPair(pair) { return String(pair).replace(/\s+/g, "") }
    // Base-coin atomic decimals for the selected market; -1 when unknown so nothing is mis-scaled.
    function marketDecimals() {
        var d = Number(root.marketInfo.decimals)
        return isFinite(d) && d >= 0 && d <= 30 && Math.floor(d) === d ? d : -1
    }
    function rebuildOrderBook() {
        asks.clear()
        bids.clear()
        var amountDecimals = root.marketDecimals()
        if (!root.marketSnapshot.valid || amountDecimals < 0) { root.hasMarketData = false; root.lastPrice = 0; return }
        var offers = root.marketSnapshot.offers || []
        var wantPair = root.normalizedPair(root.pairName)
        var askList = [], bidList = []
        for (var i = 0; i < offers.length; ++i) {
            var o = offers[i]
            if (root.normalizedPair(o.pair) !== wantPair) continue
            var priceNum = root.atomicToNumber(o.priceAtomic, 11)
            var amountNum = root.atomicToNumber(o.amountAtomic, amountDecimals)
            if (priceNum <= 0 || amountNum <= 0) continue
            var entry = { p: priceNum.toFixed(5), a: amountNum.toFixed(5), priceNum: priceNum }
            var side = String(o.side).toUpperCase()
            if (side === "SELL") askList.push(entry)
            else if (side === "BUY") bidList.push(entry)
        }
        askList.sort(function (x, y) { return x.priceNum - y.priceNum })
        bidList.sort(function (x, y) { return y.priceNum - x.priceNum })
        for (var j = 0; j < askList.length; ++j) asks.append({ p: askList[j].p, a: askList[j].a })
        for (var k = 0; k < bidList.length; ++k) bids.append({ p: bidList[k].p, a: bidList[k].a })
        root.hasMarketData = askList.length > 0 || bidList.length > 0
        if (askList.length && bidList.length) root.lastPrice = (askList[0].priceNum + bidList[0].priceNum) / 2
        else if (askList.length) root.lastPrice = askList[0].priceNum
        else if (bidList.length) root.lastPrice = bidList[0].priceNum
        else root.lastPrice = 0
    }
    function refreshOffers() {
        var active = root.dex.localOffers()
        orders.clear()
        for (var i = 0; i < active.length; ++i) {
            var result = active[i]
            if (result.pair !== root.pairName) continue
            orders.append({offerId:result.id, sideText:result.side, amountText:result.amount,
                priceText:result.price, feeText:result.protocolFeeXmz+" protocol + "+result.networkFeeXmz+" (unverified estimate)",
                minerFeeText:result.externalMinerFee, settlementLabel:result.settlementLabel,
                settlementText:result.settlementXmz})
        }
    }

    function fixed(value, places) { return isFinite(value) && value > 0 ? value.toFixed(places) : (0).toFixed(places) }
    function preview(side) {
        var result = root.dex.createLocalOffer(root.pairName, side, amountInput.text, priceInput.text, 3600)
        if (!result.valid) { previewError = result.error; return }
        previewError = ""
        refreshOffers()
    }
    function showLitecoinCreation() {
        if (root.baseCoin !== "LTC") return
        var target
        if (root.litecoinLight.configured === true) {
            if (root.litecoinLight.setupConfigured !== true) return
            target=ltcCreateForm
            if (ltcCreatePassword.visible) ltcCreatePassword.forceActiveFocus()
        } else {
            if (root.dex.lightWalletSetupAvailable !== true) return
            target=localWalletSetup.focusCreation()
        }
        var container=root.parent
        while (container) {
            if (typeof container.contentY === "number" && container.contentItem) {
                var position=target.mapToItem(container.contentItem,0,0)
                container.contentY=Math.max(0,Math.min(position.y-24,Math.max(0,container.contentHeight-container.height)))
                break
            }
            container=container.parent
        }
    }
    function baseOf(pair) { return String(pair).split("/")[0].trim() }
    function selectPair(pair) {
        baseCoin = baseOf(pair)
        amountInput.text = ""
        rebuildOrderBook()
        refreshOffers()
    }

    ColumnLayout {
        id: page
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            M.Label { fontSize: 25; text: qsTr("Trade") + translationManager.emptyString }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: offersBadge.implicitWidth + 28; Layout.preferredHeight: 30; radius: 15
                color: "transparent"; border.width: 1; border.color: root.hasMarketData ? M.Style.accent : M.Style.accentMuted
                M.TextPlain { id: offersBadge; themeTransition: false; anchors.centerIn: parent; font.pixelSize: 12
                    color: root.hasMarketData ? M.Style.accent : M.Style.dimmedFontColor
                    text: (root.hasMarketData ? qsTr("Verified offers") : qsTr("No offers yet")) + translationManager.emptyString }
            }
        }

        // ---- The simple way to trade: one active trade, then a new trade card
        M.DexTradeProgress {
            id: tradeProgress
            objectName: "tradeProgress"
            Layout.fillWidth: true
            visible: root.evmTrade.configured === true
            trade: root.evmTrade
            coin: "BNB"
            errorText: root.evmTradeError
            onApprove: root.evmTradeError = root.dex.approveEvmTrade(commitment) ? "" : qsTr("Approval could not start.")
            onRefresh: root.evmTradeError = root.dex.refreshEvmTrade() ? "" : qsTr("Refresh could not start.")
            externalWallet: root.externalWallets.BNB || ""
            onPayoutRequested: root.evmTradeError = root.dex.payoutEvmTrade(root.externalWallets.BNB || "") ? "" : qsTr("The payout could not start.")
        }
        // Pair tabs, as on an exchange: each shows its last price and 24 h change from completed trades
        M.DexPairTabs {
            objectName: "pairTabs"
            Layout.fillWidth: true
            coins: tradeCard.coins
            coin: tradeCard.coin
            market: marketPanel
            coinDecimals: tradeCard.coinDecimals
            onPicked: { tradeCard.coin = coin; tradeCard.reviewing = false }
        }

        // Wide windows: market on the left, the trade form beside it; narrow: one column
        GridLayout {
            id: tradeArea
            Layout.fillWidth: true
            columns: root.width > 1100 ? 2 : 1
            columnSpacing: 12; rowSpacing: 12
            M.DexMarketPanel {
                id: marketPanel
                objectName: "marketPanel"
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                coin: tradeCard.coin
                coinDecimals: tradeCard.coinDecimals[tradeCard.coin] || 8
                offers: tradeCard.offers
                trades: root.marketSnapshot.valid === true ? (root.marketSnapshot.trades || []) : []
                // A SELL offer sells the coin for XMZ: taking it means selling XMZ; a BUY offer, buying XMZ
                onOfferPicked: {
                    tradeCard.direction = String(offer.side).toUpperCase() === "SELL" ? "sell" : "buy"
                    var xmz = Number(offer.priceAtomic) / 1e11 * Number(offer.amountAtomic) / Math.pow(10, coinDecimals)
                    tradeCard.amountText = tradeCard.trim(Math.floor(xmz * 1e6) / 1e6, 6)   // down: never more than the offer covers
                }
            }
            ColumnLayout {
                Layout.fillWidth: tradeArea.columns === 1
                Layout.preferredWidth: tradeArea.columns === 1 ? -1 : 380
                Layout.alignment: Qt.AlignTop
                spacing: 12
                M.DexTradeCard {
                    id: tradeCard
                    objectName: "tradeCard"
                    Layout.fillWidth: true
                    showCoinPicker: false
                wallets: root.externalWallets
                    offers: root.marketSnapshot.valid === true ? (root.marketSnapshot.offers || []) : []
                    xmzBalance: appWindow.currentWallet ? Number(walletManager.displayAmount(appWindow.currentWallet.unlockedBalanceAll())) : -1
                    canStart: false
                    startBlockedReason: qsTr("Testnet preview: taking an offer from the app comes next. Testnet trades are started with the trade kit for now.")
                }
                M.TextPlain {
                    themeTransition: false; color: M.Style.defaultFontColor; font.pixelSize: 15
                    Layout.topMargin: 4
                    text: qsTr("Your wallets (optional)")
                }
                Repeater {
                    // The selected market's coin first
                    model: [tradeCard.coin].concat(tradeCard.coins.filter(function (c) { return c !== tradeCard.coin }))
                    delegate: M.DexExternalWallet {
                        objectName: "externalWallet_" + modelData
                        Layout.fillWidth: true
                        coin: modelData
                        address: root.externalWallets[modelData] || ""
                        onConnectRequested: root.setExternalWallet(modelData, address)
                        onDisconnectRequested: root.setExternalWallet(modelData, "")
                    }
                }
            }
        }

        // ---- Everything else: wallets, recovery and the testnet lab, closed by default
        Rectangle {
            objectName: "advancedToggle"
            Layout.fillWidth: true; implicitHeight: 44; radius: 8
            color: "transparent"; border.color: M.Style.accentMuted
            M.TextPlain {
                anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter
                text: (root.showAdvanced ? "▾ " : "▸ ") + qsTr("Advanced: coin wallets, recovery and testnet lab")
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.showAdvanced = !root.showAdvanced }
        }
        ColumnLayout {
            id: advancedTools
            objectName: "advancedTools"
            Layout.fillWidth: true
            visible: root.showAdvanced
            spacing: 12
        Rectangle {
            objectName: "litecoinWalletOverview"
            // Litecoin wallet card; on a Bitcoin stage the BTC wallet panel below takes its place.
            visible: root.swapCoin === "LTC"
            Layout.fillWidth: true; implicitHeight: ltcOverviewContent.implicitHeight + 32
            radius: 8; color: "#202b36"; border.color: "#526b80"
            ColumnLayout {
                id: ltcOverviewContent
                anchors.fill: parent; anchors.margins: 16; spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    M.TextPlain {text: qsTr("Litecoin wallet");font.bold:true;font.pixelSize:20;color:"#f0f5fa"}
                    Item {Layout.fillWidth:true}
                    M.StandardButton {
                        objectName:"litecoinWalletOpenMarket";text:qsTr("Open LTC / XMZ")
                        visible:root.baseCoin !== "LTC"
                        onClicked:root.selectPair("LTC / XMZ")
                    }
                }
                M.TextPlain {
                    objectName:"litecoinWalletOverviewStatus";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:"#d5e1eb"
                    text:root.baseCoin !== "LTC" ? qsTr("Open LTC / XMZ to manage your local Litecoin wallet alongside the DEX.")
                        :root.litecoinLight.configured !== true
                            ? (root.dex.lightWalletSetupAvailable === true
                                ? qsTr("No Litecoin wallet open. Create a wallet, restore recovery words, or open your saved wallet folder using the controls below.")
                                : qsTr("Litecoin wallet setup is unavailable in this session. The DEX prototype needs its verified local wallet helper."))
                        :root.litecoinLight.setupConfigured === true ? qsTr("Wallet settings prepared. Review the setup below to create or restore your wallet.")
                        :qsTr("Wallet configured · %1
Backup and recovery controls are below.").arg(root.litecoinLight.network || qsTr("Network not checked"))
                }
                M.TextPlain {
                    objectName:"litecoinWalletOverviewBalance";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:"#f0f5fa"
                    property var wallet:root.litecoinLight.wallet || ({})
                    property bool observed:root.litecoinLight.state === "wallet-checked" && root.litecoinLight.busy !== true
                        && wallet.balanceStatus === "provisional" && wallet.watchOnly === true && wallet.spendable === false
                        && root.formatLtcAtomic(wallet.observedConfirmedAtomic) !== "—"
                    visible:root.baseCoin === "LTC"
                    text:observed ? qsTr("Observed confirmed: %1 LTC · provisional, not a spendable balance").arg(root.formatLtcAtomic(wallet.observedConfirmedAtomic))
                        :root.litecoinLight.configured !== true ? qsTr("No wallet selected")
                        :root.litecoinLight.busy === true ? qsTr("Wallet operation in progress…")
                        :qsTr("No current balance available · refresh to check")
                }
                M.StandardButton {
                    objectName:"litecoinOverviewCreate";text:root.litecoinLight.setupConfigured === true ? qsTr("Continue setup") : qsTr("Create wallet")
                    visible:root.baseCoin === "LTC" && (root.litecoinLight.configured !== true || root.litecoinLight.setupConfigured === true)
                    enabled:root.litecoinLight.busy !== true && (root.litecoinLight.setupConfigured === true || root.dex.lightWalletSetupAvailable === true)
                    onClicked: {if (enabled) root.showLitecoinCreation()}
                }
                Flow {
                    Layout.fillWidth:true;spacing:8
                    visible:root.baseCoin === "LTC" && root.litecoinLight.configured === true
                    M.StandardButton {
                        objectName:"litecoinOverviewReceive";text:qsTr("Receive")
                        enabled:root.litecoinLight.walletReceiveAvailable === true && root.litecoinLight.busy !== true
                        onClicked: {if (enabled) root.dex.showLitecoinReceivingAddress()}
                    }
                    M.StandardButton {
                        objectName:"litecoinOverviewRefresh";text:qsTr("Refresh balance")
                        enabled:root.litecoinLight.walletRefreshAvailable === true && root.litecoinLight.busy !== true
                        onClicked: {if (enabled) root.dex.refreshLitecoinLightWallet(20,500,1000)}
                    }
                    M.StandardButton {
                        objectName:"litecoinOverviewSync";text:qsTr("Sync headers")
                        enabled:root.litecoinLight.syncAvailable === true && root.litecoinLight.busy !== true
                        onClicked: {if (enabled) root.dex.synchronizeLitecoinLight(10000000)}
                    }
                }
                M.TextPlain {
                    objectName:"litecoinOverviewAddress";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:"#f0f5fa"
                    property var wallet:root.litecoinLight.wallet || ({})
                    visible:root.baseCoin === "LTC" && root.litecoinLight.state === "wallet-address" && root.litecoinLight.busy !== true
                        && wallet.watchOnly === true && wallet.spendable === false && typeof wallet.address === "string" && wallet.address.length>0
                        && typeof root.litecoinLight.network === "string" && root.litecoinLight.network.length>0
                    text:visible ? qsTr("Receiving address (%1)\n%2\nKeep the matching wallet keys to spend received coins.").arg(root.litecoinLight.network).arg(wallet.address) : ""
                }
                M.TextPlain {
                    Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#b8c9d8"
                    text:qsTr("Keys and recovery words stay on your device. Wallet management is available separately from DEX trading, which remains in development.")
                }
            }
        }
        M.WarningBox {
            text: qsTr("EXPERIMENTAL DEX RESEARCH PREVIEW — NOT REAL-MONEY TRADING. The order book and reference price below are built from cryptographically verified signed offers when a receiver snapshot is loaded, but this proves nothing about liquidity, and an empty book only means no verified offers were found. The candle chart and balances remain illustrative demo data. Order-book offers cannot be signed or broadcast yet. Test trades set up from a signed quote settle on test networks only (Bitcoin signet or testnet4, Litecoin testnet, the XMZ test network); mainnet trading is disabled. The initial release is planned without an app trading fee; native chain transaction fees still apply. Do not send funds to test a DEX order or share wallet passwords, seeds or private keys. Bypassing these safeguards could cause permanent loss of funds.") + translationManager.emptyString
        }

        M.TextPlain {
            objectName:"lightWalletSetupNotice";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
            text:root.dex.lightWalletSetupNotices ? (root.dex.lightWalletSetupNotices[root.baseCoin.toLowerCase()] || "") : ""
            visible:text.length>0
        }
        M.LightWalletBackupRestorePanel {
            Layout.fillWidth:true;backend:root.dex;coin:root.baseCoin.toLowerCase();walletStatus:coin === "ltc" ? root.litecoinLight : (root.dex.lightWallets ? (root.dex.lightWallets[coin] || ({})) : ({}))
        }
        M.LightWalletSetupPanel {
            id:localWalletSetup
            Layout.fillWidth: true
            backend: root.dex
            coin: root.baseCoin.toLowerCase()
            walletStatus: coin === "ltc" ? root.litecoinLight : (root.dex.lightWallets ? (root.dex.lightWallets[coin] || ({})) : ({}))
        }

        M.LightWalletImportPanel {
            Layout.fillWidth:true;backend:root.dex;coin:root.baseCoin.toLowerCase()
            walletStatus:coin === "ltc" ? root.litecoinLight : (root.dex.lightWallets ? (root.dex.lightWallets[coin] || ({})) : ({}))
        }
        M.LightWalletDiscoveryPanel {
            Layout.fillWidth:true
            backend:root.dex;coin:root.baseCoin.toLowerCase()
            walletStatus:coin === "ltc" ? root.litecoinLight : (root.dex.lightWallets ? (root.dex.lightWallets[coin] || ({})) : ({}))
        }
        M.LightWalletImportRecoveryPanel {
            Layout.fillWidth:true;backend:root.dex;coin:root.baseCoin.toLowerCase()
            walletStatus:coin === "ltc" ? root.litecoinLight : (root.dex.lightWallets ? (root.dex.lightWallets[coin] || ({})) : ({}))
        }
        M.LightWalletImportActivationPanel {
            Layout.fillWidth:true;backend:root.dex;coin:root.baseCoin.toLowerCase()
            walletStatus:coin === "ltc" ? root.litecoinLight : (root.dex.lightWallets ? (root.dex.lightWallets[coin] || ({})) : ({}))
        }



        M.LightWalletPanel {
            Layout.fillWidth: true
            backend: root.dex
            coin: root.baseCoin.toLowerCase()
            walletStatus: coin !== "ltc" && root.dex.lightWallets ? (root.dex.lightWallets[coin] || ({})) : ({})
        }

        ColumnLayout {
            objectName: "litecoinLightPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "LTC" && root.litecoinLight.configured === true
            M.LightWalletRecoveryPanel {
                Layout.fillWidth: true
                backend: root.dex
                coin: "ltc"
                walletStatus: root.litecoinLight
            }
            M.TextPlain { text: qsTr("Litecoin light wallet — in development"); color: M.Style.defaultFontColor; font.bold: true }
            M.TextPlain {
                Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;color:M.Style.defaultFontColor;textFormat:Text.PlainText
                text:qsTr("Wallet folder: %1\nUse this folder to reopen the wallet after restarting the app.").arg(root.litecoinLight.walletFolder || "")
            }
            M.TextPlain {
                objectName: "lightWalletCustodyNotice"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: qsTr("Your wallet keys are encrypted on this device. Keep your password and a separate encrypted backup: Monzero does not keep a recovery copy. Network servers provide chain data and can observe wallet queries. This backup covers keys and address records, not active DEX trades.") + translationManager.emptyString
            }
            ColumnLayout {
                id:ltcCreateForm;objectName:"litecoinLightCreateForm";Layout.fillWidth:true
                visible:root.litecoinLight.setupConfigured === true
                property var settings:root.litecoinLight.creation || ({})
                property string error:""
                onVisibleChanged: if (!visible) {
                    if (ltcCreatePassword) ltcCreatePassword.text=""
                    if (ltcCreateConfirmation) ltcCreateConfirmation.text=""
                    if (ltcCreateConsent) ltcCreateConsent.checked=false
                }
                M.TextPlain {
                    objectName:"litecoinLightCreateDetails";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    text:qsTr("Review Litecoin wallet setup
Network: %1
Wallet ID: %2
Account index: %3
Setup records: %4
Key vault: %5
Address records: %6
Payment records: %7
Header cache: %8
Initial backup folder: %9
TLS server: %10
Maximum stored block height: %11")
                        .arg(root.litecoinLight.network).arg(ltcCreateForm.settings.walletId || "").arg(ltcCreateForm.settings.accountIndex || 0)
                        .arg(ltcCreateForm.settings.setupDirectory || "").arg(ltcCreateForm.settings.vaultDirectory || "")
                        .arg(ltcCreateForm.settings.addressBookDirectory || "").arg(ltcCreateForm.settings.spendDirectory || "")
                        .arg(ltcCreateForm.settings.cacheDirectory || "").arg(ltcCreateForm.settings.backupDirectory || "")
                        .arg(ltcCreateForm.settings.endpoint?ltcCreateForm.settings.endpoint.host+":"+ltcCreateForm.settings.endpoint.port:qsTr("Not configured"))
                        .arg(ltcCreateForm.settings.maximumHeight || 0)
                }
                M.TextPlain {
                    objectName:"litecoinWalletRecoveryFormat";Layout.fillWidth:true;wrapMode:Text.Wrap
                    textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    text:ltcCreateForm.settings.seedEncoding === "bip39-english-v1"
                        ? qsTr("Recovery format: 24 English BIP39 words, with no additional BIP39 passphrase.")
                        : qsTr("Recovery format: raw BIP32 seed. This wallet does not use recovery words.")
                }
                M.TextPlain {
                    Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    text:qsTr("Creation saves encrypted keys and verifies an initial seed/address backup before reporting success. It does not connect to the server. Keep all setup folders after an interruption; an identical retry reuses the saved keys.
The initial backup does not include future addresses, payment records or active swap recovery. Keep a separate verified copy and make updated backups as the wallet is used.")
                }
                M.TextPlain {
                    Layout.fillWidth:true;wrapMode:Text.Wrap;color:M.Style.defaultFontColor
                    visible:root.litecoinLight.busy === true
                    text:qsTr("Creating the wallet and verifying its initial backup…")
                }
                M.LineEdit {
                    id:ltcCreatePassword;objectName:"litecoinLightCreatePassword";Layout.fillWidth:true;password:true
                    labelText:qsTr("New wallet password");readOnly:root.litecoinLight.busy === true
                    visible:root.litecoinLight.walletCreateAvailable === true
                }
                M.LineEdit {
                    id:ltcCreateConfirmation;objectName:"litecoinLightCreateConfirmation";Layout.fillWidth:true;password:true
                    labelText:qsTr("Confirm wallet password");readOnly:root.litecoinLight.busy === true
                    visible:root.litecoinLight.walletCreateAvailable === true
                }
                CheckBox {
                    id:ltcCreateConsent;objectName:"litecoinLightCreateConsent";Layout.fillWidth:true
                    visible:root.litecoinLight.walletCreateAvailable === true
                    text:qsTr("I approve creating or resuming this wallet with the network and folders shown above.")
                    contentItem:M.TextPlain {
                        text:ltcCreateConsent.text;color:M.Style.defaultFontColor;wrapMode:Text.Wrap;textFormat:Text.PlainText
                        leftPadding:ltcCreateConsent.indicator.width+ltcCreateConsent.spacing
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightCreate";text:qsTr("Create or resume wallet");visible:root.litecoinLight.walletCreateAvailable === true
                    enabled:visible && root.litecoinLight.busy !== true && ltcCreateConsent.checked && !!ltcCreatePassword.text
                        && ltcCreatePassword.text === ltcCreateConfirmation.text
                    onClicked: {
                        if (!enabled) return
                        var password=ltcCreatePassword.text,confirmation=ltcCreateConfirmation.text
                        ltcCreatePassword.text="";ltcCreateConfirmation.text="";ltcCreateConsent.checked=false;ltcCreateForm.error=""
                        var accepted=root.dex.createLitecoinWallet(root.litecoinLight.creationDigest,password,confirmation);password="";confirmation=""
                        if (!accepted) ltcCreateForm.error=qsTr("Could not start creation. Preserve the setup folders and review the saved wallet before retrying.")
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightCreatedReview";text:qsTr("Find saved wallet")
                    enabled:root.litecoinLight.walletCreatedReviewAvailable === true && root.litecoinLight.busy !== true
                    onClicked: {
                        if (!enabled) return
                        ltcCreatePassword.text="";ltcCreateConfirmation.text="";ltcCreateConsent.checked=false;ltcCreateForm.error=""
                        if (!root.dex.reviewCreatedLitecoinWallet()) ltcCreateForm.error=qsTr("No matching saved configuration could be reviewed. No replacement wallet was created.")
                    }
                }
                M.TextPlain {
                    objectName:"litecoinLightCreatedReceipt";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet:root.litecoinLight.wallet || ({})
                    visible:(root.litecoinLight.state === "wallet-created" || root.litecoinLight.state === "wallet-created-review") && root.litecoinLight.busy !== true
                    text:visible?(root.litecoinLight.state === "wallet-created"?qsTr("Wallet created. Initial encrypted backup verified."):qsTr("Saved wallet configuration reviewed. Backup contents have not been rechecked."))
                        +"
"+qsTr("Wallet ID: %1
Configuration: %2
Backup digest: %3
Open this wallet to use its receiving and payment controls. Trading remains unavailable.")
                        .arg(wallet.walletId).arg(wallet.configurationFile).arg(wallet.backupDigest):""
                }
                M.StandardButton {
                    objectName:"litecoinLightCreatedOpen";text:qsTr("Open wallet")
                    visible:root.litecoinLight.walletCreatedOpenAvailable === true
                    enabled:visible && root.litecoinLight.busy !== true
                    onClicked: {
                        if (!enabled) return
                        ltcCreateForm.error=""
                        if (!root.dex.openCreatedLitecoinWallet((root.litecoinLight.wallet || ({})).configurationDigest))
                            ltcCreateForm.error=qsTr("The wallet configuration changed or could not be opened. Find and review the saved wallet again.")
                    }
                }
                M.TextPlain {
                    objectName:"litecoinLightCreateError";Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    visible:!!ltcCreateForm.error || root.litecoinLight.state === "unknown"
                    text:ltcCreateForm.error || qsTr("The setup result could not be verified. Preserve all setup folders and find the saved wallet before retrying.")
                }
            }
            M.TextPlain {
                objectName: "litecoinLightSummary"
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                property var chain: root.litecoinLight.chain || ({})
                visible:root.litecoinLight.setupConfigured !== true
                text: root.litecoinLight.busy === true
                    ? (root.litecoinLight.state === "synchronizing" ? qsTr("Synchronizing Litecoin headers…") : root.litecoinLight.state === "wallet-loading" ? qsTr("Updating watch-only wallet…") : qsTr("Loading saved headers…"))
                    : root.litecoinLight.state === "unknown" ? qsTr("Could not verify Litecoin data. Reload headers or refresh the wallet again.")
                    : typeof chain.height === "number" ? qsTr("Network: %1 · Stored header height: %2").arg(root.litecoinLight.network).arg(chain.height)
                    : qsTr("No saved header status has been loaded.")
            }
            M.TextPlain {
                objectName: "litecoinLightSyncDetails"
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                property var result: root.litecoinLight.synchronization || ({})
                visible: typeof result.reportedHeight === "number" && root.litecoinLight.busy !== true
                text: visible ? qsTr("Server-reported height: %1 · Headers downloaded: %2").arg(result.reportedHeight).arg(result.downloadedHeaders) : ""
            }
            M.TextPlain {
                objectName: "litecoinLightBoundary"
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: root.litecoinLight.walletConfigured === true
                    ? qsTr("Watch-only observations may omit transactions. Network freshness is unassessed; these amounts are not spendable and trading is not available.")
                    : qsTr("Network freshness has not yet been established. Wallet balances and trading are not available in this mode.")
            }
            M.TextPlain {
                objectName: "litecoinLightWalletAmounts"
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                property var wallet: root.litecoinLight.wallet || ({})
                visible: root.litecoinLight.state === "wallet-checked" && root.litecoinLight.busy !== true
                    && wallet.balanceStatus === "provisional" && wallet.watchOnly === true && wallet.spendable === false
                text: visible ? qsTr("Provisional amounts (LTC)\nObserved confirmed after pending spends: %1\nImmature: %2\nReserved by reported pending spends: %3\nReported pending receipts: %4")
                    .arg(root.formatLtcAtomic(wallet.observedConfirmedAtomic)).arg(root.formatLtcAtomic(wallet.immatureAtomic))
                    .arg(root.formatLtcAtomic(wallet.pendingSpentAtomic)).arg(root.formatLtcAtomic(wallet.pendingReceivedAtomic)) : ""
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: M.Style.defaultFontColor
                visible: root.litecoinLight.state === "wallet-ready"
                text: qsTr("Watch-only account initialized. Synchronize headers, then refresh its provisional amounts.")
            }
            M.TextPlain {
                objectName: "litecoinLightReceivingAddress"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                property var wallet: root.litecoinLight.wallet || ({})
                visible: root.litecoinLight.state === "wallet-address" && root.litecoinLight.busy !== true
                    && wallet.watchOnly === true && wallet.spendable === false && typeof wallet.address === "string"
                text: visible ? qsTr("Watch-only receiving address (%1)\n%2\nSpending requires the matching wallet keys. Trading is not available yet.").arg(root.litecoinLight.network).arg(wallet.address) : ""
            }
            M.StandardButton {
                objectName: "litecoinLightReceive"; text: qsTr("Show receiving address")
                visible: root.litecoinLight.walletConfigured === true
                enabled: root.litecoinLight.walletReceiveAvailable === true
                onClicked: root.dex.showLitecoinReceivingAddress()
            }
            ColumnLayout {
                id:ltcBackupForm;objectName:"litecoinLightBackupForm";Layout.fillWidth:true
                visible:root.litecoinLight.walletBackupConfigured === true
                property string submittedDirectory:""
                property string error:""
                property bool restoreAttempt:false
                property var restoreDirectories:root.litecoinLight.restoreDirectories || ({})
                property bool restoreReady:visible && ltcBackupReceipt.visible && root.litecoinLight.state === "wallet-backup-verified"
                    && root.litecoinLight.walletRestoreAvailable === true && !!restoreDirectories.vault && !!restoreDirectories.addressBook
                property string restoreContext:JSON.stringify({directory:ltcBackupDirectory.text,status:root.litecoinLight})
                onRestoreContextChanged: if (ltcRestoreConsent) ltcRestoreConsent.checked=false
                onRestoreReadyChanged: if (!restoreReady && ltcRestoreConsent) ltcRestoreConsent.checked=false
                function restoreVerified() {
                    if (!restoreReady || !ltcRestoreConsent.checked || !ltcBackupPassword.text || root.litecoinLight.busy === true) return
                    var credential=ltcBackupPassword.text;ltcBackupPassword.text="";ltcRestoreConsent.checked=false
                    restoreAttempt=true;error=""
                    var accepted=root.dex.restoreLitecoinWallet(submittedDirectory,root.litecoinLight.wallet.backupDigest,credential);credential=""
                    if (!accepted) {submittedDirectory="";error=qsTr("Could not start restoration. Verify the backup again before retrying.")}
                }
                onVisibleChanged: if (!visible && ltcBackupPassword) ltcBackupPassword.text=""
                function submit(save) {
                    if (root.litecoinLight.walletBackupAvailable !== true || root.litecoinLight.busy === true
                        || !ltcBackupDirectory.text || !ltcBackupPassword.text) return
                    var credential=ltcBackupPassword.text;ltcBackupPassword.text=""
                    submittedDirectory=ltcBackupDirectory.text;error="";restoreAttempt=false
                    var accepted=root.dex.backupLitecoinWallet(submittedDirectory,credential,save);credential=""
                    if (!accepted) {submittedDirectory="";error=qsTr("Could not start the backup operation. Verify the destination before retrying a save.")}
                }
                M.TextPlain {
                    Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    text:qsTr("Encrypted Litecoin wallet backup\nSaves the seed and allocated addresses. Payment records and active swap recovery are not included. Keep those records separately.\nChoose an existing private folder. Saving uses litecoin-wallet.backup and never overwrites a different backup; use a new folder for each new backup.")
                }
                M.LineEdit {
                    id:ltcBackupDirectory;objectName:"litecoinLightBackupDirectory";Layout.fillWidth:true
                    labelText:qsTr("Backup folder (absolute path)");readOnly:root.litecoinLight.busy === true
                    onTextChanged: {if (ltcBackupPassword) ltcBackupPassword.text="";ltcBackupForm.submittedDirectory="";ltcBackupForm.error="";ltcBackupForm.restoreAttempt=false}
                }
                M.LineEdit {
                    id:ltcBackupPassword;objectName:"litecoinLightBackupPassword";Layout.fillWidth:true
                    labelText:qsTr("Wallet / backup password");password:true;readOnly:root.litecoinLight.busy === true
                }
                RowLayout {
                    M.StandardButton {
                        objectName:"litecoinLightBackupSave";text:qsTr("Save encrypted backup")
                        enabled:root.litecoinLight.walletBackupAvailable === true && root.litecoinLight.busy !== true && !!ltcBackupDirectory.text && !!ltcBackupPassword.text
                        onClicked:ltcBackupForm.submit(true)
                    }
                    M.StandardButton {
                        objectName:"litecoinLightBackupVerify";text:qsTr("Verify existing backup")
                        enabled:root.litecoinLight.walletBackupAvailable === true && root.litecoinLight.busy !== true && !!ltcBackupDirectory.text && !!ltcBackupPassword.text
                        onClicked:ltcBackupForm.submit(false)
                    }
                }
                M.TextPlain {
                    id:ltcBackupReceipt;objectName:"litecoinLightBackupReceipt";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet:root.litecoinLight.wallet || ({})
                    visible:(root.litecoinLight.state === "wallet-backup-saved" || root.litecoinLight.state === "wallet-backup-verified")
                        && root.litecoinLight.busy !== true && wallet.backupVerified === true
                        && !!ltcBackupForm.submittedDirectory && ltcBackupForm.submittedDirectory === ltcBackupDirectory.text
                        && root.litecoinLight.backupDirectory === ltcBackupDirectory.text
                    text:visible?(wallet.backupSaved === true?qsTr("Encrypted backup saved and verified."):qsTr("Existing encrypted backup verified."))
                        +"\n"+qsTr("Folder: %1\nWallet ID: %2\nBackup digest: %3\nSaved receiving addresses: %4\nSaved change addresses: %5\nThese counts describe this backup, not necessarily the current wallet. Verification does not prove an independent offline copy. Active swap recovery is not included.")
                        .arg(root.litecoinLight.backupDirectory).arg(wallet.walletId).arg(wallet.backupDigest).arg(wallet.receivingAllocated).arg(wallet.changeAllocated):""
                }
                M.TextPlain {
                    objectName:"litecoinLightRestoreDestinations";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    visible:ltcBackupForm.restoreReady
                    text:qsTr("Restore the verified backup into:
Key vault folder: %1
Address records folder: %2
Conflicting records will not be overwritten. An older backup may omit later addresses. Payment and active swap records must be recovered separately.")
                        .arg(ltcBackupForm.restoreDirectories.vault || "").arg(ltcBackupForm.restoreDirectories.addressBook || "")
                }
                CheckBox {
                    id:ltcRestoreConsent;objectName:"litecoinLightRestoreConsent";Layout.fillWidth:true;visible:ltcBackupForm.restoreReady
                    text:qsTr("I approve restoring the verified seed and address records into the folders shown above.")
                    contentItem:M.TextPlain {
                        text:ltcRestoreConsent.text;color:M.Style.defaultFontColor;wrapMode:Text.Wrap;textFormat:Text.PlainText
                        leftPadding:ltcRestoreConsent.indicator.width+ltcRestoreConsent.spacing
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightRestore";text:qsTr("Restore verified backup");visible:ltcBackupForm.restoreReady
                    enabled:visible && ltcRestoreConsent.checked && !!ltcBackupPassword.text && root.litecoinLight.busy !== true
                    onClicked:ltcBackupForm.restoreVerified()
                }
                M.TextPlain {
                    objectName:"litecoinLightRestoreReceipt";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet:root.litecoinLight.wallet || ({})
                    visible:root.litecoinLight.state === "wallet-restored" && root.litecoinLight.busy !== true && wallet.restored === true
                        && !!ltcBackupForm.submittedDirectory && ltcBackupForm.submittedDirectory === ltcBackupDirectory.text
                        && root.litecoinLight.backupDirectory === ltcBackupDirectory.text
                    text:visible?qsTr("Seed and address records restored and verified.
Backup digest: %1
Saved receiving addresses: %2
Saved change addresses: %3
Later addresses may be missing from this backup. Payment and active swap recovery are not complete.")
                        .arg(wallet.backupDigest).arg(wallet.receivingAllocated).arg(wallet.changeAllocated):""
                }
                M.TextPlain {
                    objectName:"litecoinLightBackupError";Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    visible:!!ltcBackupForm.error || (root.litecoinLight.state === "unknown" && !!ltcBackupForm.submittedDirectory && ltcBackupForm.submittedDirectory === ltcBackupDirectory.text)
                    text:ltcBackupForm.error || (ltcBackupForm.restoreAttempt?qsTr("The restore result could not be verified. Preserve any recovered records. Verify the same backup again before an explicit retry; do not reset the wallet folders."):qsTr("The backup result could not be verified. A save may have completed. Verify the existing destination before trying another save."))
                }
            }
            ColumnLayout {
                id: lightPaymentForm
                objectName: "litecoinLightPaymentForm"
                Layout.fillWidth: true
                visible: root.litecoinLight.walletPrepareConfigured === true
                property var payment: ({address:ltcRecipient.text.trim(),amountAtomic:root.parseLtcAtomic(ltcAmount.text),
                    feeAtomic:root.parseLtcAtomic(ltcFee.text),minimumChangeAtomic:root.parseLtcAtomic(ltcMinimumChange.text),
                    minimumConfirmations:/^[1-9][0-9]{0,6}$/.test(ltcConfirmations.text) ? Number(ltcConfirmations.text) : 0})
                property string context: JSON.stringify({payment:payment,scan:root.ltcPaymentScan})
                property bool signingReady: !!ltcUnsignedReview && ltcUnsignedReview.visible && root.litecoinLight.walletSignAvailable === true && root.litecoinLight.state === "wallet-reopened"
                    && root.ltcSubmittedContext === context && root.ltcLoadedContext === context && !!root.ltcLoadedRequest.review
                    && (root.litecoinLight.wallet || ({})).signingCommitment === root.ltcLoadedRequest.review.signingCommitment
                onSigningReadyChanged: if (!signingReady) {if (ltcSigningPassword) ltcSigningPassword.text="";if (ltcSigningConsent) ltcSigningConsent.checked=false}
                property bool sendReady: !!ltcSendStatus && ltcSendStatus.visible && root.litecoinLight.walletBroadcastAvailable === true
                    && root.litecoinLight.state === "wallet-broadcast-status" && root.ltcLoadedContext === context
                    && !!root.ltcLoadedRequest.review && root.litecoinLight.wallet.signingCommitment === root.ltcLoadedRequest.review.signingCommitment
                property string sendContext: JSON.stringify({form:context,state:root.litecoinLight.state,busy:root.litecoinLight.busy,wallet:root.litecoinLight.wallet})
                onSendContextChanged: if (ltcSendConsent) ltcSendConsent.checked=false
                onSendReadyChanged: if (!sendReady && ltcSendConsent) ltcSendConsent.checked=false
                property bool valid: payment.address.length > 0 && payment.address.length <= 90
                    && payment.amountAtomic !== "" && payment.amountAtomic !== "0" && payment.feeAtomic !== ""
                    && payment.minimumChangeAtomic !== "" && payment.minimumChangeAtomic !== "0" && payment.minimumConfirmations > 0
                property var quote: ({address:payment.address,amountAtomic:payment.amountAtomic,
                    minimumChangeAtomic:payment.minimumChangeAtomic,minimumConfirmations:payment.minimumConfirmations,
                    targetBlocks:/^[1-9][0-9]{0,3}$/.test(ltcFeeTarget.text) ? Number(ltcFeeTarget.text) : 0,
                    maximumRateAtomicPerKvb:root.parseLtcAtomic(ltcRateCap.text),maximumFeeAtomic:root.parseLtcAtomic(ltcFeeCap.text)})
                property string quoteContext: JSON.stringify({quote:quote,scan:root.ltcPaymentScan})
                property bool quoteValid: payment.address.length > 0 && payment.address.length <= 90
                    && payment.amountAtomic !== "" && payment.amountAtomic !== "0"
                    && payment.minimumChangeAtomic !== "" && payment.minimumChangeAtomic !== "0" && payment.minimumConfirmations > 0
                    && quote.targetBlocks > 0 && quote.targetBlocks <= 1008
                    && quote.maximumRateAtomicPerKvb !== "" && quote.maximumRateAtomicPerKvb !== "0" && quote.maximumFeeAtomic !== ""
                property bool quoteCurrent: root.litecoinLight.state === "wallet-estimated" && root.litecoinLight.busy !== true
                    && root.ltcQuoteContext === quoteContext
                property var estimate: quoteCurrent && root.litecoinLight.wallet ? root.litecoinLight.wallet.feeEstimate : null
                M.TextPlain { text: qsTr("Prepare a Litecoin payment"); color: M.Style.defaultFontColor; font.bold: true }
                M.TextPlain {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: M.Style.dimmedFontColor
                    text: qsTr("Preparation reserves inputs for this request. It does not sign or send funds. Enter a fee or request a server estimate, then explicitly apply it. Estimates do not guarantee confirmation.")
                }
                M.StandardButton {
                    objectName:"litecoinLightLoadRequests";text:qsTr("Find saved requests")
                    enabled:root.litecoinLight.walletRequestsAvailable === true
                    onClicked: {root.ltcLoadedRequest=({});root.ltcLoadedContext="";root.ltcSubmittedContext="";root.ltcQuoteContext="";root.ltcPaymentError="";if (!root.dex.loadLitecoinPaymentRequests()) root.ltcPaymentError=qsTr("Could not validate saved requests.")}
                }
                ComboBox {
                    id:ltcSavedRequests;objectName:"litecoinLightSavedRequests";Layout.fillWidth:true
                    property var requests:root.litecoinLight.paymentRequests || []
                    visible:root.litecoinLight.state === "wallet-requests" && requests.length > 0
                    model:requests.map(function(request) {return root.formatLtcAtomic(request.payment.amountAtomic)+" LTC · "+request.payment.address.slice(0,18)+"… · "+request.requestId.slice(0,8)})
                }
                M.StandardButton {
                    objectName:"litecoinLightLoadSelected";text:qsTr("Load selected request")
                    visible:ltcSavedRequests.visible;enabled:root.litecoinLight.busy !== true && ltcSavedRequests.currentIndex >= 0
                    onClicked: {
                        var request=ltcSavedRequests.requests[ltcSavedRequests.currentIndex]
                        root.ltcSubmittedContext="";root.ltcQuoteContext="";root.ltcPaymentError="";root.ltcPaymentScan=request.scan
                        ltcRecipient.text=request.payment.address
                        ltcAmount.text=root.formatLtcAtomic(request.payment.amountAtomic)
                        ltcFee.text=root.formatLtcAtomic(request.payment.feeAtomic)
                        ltcMinimumChange.text=root.formatLtcAtomic(request.payment.minimumChangeAtomic)
                        ltcConfirmations.text=String(request.payment.minimumConfirmations)
                        root.ltcLoadedRequest=JSON.parse(JSON.stringify(request))
                        root.ltcLoadedContext=lightPaymentForm.context
                    }
                }
                M.TextPlain {
                    Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.dimmedFontColor
                    visible:root.litecoinLight.state === "wallet-requests" && ltcSavedRequests.requests.length === 0
                    text:qsTr("No saved requests for this wallet.")
                }
                M.LineEdit { id:ltcRecipient;objectName:"litecoinLightPaymentRecipient";Layout.fillWidth:true;labelText:qsTr("Recipient address");readOnly:root.litecoinLight.busy === true }
                M.LineEdit { id:ltcAmount;objectName:"litecoinLightPaymentAmount";Layout.fillWidth:true;labelText:qsTr("Payment (LTC)");placeholderText:"0.01";readOnly:root.litecoinLight.busy === true }
                M.LineEdit { id:ltcFee;objectName:"litecoinLightPaymentFee";Layout.fillWidth:true;labelText:qsTr("Network fee (LTC)");readOnly:root.litecoinLight.busy === true }
                M.LineEdit { id:ltcMinimumChange;objectName:"litecoinLightPaymentMinimumChange";Layout.fillWidth:true;labelText:qsTr("Minimum change (LTC)");text:"0.00001000";readOnly:root.litecoinLight.busy === true }
                M.LineEdit { id:ltcConfirmations;objectName:"litecoinLightPaymentConfirmations";Layout.fillWidth:true;labelText:qsTr("Minimum input confirmations");text:"6";readOnly:root.litecoinLight.busy === true }
                M.LineEdit { id:ltcFeeTarget;objectName:"litecoinLightFeeTarget";Layout.fillWidth:true;labelText:qsTr("Fee estimate target (blocks)");text:"6";readOnly:root.litecoinLight.busy === true }
                M.LineEdit { id:ltcRateCap;objectName:"litecoinLightRateCap";Layout.fillWidth:true;labelText:qsTr("Maximum estimated rate (LTC per 1,000 virtual bytes)");text:"0.001";readOnly:root.litecoinLight.busy === true }
                M.LineEdit { id:ltcFeeCap;objectName:"litecoinLightFeeCap";Layout.fillWidth:true;labelText:qsTr("Maximum estimated total fee (LTC)");text:"0.001";readOnly:root.litecoinLight.busy === true }
                M.StandardButton {
                    objectName:"litecoinLightEstimateFee";text:qsTr("Estimate fee")
                    enabled:root.litecoinLight.walletEstimateAvailable === true && lightPaymentForm.quoteValid && root.litecoinLight.busy !== true
                    onClicked: {
                        root.ltcQuoteContext=lightPaymentForm.quoteContext;root.ltcSubmittedContext="";root.ltcPaymentError=""
                        if (!root.dex.estimateLitecoinPaymentFee(lightPaymentForm.quote,root.ltcPaymentScan.gap,root.ltcPaymentScan.addressesPerBranch,root.ltcPaymentScan.historyEntriesPerBranch)) {
                            root.ltcQuoteContext="";root.ltcPaymentError=qsTr("Could not request a fee estimate. Check the payment details and wallet connection.")
                        }
                    }
                }
                M.TextPlain {
                    objectName:"litecoinLightFeeEstimate";Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    visible:lightPaymentForm.quoteCurrent
                    text:lightPaymentForm.estimate ? qsTr("Estimated fee: %1 LTC · %2 virtual bytes · target %3 blocks. No inputs reserved.")
                        .arg(root.formatLtcAtomic(lightPaymentForm.estimate.feeAtomic)).arg(lightPaymentForm.estimate.virtualBytes).arg(lightPaymentForm.estimate.targetBlocks)
                        : qsTr("The server has no fee estimate for this target. You can enter a fee manually.")
                }
                M.StandardButton {
                    objectName:"litecoinLightApplyFee";text:qsTr("Use estimate")
                    visible:lightPaymentForm.quoteCurrent && !!lightPaymentForm.estimate
                    enabled:visible && root.litecoinLight.busy !== true
                    onClicked: {ltcFee.text=root.formatLtcAtomic(lightPaymentForm.estimate.feeAtomic);root.ltcSubmittedContext="";root.ltcQuoteContext=""}
                }
                M.StandardButton {
                    objectName: "litecoinLightPreparePayment";text:qsTr("Prepare payment review")
                    enabled: root.litecoinLight.walletPrepareAvailable === true && lightPaymentForm.valid && root.litecoinLight.busy !== true
                    onClicked: {
                        root.ltcQuoteContext = ""
                        root.ltcSubmittedContext = lightPaymentForm.context
                        root.ltcPaymentError = ""
                        if (!root.dex.prepareCurrentLitecoinPayment(lightPaymentForm.payment,root.ltcPaymentScan.gap,root.ltcPaymentScan.addressesPerBranch,root.ltcPaymentScan.historyEntriesPerBranch)) {
                            root.ltcSubmittedContext = ""
                            root.ltcPaymentError = qsTr("Could not retain this request. Check the payment details and private wallet configuration before retrying.")
                        }
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightReopenPayment";text:qsTr("Refresh saved payment review")
                    visible:!!root.ltcLoadedRequest.review && root.ltcLoadedContext === lightPaymentForm.context
                    enabled:visible && root.litecoinLight.walletReopenAvailable === true && root.litecoinLight.busy !== true
                    onClicked: {
                        root.ltcSubmittedContext=lightPaymentForm.context;root.ltcQuoteContext="";root.ltcPaymentError=""
                        var request=root.ltcLoadedRequest
                        var retained={signingCommitment:request.review.signingCommitment,changeAddress:request.review.changeAddress}
                        if (!root.dex.reopenLitecoinPayment(request.requestId,request.payment,retained,request.scan.gap,request.scan.addressesPerBranch,request.scan.historyEntriesPerBranch)) {
                            root.ltcSubmittedContext="";root.ltcPaymentError=qsTr("Could not reopen the saved payment. Its original request and review have been preserved.")
                        }
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightRecoverSignedPayment";text:qsTr("Recover signed payment")
                    visible:!!root.ltcLoadedRequest.review && root.ltcLoadedContext === lightPaymentForm.context
                    enabled:visible && root.litecoinLight.walletSignedRecoveryAvailable === true && root.litecoinLight.busy !== true
                    onClicked: {
                        root.ltcSubmittedContext=lightPaymentForm.context;root.ltcQuoteContext="";root.ltcPaymentError=""
                        var request=root.ltcLoadedRequest
                        if (!root.dex.recoverSignedLitecoinPayment(request.requestId,request.payment,request.review)) {
                            root.ltcSubmittedContext="";root.ltcPaymentError=qsTr("Could not recover the signed payment. Check that its saved records are available.")
                        }
                    }
                }
                M.TextPlain {
                    objectName: "litecoinLightPaymentError";Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    visible: root.ltcPaymentError !== "";text:root.ltcPaymentError
                }
                M.TextPlain {
                    id:ltcUnsignedReview;objectName: "litecoinLightPaymentReview";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet: root.litecoinLight.wallet || ({})
                    visible: (root.litecoinLight.state === "wallet-prepared" || root.litecoinLight.state === "wallet-reopened") && root.litecoinLight.busy !== true
                        && root.ltcSubmittedContext === lightPaymentForm.context && wallet.prepared === true && wallet.signed === false
                        && wallet.watchOnly === true && wallet.spendable === false
                    text: visible ? qsTr("Unsigned payment review (%1)\nRecipient: %2\nPayment: %3 LTC\nNetwork fee: %4 LTC\nChange: %5 LTC\nChange address: %6\nInputs: %7\nTransaction ID: %8\nSign first, then check the saved send status to review sending.")
                        .arg(root.litecoinLight.network).arg(wallet.paymentAddress).arg(root.formatLtcAtomic(wallet.paymentAtomic))
                        .arg(root.formatLtcAtomic(wallet.feeAtomic)).arg(root.formatLtcAtomic(wallet.changeAtomic))
                        .arg(wallet.changeAddress || qsTr("No change")).arg(wallet.inputCount).arg(wallet.txid) : ""
                }
                M.LineEdit {
                    id:ltcSigningPassword;objectName:"litecoinLightSigningPassword";Layout.fillWidth:true
                    labelText:qsTr("Wallet password");password:true;visible:lightPaymentForm.signingReady
                    readOnly:root.litecoinLight.busy === true
                }
                CheckBox {
                    id:ltcSigningConsent;objectName:"litecoinLightSigningConsent";Layout.fillWidth:true
                    visible:lightPaymentForm.signingReady
                    text:qsTr("I approve signing the payment shown above. It will be saved locally and not broadcast.")
                    contentItem: M.TextPlain {
                        text:ltcSigningConsent.text;color:M.Style.defaultFontColor;wrapMode:Text.Wrap;textFormat:Text.PlainText
                        leftPadding:ltcSigningConsent.indicator.width+ltcSigningConsent.spacing
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightSignPayment";text:qsTr("Sign reviewed payment")
                    visible:lightPaymentForm.signingReady
                    enabled:visible && ltcSigningConsent.checked && ltcSigningPassword.text.length>0 && root.litecoinLight.busy !== true
                    onClicked: {
                        if (!lightPaymentForm.signingReady || !ltcSigningConsent.checked || ltcSigningPassword.text.length===0) return
                        var commitment=root.litecoinLight.wallet.signingCommitment
                        var secret=ltcSigningPassword.text;ltcSigningPassword.text="";ltcSigningConsent.checked=false;root.ltcPaymentError=""
                        var accepted=root.dex.signReviewedLitecoinPayment(commitment,secret);secret=""
                        if (!accepted) {root.ltcSubmittedContext="";root.ltcPaymentError=qsTr("Could not start signing. Refresh the saved review before trying again.")}
                    }
                }
                M.TextPlain {
                    objectName:"litecoinLightSignedPayment";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet:root.litecoinLight.wallet || ({})
                    visible:root.litecoinLight.state === "wallet-signed" && root.litecoinLight.busy !== true
                        && root.ltcSubmittedContext === lightPaymentForm.context && wallet.signed === true && wallet.broadcast === false && wallet.watchOnly === false
                    text:visible?qsTr("Signed payment saved locally. Not broadcast.\nTransaction ID: %1\nNetwork fee: %2 LTC").arg(wallet.txid).arg(root.formatLtcAtomic(wallet.feeAtomic)):""
                }
                M.TextPlain {
                    objectName:"litecoinLightRecoveredSignedPayment";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet:root.litecoinLight.wallet || ({})
                    visible:root.litecoinLight.state === "wallet-signed-recovered" && root.litecoinLight.busy !== true
                        && root.ltcSubmittedContext === lightPaymentForm.context && wallet.signed === true && wallet.recovered === true
                        && wallet.watchOnly === true && wallet.broadcastStatus === "unassessed"
                    text:visible?qsTr("Saved signed payment verified locally. Broadcast and confirmation status have not been checked.\nTransaction ID: %1\nPayment: %2 LTC\nNetwork fee: %3 LTC")
                        .arg(wallet.txid).arg(root.formatLtcAtomic(wallet.paymentAtomic)).arg(root.formatLtcAtomic(wallet.feeAtomic)):""
                }
                M.StandardButton {
                    objectName:"litecoinLightCheckSendStatus";text:qsTr("Check saved send status")
                    visible:!!root.ltcLoadedRequest.review && root.ltcLoadedContext === lightPaymentForm.context
                    enabled:visible && root.litecoinLight.walletBroadcastStatusAvailable === true && root.litecoinLight.busy !== true
                    onClicked: {
                        root.ltcSubmittedContext=lightPaymentForm.context;root.ltcQuoteContext="";root.ltcPaymentError="";root.ltcSendAttemptContext=""
                        var request=root.ltcLoadedRequest
                        if (!root.dex.checkLitecoinPaymentSendStatus(request.requestId,request.payment,request.review,request.scan.gap,request.scan.addressesPerBranch,request.scan.historyEntriesPerBranch)) {
                            root.ltcSubmittedContext="";root.ltcPaymentError=qsTr("Could not check saved send status. The payment records have been preserved.")
                        }
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightReconcilePayment";text:qsTr("Check payment on chain")
                    visible:!!root.ltcLoadedRequest.review && root.ltcLoadedContext === lightPaymentForm.context
                    enabled:visible && root.litecoinLight.walletReconcileAvailable === true && root.litecoinLight.busy !== true
                    onClicked: {
                        if (!enabled) return
                        root.ltcSubmittedContext=lightPaymentForm.context;root.ltcQuoteContext="";root.ltcPaymentError="";root.ltcSendAttemptContext=""
                        root.ltcChainCheckContext=lightPaymentForm.context
                        var request=root.ltcLoadedRequest
                        if (!root.dex.reconcileLitecoinPayment(request.requestId,request.payment,request.review,10000000,request.scan.historyEntriesPerBranch)) {
                            root.ltcSubmittedContext="";root.ltcPaymentError=qsTr("Could not check this payment on chain. The payment records have been preserved.")
                        }
                    }
                }
                M.TextPlain {
                    objectName:"litecoinLightPaymentObservation";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet:root.litecoinLight.wallet || ({})
                    property var observation:wallet.paymentObservation || ({})
                    property string checkedTime:typeof observation.observedAtUnix === "number" && isFinite(observation.observedAtUnix)
                        && observation.observedAtUnix > 0 && observation.observedAtUnix <= 8640000000000
                        ?new Date(observation.observedAtUnix*1000).toISOString():""
                    visible:root.litecoinLight.state === "wallet-reconciled" && root.litecoinLight.busy !== true
                        && root.ltcSubmittedContext === lightPaymentForm.context && wallet.signed === true && wallet.recovered === true
                        && wallet.watchOnly === true && !!wallet.broadcastEndpoint && !!wallet.paymentObservation && !!checkedTime
                        && (wallet.confirmationStatus === "included" || wallet.confirmationStatus === "reported-pending" || wallet.confirmationStatus === "not-reported")
                    property string summary:wallet.confirmationStatus === "included"
                        ?qsTr("Payment included in the selected header chain.\nConfirmations at this check: %1\nBlock height: %2\nBlock hash: %3").arg(observation.confirmations).arg(observation.height).arg(observation.blockHash)
                        :wallet.confirmationStatus === "reported-pending"?qsTr("The server reports this payment as pending. Block inclusion has not been verified.")
                        :qsTr("The server did not report this payment. This does not mean it failed or was never sent.")
                    property string acknowledgement:wallet.broadcastStatus === "server-acknowledged"?qsTr("Server acknowledgement saved.")
                        :wallet.broadcastStatus === "outcome-unknown"?qsTr("No acknowledgement saved. The transaction may have been sent.")
                        :qsTr("No attempt recorded for this server.")
                    text:visible?summary+"\n"+qsTr("Checked (UTC): %1\nSelected height: %2\nSelected tip: %3\nTransaction ID: %4\nServer: %5:%6")
                        .arg(checkedTime).arg(observation.selectedHeight).arg(observation.selectedTip)
                        .arg(wallet.txid).arg(wallet.broadcastEndpoint.host).arg(wallet.broadcastEndpoint.port)
                        +"\n"+acknowledgement+"\n"+qsTr("A chain reorganization can change this result. The server view may be incomplete or out of date. Check again to update it."):""
                }
                M.TextPlain {
                    objectName:"litecoinLightReconcileUnknown";Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    visible:root.litecoinLight.state === "unknown" && root.ltcChainCheckContext === lightPaymentForm.context
                    text:qsTr("The chain check could not be verified. No current inclusion result is available. The saved payment records have been preserved.")
                }
                M.TextPlain {
                    id:ltcSendStatus;objectName:"litecoinLightSendStatus";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var wallet:root.litecoinLight.wallet || ({})
                    visible:(root.litecoinLight.state === "wallet-broadcast-status" || root.litecoinLight.state === "wallet-broadcast-acknowledged")
                        && root.litecoinLight.busy !== true && root.ltcSubmittedContext === lightPaymentForm.context
                        && wallet.signed === true && wallet.confirmationStatus === "unassessed" && !!wallet.broadcastEndpoint
                        && (wallet.broadcastStatus === "no-local-attempt" || wallet.broadcastStatus === "outcome-unknown" || wallet.broadcastStatus === "server-acknowledged")
                    property string summary:wallet.broadcastStatus === "server-acknowledged"?qsTr("The server acknowledged this transaction. This is not proof of confirmation.")
                        :wallet.broadcastStatus === "outcome-unknown"?qsTr("Send outcome unknown. This transaction may already have been sent. Keep its original records.")
                        :qsTr("No send attempt is recorded for this server. This does not rule out a send through another server.")
                    text:visible?summary+"\n"+qsTr("Confirmation has not been checked.\nNetwork: %1\nRecipient: %2\nPayment: %3 LTC\nNetwork fee: %4 LTC\nChange: %5 LTC\nChange address: %6\nInputs: %7\nTransaction ID: %8\nServer: %9:%10")
                        .arg(root.litecoinLight.network).arg(wallet.paymentAddress).arg(root.formatLtcAtomic(wallet.paymentAtomic))
                        .arg(root.formatLtcAtomic(wallet.feeAtomic)).arg(root.formatLtcAtomic(wallet.changeAtomic)).arg(wallet.changeAddress || qsTr("No change"))
                        .arg(wallet.inputCount).arg(wallet.txid).arg(wallet.broadcastEndpoint.host).arg(wallet.broadcastEndpoint.port):""
                }
                CheckBox {
                    id:ltcSendConsent;objectName:"litecoinLightSendConsent";Layout.fillWidth:true;visible:lightPaymentForm.sendReady
                    text:qsTr("I approve sending these exact signed payment bytes to the server shown above. They may already have been sent.")
                    contentItem:M.TextPlain {
                        text:ltcSendConsent.text;color:M.Style.defaultFontColor;wrapMode:Text.Wrap;textFormat:Text.PlainText
                        leftPadding:ltcSendConsent.indicator.width+ltcSendConsent.spacing
                    }
                }
                M.StandardButton {
                    objectName:"litecoinLightSendPayment";text:qsTr("Send signed payment");visible:lightPaymentForm.sendReady
                    enabled:visible && ltcSendConsent.checked && root.litecoinLight.busy !== true
                    onClicked: {
                        if (!lightPaymentForm.sendReady || !ltcSendConsent.checked) return
                        var wallet=root.litecoinLight.wallet;ltcSendConsent.checked=false;root.ltcPaymentError=""
                        root.ltcSendAttemptContext=lightPaymentForm.context
                        if (!root.dex.sendReviewedLitecoinPayment(wallet.signingCommitment,wallet.txid,wallet.broadcastEndpoint)) {
                            root.ltcSubmittedContext="";root.ltcPaymentError=qsTr("Could not start sending. Check saved send status before any further action.")
                        }
                    }
                }
                M.TextPlain {
                    objectName:"litecoinLightSendUnknown";Layout.fillWidth:true;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    visible:root.litecoinLight.state === "unknown" && root.ltcSendAttemptContext === lightPaymentForm.context
                    text:qsTr("The send result could not be verified. The transaction may have been sent. Check saved send status before any further action.")
                }
            }
            RowLayout {
                visible: root.litecoinLight.walletConfigured === true
                M.StandardButton {
                    objectName: "litecoinLightWalletInit"; text: qsTr("Initialize watch-only account")
                    enabled: root.litecoinLight.walletInitAvailable === true
                    onClicked: root.dex.initializeLitecoinLightWallet()
                }
                M.StandardButton {
                    objectName: "litecoinLightWalletRefresh"; text: qsTr("Refresh provisional amounts")
                    enabled: root.litecoinLight.walletRefreshAvailable === true
                    onClicked: root.dex.refreshLitecoinLightWallet(20, 500, 1000)
                }
            }
            RowLayout {
                M.StandardButton {
                    objectName: "litecoinLightLoad"; text: qsTr("Load saved headers")
                    enabled: root.litecoinLight.refreshAvailable === true
                    onClicked: root.dex.requestLitecoinLightStatus()
                }
                M.StandardButton {
                    objectName: "litecoinLightSync"; text: qsTr("Synchronize headers")
                    enabled: root.litecoinLight.syncAvailable === true
                    onClicked: root.dex.synchronizeLitecoinLight(10000000)
                }
                M.StandardButton {
                    objectName: "litecoinLightCancel"; text: qsTr("Cancel")
                    visible: root.litecoinLight.busy === true
                    onClicked: root.dex.cancelLitecoinLight()
                }
            }
        }

        M.TextPlain {
            objectName: "nativeQuoteOutcome"
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            color: M.Style.defaultFontColor
            property string quoteState: root.dex.nativeQuoteStatus ? root.dex.nativeQuoteStatus.state : "unattached"
            visible: root.baseCoin === "BTC" && text.length > 0
            text: quoteState === "reserved"
                ? qsTr("Commercial terms saved. No trade has started and no funds were sent.")
                : quoteState === "failed"
                    ? qsTr("Quote validation or reservation failed. No trade was started. A partial reservation may remain; do not delete records to retry.")
                    : quoteState === "cancelled"
                        ? qsTr("Quote review cancelled. No trade was started.")
                        : quoteState === "checking" ? qsTr("Checking signed quote and destinations. No funds will be sent.") : ""
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.baseCoin === "BTC" && typeof root.quoteReview.commitment === "string"
            enabled: visible
            objectName: "nativeQuoteReviewPanel"
            M.TextPlain {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                text: qsTr("Review verified commercial terms. Approval records these terms only; it does not fund or start a swap.")
                color: M.Style.defaultFontColor
            }
            M.TextPlain {
                objectName: "nativeQuoteAmountSummary"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: root.quoteReview.amounts
                    ? qsTr("%1 BTC for %2 XMZ\nQuoted rate: %3 BTC per XMZ (offer price, not market valuation).\nSeparate network fee limits: %4 BTC and %5 XMZ. Application trading fee: zero.")
                        .arg(root.quoteReview.amounts.btcAmount).arg(root.quoteReview.amounts.xmzAmount)
                        .arg(root.quoteReview.amounts.btcPerXmz).arg(root.quoteReview.amounts.btcFeeLimit)
                        .arg(root.quoteReview.amounts.xmzFeeLimit)
                    : ""
            }
            M.TextPlain {
                objectName: "nativeQuoteReviewTerms"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                text: JSON.stringify(root.quoteReview.terms || {}, null, 2)
                color: M.Style.defaultFontColor
            }
            CheckBox {
                id: quoteAcknowledgement
                objectName: "nativeQuoteAcknowledgement"
                text: qsTr("I reviewed the amounts, destinations, networks and fee limits above.")
            }
            M.StandardButton {
                objectName: "nativeQuoteApprove"
                text: qsTr("Approve terms only — no funds sent")
                enabled: quoteAcknowledgement.checked && typeof root.quoteReview.commitment === "string"
                onClicked: root.dex.approveNativeQuote(root.quoteReview.commitment)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            M.TextPlain { text: qsTr("Isolated receiver snapshot (read-only)"); color: M.Style.defaultFontColor; font.bold: true }
            M.LineEdit {
                id: snapshotPath
                Layout.fillWidth: true
                labelText: qsTr("Local receiver offers.json path")
                placeholderText: "/path/to/dex-receiver-state/offers.json"
                enabled: !(dexPrototypeEnabled && root.dex.researchMarketBusy)
                onTextChanged: { root.marketSnapshot = ({}); root.rebuildOrderBook() }
            }
            M.StandardButton {
                text: dexPrototypeEnabled && root.dex.researchMarketBusy ? qsTr("Verifying snapshot…") : qsTr("Verify and refresh snapshot")
                enabled: !(dexPrototypeEnabled && root.dex.researchMarketBusy) && snapshotPath.text.trim().length > 0
                onClicked: {
                    root.marketSnapshot = ({})
                    root.dex.refreshResearchMarket(snapshotPath.text)
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
                    : (root.marketSnapshot.error || qsTr("Optional: enter the full path to an existing offers.json file from your isolated receiver. This is not a network address. Once set, the order book below refreshes automatically every few seconds."))
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

        ColumnLayout {
            objectName: "bchPreparationPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchPreparation.configured === true
            Text {
                Layout.fillWidth: true
                color: M.Style.defaultFontColor
                text: qsTr("BCH funding preparation")
                font.bold: true
            }
            M.StandardButton {
                objectName: "bchPrepare"
                text: qsTr("Prepare BCH funding")
                enabled: root.bchPreparation.prepareAvailable === true
                onClicked: root.dex.prepareBchFunding()
            }
            M.StandardButton {
                objectName: "bchPreparationCheck"
                text: qsTr("Read saved preparation")
                enabled: root.bchPreparation.checkAvailable === true
                onClicked: root.dex.requestBchPreparationCheck()
            }
            Text {
                objectName: "bchPreparationDetails"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: M.Style.defaultFontColor
                property var result: root.bchPreparation.result || ({})
                property var reviewed: result.review ? result.review.details || ({}) : ({})
                property var details: reviewed.preparationReview || reviewed
                visible: !!details.preparation && details.action !== "sign-only"
                text: {
                    if (!visible) return ""
                    var lines = [qsTr("Trade: ") + details.swapId,
                        qsTr("Network: ") + details.network,
                        qsTr("Funding fee: ") + root.exactAtomicText(details.feeAtoms, 8) + " BCH",
                        qsTr("Prepared at (Unix seconds): ") + details.preparedAt,
                        qsTr("Preparation: ") + details.preparation]
                    lines.push(qsTr("Wallet deployment: ") + details.deployment)
                    lines.push(qsTr("Quote expiry (Unix seconds): ") + details.expiresAt)
                    if (details.policy) {
                        lines.push(qsTr("Change address: ") + details.policy.change_address)
                        lines.push(qsTr("Minimum input confirmations: ") + details.policy.minimum_confirmations)
                    }
                    var inputs = details.inputs || [], outputs = details.outputs || []
                    for (var i=0; i<inputs.length; ++i)
                        lines.push(qsTr("Input: ") + inputs[i].outpoint + " — " + root.exactAtomicText(inputs[i].valueAtoms,8)
                            + " BCH\n" + qsTr("Input script: ") + inputs[i].script)
                    for (var j=0; j<outputs.length; ++j)
                        lines.push(qsTr("Output: ") + outputs[j].vout + " — " + root.exactAtomicText(outputs[j].valueAtoms,8)
                            + " BCH\n" + qsTr("Destination script: ") + outputs[j].script)
                    return lines.join("\n")
                }
            }
            M.StandardButton {
                objectName: "bchLockReview"
                text: qsTr("Review wallet locks")
                enabled: root.bchPreparation.lockReviewAvailable === true
                onClicked: root.dex.requestBchLockReview()
            }
            M.StandardButton {
                objectName: "bchLockCheck"
                text: qsTr("Read saved lock intent")
                enabled: root.bchPreparation.lockCheckAvailable === true
                onClicked: root.dex.requestBchLockCheck()
            }
            M.CheckBox {
                id: bchLockConsent
                objectName: "bchLockConsent"
                visible: root.bchPreparation.lockSubmitAvailable === true
                text: qsTr("I approve locking or reconciling only these saved BCH inputs in the configured wallet.")
            }
            M.StandardButton {
                objectName: "bchLockSubmit"
                text: qsTr("Lock or reconcile approved inputs")
                enabled: root.bchPreparation.lockSubmitAvailable === true && bchLockConsent.checked
                onClicked: {
                    var commitment = root.bchPreparation.lockReview.commitment
                    bchLockConsent.checked = false
                    root.dex.submitBchLock(commitment)
                }
            }
            M.TextPlain {
                objectName: "bchLockHistory"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: M.Style.defaultFontColor
                property var intent: root.bchPreparation.lockIntent || ({})
                visible: root.bchPreparation.lockStatusKnown === true
                text: intent.state === "recorded" ? qsTr("Saved wallet-lock intent: ") + intent.commitment
                    + "\n" + qsTr("Recorded at (Unix seconds): ") + intent.recordedAt
                    + "\n" + qsTr("This is historical intent. Wallet locks may have disappeared after a node restart.")
                    : qsTr("No saved lock intent was found. This does not prove that the wallet has no locks.")
            }
            M.TextPlain {
                objectName: "bchLockObservation"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: M.Style.defaultFontColor
                property var observation: root.bchPreparation.lockObservation || ({})
                visible: root.bchPreparation.chainObserved === true && observation.walletInputsLocked === true
                text: qsTr("The original inputs were checked and wallet-locked at this observation.")
                    + "\n" + qsTr("Chain tip: ") + observation.tip
                    + "\n" + qsTr("Checked at (Unix seconds): ") + observation.observedAt
                    + "\n" + qsTr("Wallet locks are advisory. This does not approve signing or broadcasting.")
            }
            M.StandardButton {
                objectName: "bchSigningReview"
                text: qsTr("Review BCH signing")
                enabled: root.bchPreparation.signingReviewAvailable === true
                onClicked: root.dex.requestBchSigningReview()
            }
            M.StandardButton {
                objectName: "bchSigningCheck"
                text: qsTr("Read saved signing status")
                enabled: root.bchPreparation.signingCheckAvailable === true
                onClicked: root.dex.requestBchSigningCheck()
            }
            Text {
                objectName: "bchSigningDetails"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: M.Style.defaultFontColor
                property var result: root.bchPreparation.result || ({})
                property var review: result.review || ({})
                property var details: review.details ? review.details.signingReview || review.details : ({})
                visible: details.action === "sign-only"
                function digest(bytes) {
                    return (bytes || []).map(function (octet) {return ("0"+octet.toString(16)).slice(-2)}).join("")
                }
                text: {
                    if (!visible) return ""
                    var lines=[qsTr("Sign only — broadcasting requires a separate approval."),
                        qsTr("Trade: ")+details.swapId,qsTr("Network: ")+details.network,
                        qsTr("Review commitment: ")+review.commitment,
                        qsTr("Wallet deployment: ")+digest(details.deployment),
                        qsTr("Preparation: ")+digest(details.preparation),
                        qsTr("Wallet lock intent: ")+digest(details.walletLockIntent),
                        qsTr("Quote expiry (Unix seconds): ")+details.expiresAt,
                        qsTr("Funding fee: ")+root.exactAtomicText(String(details.feeAtoms),8)+" BCH",
                        qsTr("Change address: ")+details.changeAddress,
                        qsTr("Minimum input confirmations: ")+details.minimumConfirmations]
                    var inputs=details.inputs || [],outputs=details.outputs || []
                    for (var i=0;i<inputs.length;++i)
                        lines.push(qsTr("Input: ")+inputs[i].txid+":"+inputs[i].vout+" — "+root.exactAtomicText(String(inputs[i].atoms),8)
                            +" BCH\n"+qsTr("Input script: ")+inputs[i].script)
                    for (var j=0;j<outputs.length;++j)
                        lines.push(qsTr("Output: ")+outputs[j].vout+" — "+root.exactAtomicText(String(outputs[j].atoms),8)
                            +" BCH\n"+qsTr("Destination script: ")+outputs[j].script)
                    return lines.join("\n")
                }
            }
            M.CheckBox {
                id: bchSigningConsent
                objectName: "bchSigningConsent"
                visible: root.bchPreparation.signingSubmitAvailable === true
                text: qsTr("I approve signing only the reviewed BCH funding transaction.")
            }
            M.StandardButton {
                objectName: "bchSigningSubmit"
                text: qsTr("Sign reviewed BCH funding")
                enabled: root.bchPreparation.signingSubmitAvailable === true && bchSigningConsent.checked
                onClicked: {
                    var commitment=root.bchPreparation.signingReview.commitment
                    bchSigningConsent.checked=false
                    root.dex.submitBchSigning(commitment)
                }
            }
            Text {
                objectName: "bchSigningHistory"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: M.Style.defaultFontColor
                property var intent: root.bchPreparation.signingIntent || ({})
                property var signedFunding: root.bchPreparation.signedFunding || ({})
                visible: root.bchPreparation.signingStatusKnown === true
                text: signedFunding.txid ? qsTr("Signed BCH funding saved: ")+signedFunding.txid+":"+signedFunding.contractVout
                        +"\n"+qsTr("This is a retained transaction, not proof of broadcast or confirmation.")
                    : intent.commitment ? qsTr("A signing attempt is saved without a result. Recover the original signed transaction; do not sign again.")
                        +"\n"+qsTr("Signing intent: ")+intent.commitment+"\n"+qsTr("Approved at (Unix seconds): ")+intent.approvedAt
                    : qsTr("No signing attempt is recorded. This does not prove the wallet or chain has no signed transaction.")
            }
            M.StandardButton {
                objectName: "bchSigningRecoveryReview"
                visible: root.bchPreparation.recoveryConfigured === true
                text: qsTr("Review recovered BCH transaction")
                enabled: root.bchPreparation.recoveryReviewAvailable === true
                onClicked: root.dex.requestBchSigningRecoveryReview()
            }
            Text {
                objectName: "bchSigningRecoveryDetails"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: M.Style.defaultFontColor
                property var review: root.bchPreparation.recoveryReview || ({})
                property var details: review.details || ({})
                visible: !!review.commitment
                text: qsTr("Import only the reviewed recovered transaction. This does not sign again or broadcast.")
                    +"\n"+qsTr("Recovered transaction: ")+details.fundingTxid+":"+details.contractVout
                    +"\n"+qsTr("Original signing approval (Unix seconds): ")+details.originalApprovedAt
                    +"\n"+qsTr("Recovery approval: ")+review.commitment
            }
            M.CheckBox {
                id: bchSigningRecoveryConsent
                objectName: "bchSigningRecoveryConsent"
                visible: root.bchPreparation.recoverySubmitAvailable === true
                text: qsTr("I approve saving this exact recovered BCH transaction.")
            }
            M.StandardButton {
                objectName: "bchSigningRecoverySubmit"
                visible: root.bchPreparation.recoveryConfigured === true
                text: qsTr("Import reviewed BCH transaction")
                enabled: root.bchPreparation.recoverySubmitAvailable === true && bchSigningRecoveryConsent.checked
                onClicked: {
                    var commitment=root.bchPreparation.recoveryReview.commitment
                    bchSigningRecoveryConsent.checked=false
                    root.dex.importBchSigningRecovery(commitment)
                }
            }
            Text {
                objectName: "bchPreparationOutcome"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                text: root.bchPreparation.state === "signing-recovered" ? qsTr("The reviewed recovered transaction is saved. No new signature or broadcast was requested.")
                    : root.bchPreparation.state === "signing" ? qsTr("Signing the reviewed BCH funding transaction…")
                    : root.bchPreparation.state === "signed" ? qsTr("Signed funding is saved. Broadcasting requires a separate review and approval.")
                    : root.bchPreparation.state === "locking" ? qsTr("Checking and reconciling the original BCH wallet inputs…")
                    : root.bchPreparation.busy === true ? qsTr("Checking BCH funding preparation…")
                    : root.bchPreparation.state === "locks-checked" ? qsTr("Wallet locks were checked at the displayed time. Signing and broadcasting require separate steps.")
                    : root.bchPreparation.state === "unknown" ? qsTr("Status is unavailable. Read saved preparation, lock intent and signing status before taking further action.")
                    : qsTr("Preparation selects and saves unsigned funding inputs. It does not lock coins, sign or broadcast. Saved details do not prove current input availability; live trading remains unavailable.")
            }
        }

        ColumnLayout {
            objectName: "bchFundingPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchFunding.configured === true
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                font.bold: true
                text: qsTr("BCH funding transaction — review retained data")
            }
            M.StandardButton {
                objectName: "bchFundingReview"
                text: qsTr("Review saved BCH funding")
                enabled: root.bchFunding.reviewAvailable === true
                onClicked: root.dex.requestBchFundingReview()
            }
            M.TextPlain {
                objectName: "bchFundingDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                property var review: root.bchFunding.review && root.bchFunding.review.commitment ? root.bchFunding.review : (root.bchFunding.savedReview || ({}))
                property var details: review.details || ({})
                visible: !!review.commitment
                text: visible ? qsTr("Network: ") + details.network
                    + "\n" + qsTr("Funding transaction: ") + details.fundingTxid
                    + "\n" + qsTr("Funding fee: ") + root.exactAtomicText(String(details.feeAtoms),8) + " BCH"
                    + "\n" + qsTr("Contract output: ") + details.contractVout
                    + "\n" + qsTr("Recovery transactions — cancellation: ") + details.cancelTxid
                    + "\n" + qsTr("Refund: ") + details.refundTxid
                    + "\n" + qsTr("Punishment: ") + details.punishTxid
                    + "\n" + qsTr("Quote expiry (Unix seconds): ") + details.expiresAt
                    + "\n" + qsTr("Trade: ") + details.swapId : ""
            }
            Repeater {
                model: root.bchFunding.review && root.bchFunding.review.details ? root.bchFunding.review.details.outputs
                    : root.bchFunding.savedReview && root.bchFunding.savedReview.details ? root.bchFunding.savedReview.details.outputs : []
                delegate: M.TextPlain {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                    color: M.Style.defaultFontColor
                    text: qsTr("Output ") + index + ": " + root.exactAtomicText(String(modelData.atoms),8)
                        + " BCH\n" + qsTr("Destination script: ") + modelData.script
                }
            }
            M.StandardButton {
                objectName: "bchFundingCheck"
                text: qsTr("Check saved funding status")
                enabled: root.bchFunding.checkAvailable === true
                onClicked: root.dex.requestBchFundingCheck()
            }
            M.TextPlain {
                objectName: "bchFundingRelayHistory"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                visible: root.bchFunding.statusKnown === true
                text: root.bchFunding.relayState === "acknowledged"
                    ? qsTr("Saved relay history: the node acknowledged the funding transaction. This is not chain confirmation.")
                    : root.bchFunding.relayState === "attempted"
                        ? qsTr("Saved relay history: an attempt has an uncertain result. Sending again remains blocked.")
                        : qsTr("Saved relay history: no local send attempt is recorded. This does not establish whether funding is on the chain.")
            }
            M.StandardButton {
                objectName: "bchFundingObserve"
                text: qsTr("Check funding on the node")
                visible: root.bchFunding.submissionConfigured === true
                enabled: root.bchFunding.observeAvailable === true
                onClicked: root.dex.requestBchFundingObservation()
            }
            M.TextPlain {
                objectName: "bchFundingObservation"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                property var observation: root.bchFunding.observation || ({})
                visible: root.bchFunding.chainObserved === true
                text: !visible ? "" : (observation.state === "confirmed-unspent"
                    ? qsTr("The funding output was unspent with %1 confirmations.").arg(observation.confirmations)
                    : observation.state === "mempool"
                        ? (observation.lockOutputUnspent === true ? qsTr("Funding was in the mempool with an unspent contract output.")
                            : qsTr("Funding was in the mempool; its contract output was already spent."))
                        : qsTr("Funding was not observed as an unspent output or mempool transaction. This does not prove it was never sent."))
                    + "\n" + qsTr("Transaction: ") + observation.txid
                    + "\n" + qsTr("Node tip: ") + observation.tip
                    + "\n" + qsTr("Checked at (Unix seconds): ") + observation.observedAt
                    + "\n" + qsTr("Point-in-time evidence only. This check does not authorize another send or settlement.")
            }
            M.CheckBox {
                id: bchFundingConsent
                objectName: "bchFundingConsent"
                visible: root.bchFunding.submitAvailable === true
                text: qsTr("I approve sending this exact BCH funding transaction, with these outputs and fee.")
            }
            M.StandardButton {
                objectName: "bchFundingSubmit"
                text: qsTr("Send approved BCH funding")
                visible: root.bchFunding.submissionConfigured === true
                enabled: root.bchFunding.submitAvailable === true && bchFundingConsent.checked
                onClicked: {
                    var commitment = root.bchFunding.review.commitment
                    bchFundingConsent.checked = false
                    root.dex.submitBchFunding(commitment)
                }
            }
            M.TextPlain {
                objectName: "bchFundingOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: root.bchFunding.state === "observing" ? qsTr("Checking the exact funding transaction on the pinned node…")
                    : root.bchFunding.state === "observed" ? qsTr("Node check complete. Saved relay history and chain evidence are separate; live trading remains unavailable.")
                    : root.bchFunding.state === "submitting" ? qsTr("Sending the approved funding transaction…")
                    : root.bchFunding.busy === true ? qsTr("Checking retained funding and recovery data…")
                    : root.bchFunding.state === "acknowledged" ? qsTr("The node acknowledged the exact funding transaction. This is saved status; chain confirmation has not been checked. Do not send again.")
                    : root.bchFunding.state === "attempted" ? qsTr("A funding attempt is recorded with an uncertain result. Sending again is blocked. Chain reconciliation is still required.")
                    : root.bchFunding.state === "unknown" ? qsTr("Funding status is unavailable. Check saved funding status before taking further action.")
                    : root.bchFunding.submissionConfigured === true ? qsTr("Check saved status, review the exact transaction, then approve sending. Status checks do not contact the chain. Live trading remains unavailable.")
                    : qsTr("This review sends no funds and does not check the chain. A saved transaction is not proof of broadcast or confirmation. Live trading remains unavailable.")
            }
        }

        ColumnLayout {
            objectName: "bchRefundProtectionPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchRefundProtection.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("BCH peer refund protection")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Accepting saves the reviewed peer message. It does not send funds or prove that recovery is ready.")
            }
            M.StandardButton {
                objectName: "bchRefundProtectionCheck"
                text: qsTr("Check saved refund protection")
                enabled: root.bchRefundProtection.checkAvailable === true
                onClicked: root.bchRefundProtectionError = root.dex.requestBchRefundProtectionCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchRefundProtectionReview"
                text: qsTr("Review peer refund protection")
                enabled: root.bchRefundProtection.reviewAvailable === true
                onClicked: root.bchRefundProtectionError = root.dex.requestBchRefundProtectionReview() ? "" : qsTr("Peer-message review could not start.")
            }
            M.TextPlain {
                objectName: "bchRefundProtectionDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchRefundProtection.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment && root.bchRefundProtection.state === "reviewed"
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Peer identity: ") + details.peerIdentity
                    + "\n" + qsTr("Signed quote: ") + details.quote
                    + "\n" + qsTr("Reviewed message: ") + details.frame : ""
            }
            M.CheckBox {
                id: bchRefundProtectionConsent
                objectName: "bchRefundProtectionConsent"
                visible: root.bchRefundProtection.submitAvailable === true
                text: qsTr("I approve saving this exact refund-protection message from the reviewed peer.")
            }
            M.StandardButton {
                objectName: "bchRefundProtectionSubmit"
                text: qsTr("Accept reviewed refund protection")
                enabled: root.bchRefundProtection.submitAvailable === true && bchRefundProtectionConsent.checked
                onClicked: {
                    var commitment = root.bchRefundProtection.review.commitment
                    bchRefundProtectionConsent.checked = false
                    root.bchRefundProtectionError = root.dex.submitBchRefundProtection(commitment) ? "" : qsTr("Acceptance could not start. Check saved status and review again.")
                }
            }
            M.TextPlain {
                objectName: "bchRefundProtectionOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var receipt: root.bchRefundProtection.receipt || ({})
                text: root.bchRefundProtectionError ? root.bchRefundProtectionError
                    : root.bchRefundProtection.busy === true ? qsTr("Checking refund protection…")
                    : root.bchRefundProtection.state === "saved" && receipt.commitment
                        ? qsTr("Peer refund protection is saved. No funds were sent or chain confirmation checked.")
                            + "\n" + qsTr("Receipt: ") + receipt.commitment
                    : root.bchRefundProtection.state === "unknown"
                        ? qsTr("The result is unknown. Acceptance may have been saved. Check saved status before proceeding.")
                    : root.bchRefundProtection.state === "not-saved"
                        ? qsTr("No saved refund protection was found. Review the configured peer message before accepting it.")
                    : root.bchRefundProtection.state === "reviewed"
                        ? qsTr("Confirm the peer and message above before accepting. Live trading remains unavailable.")
                    : qsTr("Check saved status before reviewing the peer message.")
            }
        }

        ColumnLayout {
            objectName: "bchRecoveryRetentionPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchRecoveryRetention.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("BCH private recovery bundle")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Save the reviewed recovery bundle before funding. This does not send funds, release a refund or verify chain maturity.")
            }
            M.StandardButton {
                objectName: "bchRecoveryRetentionCheck"
                text: qsTr("Check saved recovery bundle")
                enabled: root.bchRecoveryRetention.checkAvailable === true
                onClicked: root.bchRecoveryRetentionError = root.dex.requestBchRecoveryRetentionCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchRecoveryRetentionReview"
                text: qsTr("Review recovery bundle")
                enabled: root.bchRecoveryRetention.reviewAvailable === true
                onClicked: root.bchRecoveryRetentionError = root.dex.requestBchRecoveryRetentionReview() ? "" : qsTr("Recovery review could not start.")
            }
            M.TextPlain {
                objectName: "bchRecoveryRetentionDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchRecoveryRetention.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment && root.bchRecoveryRetention.state === "reviewed"
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Funding transaction: ") + details.fundingTxid
                    + "\n" + qsTr("Signed quote: ") + details.quote
                    + "\n" + qsTr("Cancellation: ") + details.cancelTxid
                    + "\n" + qsTr("Refund: ") + details.refundTxid
                    + "\n" + qsTr("Punishment: ") + details.punishTxid : ""
            }
            M.CheckBox {
                id: bchRecoveryRetentionConsent
                objectName: "bchRecoveryRetentionConsent"
                visible: root.bchRecoveryRetention.submitAvailable === true
                text: qsTr("I approve retaining the private recovery bundle for these exact transactions.")
            }
            M.StandardButton {
                objectName: "bchRecoveryRetentionSubmit"
                text: qsTr("Save reviewed recovery bundle")
                enabled: root.bchRecoveryRetention.submitAvailable === true && bchRecoveryRetentionConsent.checked
                onClicked: {
                    var commitment = root.bchRecoveryRetention.review.commitment
                    bchRecoveryRetentionConsent.checked = false
                    root.bchRecoveryRetentionError = root.dex.submitBchRecoveryRetention(commitment) ? "" : qsTr("Recovery retention could not start. Check saved status and review again.")
                }
            }
            M.TextPlain {
                objectName: "bchRecoveryRetentionOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var receipt: root.bchRecoveryRetention.recovery || ({})
                text: root.bchRecoveryRetentionError ? root.bchRecoveryRetentionError
                    : root.bchRecoveryRetention.busy === true ? qsTr("Checking recovery bundle…")
                    : root.bchRecoveryRetention.state === "saved" && receipt.commitment
                        ? qsTr("Private recovery is saved. No funds were sent and chain maturity was not checked.")
                            + "\n" + qsTr("Saved commitment: ") + receipt.commitment
                    : root.bchRecoveryRetention.state === "unknown"
                        ? qsTr("The result is unknown. The bundle may have been saved. Check saved status before proceeding.")
                    : root.bchRecoveryRetention.state === "not-saved"
                        ? qsTr("No saved recovery bundle was found. Review the exact transactions before saving.")
                    : root.bchRecoveryRetention.state === "reviewed"
                        ? qsTr("Confirm the transaction IDs above before saving. Live trading remains unavailable.")
                    : qsTr("Check saved status before reviewing recovery.")
            }
        }

        ColumnLayout {
            objectName: "bchRefundWalletPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchRefundWallet.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("XMZ refund wallet — status and review")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Read the local wallet records for this BCH / XMZ trade. This does not open a wallet, check its current balance or send funds. Wallet keys remain on your device.")
            }
            M.StandardButton {
                objectName: "bchRefundWalletCheck"
                text: qsTr("Check saved wallet status")
                enabled: root.bchRefundWallet.checkAvailable === true && root.bchRefundWallet.busy !== true
                onClicked: {
                    if (!enabled) return
                    root.bchRefundWalletError = root.dex.requestBchRefundWalletCheck() ? "" : qsTr("Saved wallet check could not start.")
                }
            }
            M.TextPlain {
                objectName: "bchRefundWalletDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var wallet: root.bchRefundWallet.wallet || ({})
                visible: root.bchRefundWallet.busy !== true && ["not-attempted", "attempted", "ready"].indexOf(root.bchRefundWallet.state) >= 0 && !!wallet.address
                text: visible ? qsTr("Wallet address: %1\nLocal wallet name: %2\nTrade: %3").arg(wallet.address).arg(wallet.walletFile).arg(wallet.swapId) : ""
            }
            M.TextPlain {
                objectName: "bchRefundWalletHistory"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var wallet: root.bchRefundWallet.savedWallet || ({})
                visible: (root.bchRefundWallet.busy === true || ["unknown","reviewed","review-expired","recovery-required"].indexOf(root.bchRefundWallet.state)>=0) && !!wallet.address
                text: visible ? qsTr("Last accepted record — historical only, not rechecked:\nWallet address: %1\nLocal wallet name: %2").arg(wallet.address).arg(wallet.walletFile) : ""
            }
            M.TextPlain {
                objectName: "bchRefundWalletOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                text: root.bchRefundWallet.submissionUncertain === true || root.bchRefundWallet.state === "recovery-required" ? qsTr("The wallet operation could not be confirmed. It may have completed. Keep the wallet and trade files; do not restore again or retry automatically. Recovery is required before another operation.")
                    : root.bchRefundWalletError ? root.bchRefundWalletError
                    : root.bchRefundWallet.busy === true && root.bchRefundWallet.state === "submitting" ? qsTr("Applying the approved wallet operation… No payout is being sent.")
                    : root.bchRefundWallet.busy === true ? (root.bchRefundWallet.state === "reviewing" ? qsTr("Checking both chains and preparing the wallet review…") : qsTr("Reading saved local wallet records…"))
                    : root.bchRefundWallet.state === "review-expired" ? qsTr("The fresh review expired. Request another review before proceeding.")
                    : root.bchRefundWallet.state === "reviewed" ? qsTr("Fresh recovery evidence was checked for this review. No wallet was restored or opened, and no funds were sent.")
                    : root.bchRefundWallet.state === "ready" ? qsTr("The saved record says this refund wallet was restored and verified previously. This is not a current balance or chain confirmation. Payout requires separate checks and approval.")
                    : root.bchRefundWallet.state === "attempted" ? qsTr("A wallet restoration attempt was recorded and may have completed. Do not create another wallet. Preserve the existing wallet and trade files for reopening and verification.")
                    : root.bchRefundWallet.state === "not-attempted" ? qsTr("No wallet restoration attempt is recorded for this refund. This does not prove that wallet files are absent.")
                    : root.bchRefundWallet.state === "unknown" ? qsTr("Saved wallet status could not be verified. Preserve wallet and trade files; do not recreate the wallet based on this result.")
                    : qsTr("Check saved records to see the last known refund-wallet state.")
            }
            M.TextPlain {
                objectName:"bchRefundWalletService"
                Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                property string serviceState:root.bchRefundWallet.serviceState || "unconfigured"
                visible:serviceState !== "unconfigured" && serviceState !== "attached"
                text:serviceState === "starting" ? qsTr("Starting the app-owned recovery-wallet service…")
                    : serviceState === "start-refused" ? qsTr("The recovery-wallet service directory could not be resumed. It may be bound to another trade, have an interrupted operation or contain unexpected files. Nothing was changed; preserve the directory and resolve it manually.")
                    : serviceState === "attach-failed" ? qsTr("The recovery-wallet service started but could not be attached to this review. It was stopped cleanly and can be resumed on the next start.")
                    : serviceState === "recovery-required" ? qsTr("The recovery-wallet service stopped unexpectedly. Preserve its directory; it will not be restarted automatically.")
                    : serviceState === "stopped" ? qsTr("The recovery-wallet service has stopped.")
                    : ""
            }
            ColumnLayout {
                Layout.fillWidth:true;visible:root.bchRefundWallet.reviewConfigured === true
                M.TextPlain {
                    Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor
                    text:qsTr("Prepare a review using fresh chain checks. These buttons do not change the wallet. Start with a saved-status check; choose restore only when no restoration attempt is recorded.")
                }
                Flow {
                    Layout.fillWidth:true;spacing:8
                    Repeater {
                        model:[{action:"restore",label:qsTr("Review restoration")},{action:"open",label:qsTr("Review reopening")},{action:"check",label:qsTr("Review verification")}]
                        M.StandardButton {
                            objectName:"bchRefundWalletReview_"+modelData.action;text:modelData.label
                            property string savedState:(root.bchRefundWallet.savedWallet || ({})).state || ""
                            enabled:root.bchRefundWallet.reviewAvailable === true && root.bchRefundWallet.busy !== true
                                && (modelData.action === "restore" ? savedState === "not-attempted" : ["attempted","ready"].indexOf(savedState)>=0)
                            onClicked: {
                                if (!enabled) return
                                root.bchRefundWalletError=root.dex.requestBchRefundWalletReview(modelData.action)?"":qsTr("The fresh wallet review could not start. Check the saved status and local service configuration.")
                            }
                        }
                    }
                }
                M.TextPlain {
                    objectName:"bchRefundWalletFreshReview";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
                    property var review:root.bchRefundWallet.review || ({})
                    property var details:review.details || ({})
                    property var service:details.walletBackend || ({})
                    visible:root.bchRefundWallet.state === "reviewed" && root.bchRefundWallet.busy !== true && root.bchRefundWallet.chainObserved === true && !!review.commitment
                    text:visible?qsTr("Operation: %1\nWallet address: %2\nLocal wallet name: %3\nWallet service: %4:%5 · daemon port %6\nRequired confirmations: BCH %7 · XMZ %8\nReview expires shortly. This is not a wallet balance or payout approval.")
                        .arg(details.action === "restore"?qsTr("Restore wallet"):details.action === "open"?qsTr("Reopen wallet"):qsTr("Verify wallet"))
                        .arg(details.address || "").arg(details.walletFile || "").arg(service.host || "").arg(service.walletPort || "").arg(service.daemonPort || "")
                        .arg(details.minimumBchConfirmations || "").arg(details.minimumXmzConfirmations || ""):""
                }
            }
            ColumnLayout {
                id:bchRefundWalletApproval
                objectName:"bchRefundWalletApproval"
                Layout.fillWidth:true
                property var review:root.bchRefundWallet.review || ({})
                property string action:(review.details || ({})).action || ""
                visible:root.baseCoin === "BCH" && root.bchRefundWallet.reviewConfigured === true && root.bchRefundWallet.submitAvailable === true
                    && root.bchRefundWallet.serviceOwned === true && root.bchRefundWallet.state === "reviewed"
                    && root.bchRefundWallet.chainObserved === true && root.bchRefundWallet.busy !== true
                    && ["restore","open","check"].indexOf(action)>=0 && !!review.commitment
                function clear() {
                    if (bchRefundWalletPassword) bchRefundWalletPassword.text=""
                    if (bchRefundWalletConfirmation) bchRefundWalletConfirmation.text=""
                    if (bchRefundWalletConsent) bchRefundWalletConsent.checked=false
                }
                onVisibleChanged: if (!visible) clear()
                M.LineEdit {
                    id:bchRefundWalletPassword;objectName:"bchRefundWalletPassword";Layout.fillWidth:true;password:true
                    visible:bchRefundWalletApproval.action !== "check"
                    labelText:bchRefundWalletApproval.action === "restore" ? qsTr("New recovery-wallet password") : qsTr("Existing recovery-wallet password")
                }
                M.LineEdit {
                    id:bchRefundWalletConfirmation;objectName:"bchRefundWalletConfirmation";Layout.fillWidth:true;password:true
                    visible:bchRefundWalletApproval.action === "restore";labelText:qsTr("Confirm recovery-wallet password")
                }
                M.CheckBox {
                    id:bchRefundWalletConsent;objectName:"bchRefundWalletConsent"
                    text:qsTr("I approve the wallet operation and address shown above. This does not approve sending funds.")
                }
                M.StandardButton {
                    objectName:"bchRefundWalletSubmit"
                    text:bchRefundWalletApproval.action === "restore" ? qsTr("Restore reviewed wallet") : bchRefundWalletApproval.action === "open" ? qsTr("Reopen reviewed wallet") : qsTr("Verify reviewed wallet")
                    enabled:bchRefundWalletApproval.visible && bchRefundWalletConsent.checked
                        && (bchRefundWalletApproval.action === "check" || bchRefundWalletPassword.text.length>0)
                        && (bchRefundWalletApproval.action !== "restore" || bchRefundWalletPassword.text === bchRefundWalletConfirmation.text)
                    onClicked: {
                        if (!enabled) return
                        var accepted=root.dex.approveBchRefundWallet(bchRefundWalletApproval.review.commitment,
                            bchRefundWalletApproval.action === "check" ? "" : bchRefundWalletPassword.text,
                            bchRefundWalletApproval.action === "restore" ? bchRefundWalletConfirmation.text : "",bchRefundWalletConsent.checked)
                        bchRefundWalletApproval.clear()
                        root.bchRefundWalletError=accepted ? "" : qsTr("The wallet operation could not start. Check its status and request a fresh review.")
                    }
                }
            }
            M.TextPlain {
                Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor
                text:qsTr("Wallet operations require a fresh review and a connected recovery service owned by this app. Payout controls are not yet available.")
            }
        }

        ColumnLayout {
            objectName: "bchPunishmentPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchPunishment.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("BCH timeout recovery review")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Review the saved BCH recovery transaction for the XMZ funder. Sending requires separate approval and fresh checks on both chains.")
            }
            M.StandardButton {
                objectName: "bchPunishmentCheck"
                text: qsTr("Check saved recovery status")
                enabled: root.bchPunishment.checkAvailable === true
                onClicked: root.bchPunishmentError = root.dex.requestBchPunishmentCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchPunishmentReview"
                text: qsTr("Review timeout recovery")
                enabled: root.bchPunishment.reviewAvailable === true
                onClicked: root.bchPunishmentError = root.dex.requestBchPunishmentReview() ? "" : qsTr("Recovery review could not start.")
            }
            M.TextPlain {
                objectName: "bchPunishmentDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchPunishment.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Recovery transaction: ") + details.txid
                    + "\n" + qsTr("BCH payout: ") + root.formatLtcAtomic(details.payoutAtoms) + " BCH"
                    + "\n" + qsTr("Recovery fee: ") + root.formatLtcAtomic(details.feeAtoms) + " BCH"
                    + "\n" + qsTr("Destination script: ") + details.destinationScript
                    + "\n" + qsTr("Required recovery output age (blocks): ") + details.punishDelayBlocks : ""
            }
            M.CheckBox {
                id: bchPunishmentConsent
                objectName: "bchPunishmentConsent"
                visible: root.bchPunishment.submitAvailable === true
                text: qsTr("I approve sending this exact recovery transaction with the displayed payout, destination and fee.")
            }
            M.StandardButton {
                objectName: "bchPunishmentSubmit"
                text: qsTr("Send approved recovery")
                visible: root.bchPunishment.submissionConfigured === true
                enabled: root.bchPunishment.submitAvailable === true && bchPunishmentConsent.checked
                onClicked: {
                    var commitment=(root.bchPunishment.review || {}).commitment || ""
                    bchPunishmentConsent.checked=false
                    root.bchPunishmentError=root.dex.submitBchPunishment(commitment) ? "" : qsTr("Recovery send could not start. Check saved status before proceeding.")
                }
            }
            M.StandardButton {
                objectName: "bchPunishmentObserve"
                text: qsTr("Check chain confirmation")
                enabled: root.bchPunishment.observeAvailable === true
                onClicked: root.bchPunishmentError = root.dex.observeBchPunishment() ? "" : qsTr("Chain check could not start.")
            }
            M.TextPlain {
                objectName: "bchPunishmentOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                text: root.bchPunishmentError ? root.bchPunishmentError
                    : root.bchPunishment.chainObserved === true && root.bchPunishment.observation
                        ? qsTr("Confirmed on the configured BCH node. Confirmations: ") + root.bchPunishment.observation.confirmations
                            + "\n" + qsTr("Block: ") + root.bchPunishment.observation.block
                            + "\n" + qsTr("Observed at: ") + new Date(Number(root.bchPunishment.observation.observedAt) * 1000).toLocaleString()
                    : root.bchPunishment.busy ? (root.bchPunishment.state === "submitting" ? qsTr("Sending the approved recovery transaction…") : qsTr("Reading recovery state…"))
                    : root.bchPunishment.state === "unknown" && root.bchPunishment.submitAttempted === true
                        ? qsTr("The send outcome is unknown. Do not send again. Check saved status and chain confirmation.")
                    : root.bchPunishment.state === "unknown"
                        ? qsTr("Current recovery status could not be verified. Reopen the original configuration if a saved file changed.")
                            + (root.bchPunishment.savedRelayState === "acknowledged" ? "\n" + qsTr("Previously verified send history: acknowledged. This is not chain confirmation.")
                                : root.bchPunishment.savedRelayState === "attempted" ? "\n" + qsTr("Previously verified send history: attempted. The send outcome remains uncertain.") : "")
                    : root.bchPunishment.state === "attempted" ? qsTr("A previous send attempt is recorded. Its outcome is uncertain; this is not chain confirmation.")
                    : root.bchPunishment.state === "acknowledged" ? qsTr("A previous send acknowledgement is recorded. This is not chain confirmation.")
                    : root.bchPunishment.submissionConfigured === true ? qsTr("Review the exact transaction before approving. A send acknowledgement is not chain confirmation.")
                    : qsTr("Offline review is not chain confirmation. Sending is not available here.")
            }
        }

        ColumnLayout {
            objectName: "bchCancellationPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchCancellation.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("BCH cancellation")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Cancellation moves the BCH lock into recovery. It does not release the refund or complete the XMZ payment.")
            }
            M.StandardButton {
                objectName: "bchCancellationCheck"
                text: qsTr("Check saved cancellation status")
                enabled: root.bchCancellation.checkAvailable === true
                onClicked: root.bchCancellationError = root.dex.requestBchCancellationCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchCancellationReview"
                text: qsTr("Review cancellation")
                enabled: root.bchCancellation.reviewAvailable === true
                onClicked: root.bchCancellationError = root.dex.requestBchCancellationReview() ? "" : qsTr("Cancellation review could not start.")
            }
            M.TextPlain {
                objectName: "bchCancellationDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchCancellation.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment && root.bchCancellation.state === "reviewed"
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Cancellation transaction: ") + details.txid
                    + "\n" + qsTr("Funding transaction: ") + String(details.fundingOutpoint || "").split(":")[0]
                    + "\n" + qsTr("Recovery output: ") + root.formatLtcAtomic(details.recoveryAtoms) + " BCH"
                    + "\n" + qsTr("Cancellation fee: ") + root.formatLtcAtomic(details.feeAtoms) + " BCH"
                    + "\n" + qsTr("Required funding age (blocks): ") + details.cancelDelayBlocks : ""
            }
            M.CheckBox {
                id: bchCancellationConsent
                objectName: "bchCancellationConsent"
                visible: root.bchCancellation.submitAvailable === true
                text: qsTr("I approve broadcasting this exact cancellation transaction with the displayed fee.")
            }
            M.StandardButton {
                objectName: "bchCancellationSubmit"
                text: qsTr("Submit reviewed cancellation")
                enabled: root.bchCancellation.submitAvailable === true && bchCancellationConsent.checked
                onClicked: {
                    var commitment = root.bchCancellation.review.commitment
                    bchCancellationConsent.checked = false
                    root.bchCancellationError = root.dex.submitBchCancellation(commitment) ? "" : qsTr("Cancellation submission could not start. Check saved status and review again.")
                }
            }
            M.StandardButton {
                objectName: "bchCancellationObserve"
                text: qsTr("Check cancellation on chain")
                enabled: root.bchCancellation.observeAvailable === true
                onClicked: root.bchCancellationError = root.dex.observeBchCancellation() ? "" : qsTr("Chain check could not start.")
            }
            M.TextPlain {
                objectName: "bchCancellationOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var observation: root.bchCancellation.observation || ({})
                text: root.bchCancellationError ? root.bchCancellationError
                    : root.bchCancellation.busy === true ? qsTr("Checking cancellation…")
                    : root.bchCancellation.chainObserved === true
                        ? qsTr("Cancellation confirmed at the observed chain tip. Refund spendability still needs a separate check.")
                            + "\n" + qsTr("Transaction: ") + observation.txid
                            + "\n" + qsTr("Confirmations: ") + observation.confirmations
                            + "\n" + qsTr("Observed at (Unix time): ") + observation.observedAt
                    : root.bchCancellation.state === "unknown"
                        ? qsTr("The result is unknown. Submission may have reached the node. Check saved status and the chain before proceeding.")
                    : root.bchCancellation.relayState === "acknowledged"
                        ? qsTr("Cancellation submission was acknowledged. This is not chain confirmation.")
                    : root.bchCancellation.relayState === "attempted"
                        ? qsTr("Cancellation was attempted with an uncertain reply. Automatic resubmission is disabled; check the chain.")
                    : root.bchCancellation.state === "reviewed"
                        ? qsTr("Review the transaction and fee, then approve submission. The node must still verify maturity.")
                    : root.bchCancellation.statusKnown === true
                        ? qsTr("No local cancellation submission is saved. A peer may have submitted it; check the chain when needed.")
                    : qsTr("Check saved status before reviewing cancellation.")
            }
        }

        ColumnLayout {
            objectName: "bchRefundReleasePanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchRefundRelease.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("BCH refund")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Refund returns BCH from the confirmed cancellation. Publishing it reveals the share your trading partner needs to recover XMZ.")
            }
            M.StandardButton {
                objectName: "bchRefundReleaseCheck"
                text: qsTr("Check saved refund status")
                enabled: root.bchRefundRelease.checkAvailable === true
                onClicked: root.bchRefundReleaseError = root.dex.requestBchRefundReleaseCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchRefundReleaseReview"
                text: qsTr("Review refund")
                enabled: root.bchRefundRelease.reviewAvailable === true
                onClicked: root.bchRefundReleaseError = root.dex.requestBchRefundReleaseReview() ? "" : qsTr("Refund review could not start.")
            }
            M.TextPlain {
                objectName: "bchRefundReleaseDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchRefundRelease.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment && root.bchRefundRelease.state === "reviewed"
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Refund transaction: ") + details.txid
                    + "\n" + qsTr("Funding transaction: ") + String(details.fundingOutpoint || "").split(":")[0]
                    + "\n" + qsTr("Refund amount: ") + root.formatLtcAtomic(details.refundAtoms) + " BCH"
                    + "\n" + qsTr("Refund fee: ") + root.formatLtcAtomic(details.feeAtoms) + " BCH"
                    + "\n" + qsTr("Refund destination script: ") + details.destinationScript
                    + "\n" + qsTr("Safety margin before punishment (blocks): ") + details.punishmentMarginBlocks : ""
            }
            M.CheckBox {
                id: bchRefundReleaseConsent
                objectName: "bchRefundReleaseConsent"
                visible: root.bchRefundRelease.submitAvailable === true
                text: qsTr("I approve this exact BCH refund and fee, and understand that it lets my trading partner recover XMZ.")
            }
            M.StandardButton {
                objectName: "bchRefundReleaseSubmit"
                text: qsTr("Submit reviewed refund")
                enabled: root.bchRefundRelease.submitAvailable === true && bchRefundReleaseConsent.checked
                onClicked: {
                    var commitment = root.bchRefundRelease.review.commitment
                    bchRefundReleaseConsent.checked = false
                    root.bchRefundReleaseError = root.dex.submitBchRefundRelease(commitment) ? "" : qsTr("Refund submission could not start. Check saved status and review again.")
                }
            }
            M.StandardButton {
                objectName: "bchRefundReleaseObserve"
                text: qsTr("Check refund on chain")
                enabled: root.bchRefundRelease.observeAvailable === true
                onClicked: root.bchRefundReleaseError = root.dex.observeBchRefundRelease() ? "" : qsTr("Chain check could not start.")
            }
            M.TextPlain {
                objectName: "bchRefundReleaseOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var observation: root.bchRefundRelease.observation || ({})
                text: root.bchRefundReleaseError ? root.bchRefundReleaseError
                    : root.bchRefundRelease.busy === true ? qsTr("Checking refund…")
                    : root.bchRefundRelease.chainObserved === true
                        ? qsTr("BCH refund confirmed at the observed chain tip. XMZ recovery requires a separate check.")
                            + "\n" + qsTr("Transaction: ") + observation.txid
                            + "\n" + qsTr("Confirmations: ") + observation.confirmations
                            + "\n" + qsTr("Observed at (Unix time): ") + observation.observedAt
                    : root.bchRefundRelease.state === "unknown"
                        ? qsTr("The result is unknown. Submission may have reached the node. Check saved status and the chain before proceeding.")
                    : root.bchRefundRelease.relayState === "acknowledged"
                        ? qsTr("Refund submission was acknowledged. This is not chain confirmation.")
                    : root.bchRefundRelease.relayState === "attempted"
                        ? qsTr("Refund was attempted with an uncertain reply. Automatic resubmission is disabled; check the chain.")
                    : root.bchRefundRelease.state === "reviewed"
                        ? qsTr("Review the transaction and fee, then approve submission. The node must still verify the unspent recovery output and safety margin before punishment.")
                    : root.bchRefundRelease.statusKnown === true
                        ? qsTr("No local refund submission is saved. Review the exact refund before approving publication.")
                    : qsTr("Check saved status before reviewing refund.")
            }
        }

        ColumnLayout {
            objectName: "bchAdmissionPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchAdmission.configured === true
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("BCH / XMZ development negotiation — save the agreement only; no funds are sent.")
                font.bold: true
            }
            M.StandardButton {
                objectName: "bchAdmissionReview"
                text: qsTr("Review BCH / XMZ agreement")
                enabled: root.bchAdmission.reviewAvailable === true
                onClicked: root.dex.requestBchAdmissionReview()
            }
            M.TextPlain {
                objectName: "bchAdmissionTerms"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                property var review: root.bchAdmission.review || ({})
                property var details: review.details || ({})
                property var terms: details.terms || ({})
                visible: !!review.commitment && !!details.terms
                text: visible ? (details.role === "bch-funder" ? qsTr("You provide BCH and receive XMZ.") : qsTr("You provide XMZ and receive BCH."))
                    + "\n" + qsTr("Networks: ") + terms.bchNetwork + " / " + terms.xmzNetwork
                    + "\n" + qsTr("BCH lock: ") + root.exactAtomicText(terms.bchAtoms, 8) + " BCH"
                    + "\n" + qsTr("XMZ lock: ") + root.exactAtomicText(terms.xmzAtoms, 11) + " XMZ"
                    + "\n" + qsTr("Agreed XMZ payout: ") + root.exactAtomicText(terms.xmzPayoutAtoms, 11) + " XMZ"
                    + "\n" + qsTr("XMZ payout destination: ") + terms.xmzPayout
                    + "\n" + qsTr("XMZ refund destination: ") + terms.xmzRefund
                    + "\n" + qsTr("BCH payout script: ") + terms.bchPayoutScript
                    + "\n" + qsTr("BCH refund script: ") + terms.bchRefundScript
                    + "\n" + qsTr("Maximum BCH funding fee: ") + root.exactAtomicText(terms.bchFundingFeeMaxAtoms, 8) + " BCH"
                    + "\n" + qsTr("BCH cancellation / recovery fees: ") + root.exactAtomicText(terms.bchLockFeeAtoms, 8) + " / " + root.exactAtomicText(terms.bchRecoveryFeeAtoms, 8) + " BCH"
                    + "\n" + qsTr("Maximum XMZ funding / claim / refund fees: ") + root.exactAtomicText(terms.xmzFundingFeeMaxAtoms, 11) + " / " + root.exactAtomicText(terms.xmzClaimFeeMaxAtoms, 11) + " / " + root.exactAtomicText(terms.xmzRefundFeeMaxAtoms, 11) + " XMZ"
                    + "\n" + qsTr("Required confirmations — BCH / XMZ: ") + terms.bchConfirmations + " / " + terms.xmzConfirmations
                    + "\n" + qsTr("Cancellation / punishment delays in BCH blocks: ") + terms.cancelBlocks + " / " + terms.punishBlocks
                    + "\n" + qsTr("Expires: ") + new Date(Number(terms.expiresAt) * 1000).toLocaleString()
                    + "\n" + qsTr("Trade: ") + terms.swapId
                    + "\n" + qsTr("Saving revalidates the retained negotiation. Current chain state has not been checked.") : ""
            }
            M.CheckBox {
                id: bchAdmissionConsent
                objectName: "bchAdmissionConsent"
                visible: root.bchAdmission.submitAvailable === true
                text: qsTr("I approve saving this exact agreement, role, destinations and fee limits.")
            }
            M.StandardButton {
                objectName: "bchAdmissionSubmit"
                text: qsTr("Save reviewed BCH agreement")
                enabled: root.bchAdmission.submitAvailable === true && bchAdmissionConsent.checked
                onClicked: {
                    var commitment = root.bchAdmission.review.commitment
                    bchAdmissionConsent.checked = false
                    root.dex.submitBchAdmission(commitment)
                }
            }
            M.StandardButton {
                objectName: "bchAdmissionCheck"
                text: qsTr("Check saved BCH agreement")
                enabled: root.bchAdmission.checkAvailable === true
                onClicked: root.dex.requestBchAdmissionCheck()
            }
            M.TextPlain {
                objectName: "bchAdmissionOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: root.bchAdmission.state === "admitted" ? qsTr("Saved agreement and original approval verified. No funding or chain confirmation is implied; live trading remains unavailable.")
                    : root.bchAdmission.state === "unknown" ? qsTr("Admission status is unknown. Check the saved agreement before taking any further action.")
                    : root.bchAdmission.busy === true ? qsTr("Checking the private BCH agreement…") : ""
            }
        }

        M.BchLightDexFundingPanel {
            objectName: "bchLightDexFundingPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && (root.bchLightFunding.configured === true || root.bchAdmission.role === "bch-funder")
            backend: root.dex
            funding: root.bchLightFunding
            formatAmount: root.formatLtcAtomic
            parseAmount: root.parseLtcAtomic
        }

        ColumnLayout {
            objectName: "bchNativePreparationPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchNativePreparation.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("Prepare XMZ deposit")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Review the deposit and fee ceiling. Preparation checks the BCH lock and constructs a saved XMZ transaction. Broadcasting requires separate approval.")
            }
            M.StandardButton {
                objectName: "bchNativePreparationCheck"; text: qsTr("Check saved deposit")
                enabled: root.bchNativePreparation.checkAvailable === true
                onClicked: root.bchNativePreparationError = root.dex.requestBchNativePreparationCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchNativePreparationReview"; text: qsTr("Review XMZ deposit")
                enabled: root.bchNativePreparation.reviewAvailable === true
                onClicked: root.bchNativePreparationError = root.dex.requestBchNativePreparationReview() ? "" : qsTr("Deposit review could not start.")
            }
            M.TextPlain {
                objectName: "bchNativePreparationDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchNativePreparation.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Source wallet: ") + details.wallet
                    + "\n" + qsTr("Account: ") + details.account + qsTr(" · Priority: ") + details.priority
                    + "\n" + qsTr("Shared deposit address: ") + details.destination
                    + "\n" + qsTr("Deposit: ") + root.exactAtomicText(details.amountAtoms,11) + " XMZ"
                    + "\n" + qsTr("Maximum funding fee: ") + root.exactAtomicText(details.maximumFeeAtoms,11) + " XMZ"
                    + "\n" + qsTr("Cancellation margin (BCH blocks): ") + details.safetyBlocks
                    + "\n" + qsTr("Review commitment: ") + review.commitment : ""
            }
            M.CheckBox {
                id: bchNativePreparationConsent; objectName: "bchNativePreparationConsent"
                visible: root.bchNativePreparation.submitAvailable === true
                text: qsTr("I approve constructing this exact XMZ deposit within the reviewed fee ceiling.")
            }
            M.StandardButton {
                objectName: "bchNativePreparationSubmit"; text: qsTr("Prepare approved deposit")
                enabled: root.bchNativePreparation.submitAvailable === true && bchNativePreparationConsent.checked
                onClicked: {
                    var commitment = root.bchNativePreparation.review.commitment
                    bchNativePreparationConsent.checked = false
                    root.bchNativePreparationError = root.dex.submitBchNativePreparation(commitment) ? "" : qsTr("Preparation could not start. Check saved status before proceeding.")
                }
            }
            M.TextPlain {
                objectName: "bchNativePreparationOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var preparation: root.bchNativePreparation.preparation || ({})
                property var payment: preparation.payment || ({})
                text: root.bchNativePreparationError ? root.bchNativePreparationError
                    : root.bchNativePreparation.busy === true ? qsTr("Checking or preparing the deposit…")
                    : root.bchNativePreparation.state === "prepared" ? qsTr("Deposit prepared. It has not been broadcast by this action.")
                        + "\n" + qsTr("Transaction: ") + payment.txid
                        + "\n" + qsTr("Funding fee: ") + root.exactAtomicText(payment.feeAtoms,11) + " XMZ"
                    : root.bchNativePreparation.state === "attempted" || root.bchNativePreparation.state === "unknown"
                        ? qsTr("Preparation may have been attempted. Check saved status; do not construct a replacement transaction.")
                    : qsTr("Check saved status, review the deposit, then approve preparation.")
            }
        }

        ColumnLayout {
            objectName: "bchNativeRelayPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchNativeRelay.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("Broadcast saved XMZ deposit")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Review the saved transaction before broadcasting. The BCH lock and source wallet are checked again before dispatch. Wallet acknowledgement is not chain confirmation.")
            }
            M.StandardButton {
                objectName: "bchNativeRelayCheck"; text: qsTr("Check broadcast status")
                enabled: root.bchNativeRelay.checkAvailable === true
                onClicked: root.bchNativeRelayError = root.dex.requestBchNativeRelayCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchNativeRelayReview"; text: qsTr("Review saved broadcast")
                enabled: root.bchNativeRelay.reviewAvailable === true
                onClicked: root.bchNativeRelayError = root.dex.requestBchNativeRelayReview() ? "" : qsTr("Broadcast review could not start.")
            }
            M.TextPlain {
                objectName: "bchNativeRelayDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchNativeRelay.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Source wallet: ") + details.wallet
                    + "\n" + qsTr("Account: ") + details.account + qsTr(" · Priority: ") + details.priority
                    + "\n" + qsTr("Shared deposit address: ") + details.destination
                    + "\n" + qsTr("Deposit: ") + root.exactAtomicText(details.amountAtoms,11) + " XMZ"
                    + "\n" + qsTr("Transaction: ") + details.txid
                    + "\n" + qsTr("Funding fee: ") + root.exactAtomicText(details.feeAtoms,11) + " XMZ"
                    + "\n" + qsTr("Maximum funding fee: ") + root.exactAtomicText(details.maximumFeeAtoms,11) + " XMZ"
                    + "\n" + qsTr("Cancellation margin (BCH blocks): ") + details.cancellationMarginBlocks
                    + "\n" + qsTr("Review commitment: ") + review.commitment : ""
            }
            M.CheckBox {
                id: bchNativeRelayConsent; objectName: "bchNativeRelayConsent"
                visible: root.bchNativeRelay.submitAvailable === true
                text: qsTr("I approve broadcasting this exact saved XMZ transaction and its displayed fee.")
            }
            M.StandardButton {
                objectName: "bchNativeRelaySubmit"; text: qsTr("Broadcast approved transaction")
                enabled: root.bchNativeRelay.submitAvailable === true && bchNativeRelayConsent.checked
                onClicked: {
                    var commitment = root.bchNativeRelay.review.commitment
                    bchNativeRelayConsent.checked = false
                    root.bchNativeRelayError = root.dex.submitBchNativeRelay(commitment) ? "" : qsTr("Broadcast could not start. Check saved status before proceeding.")
                }
            }
            M.TextPlain {
                objectName: "bchNativeRelayOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var relay: root.bchNativeRelay.relay || ({})
                text: root.bchNativeRelayError ? root.bchNativeRelayError
                    : root.bchNativeRelay.busy === true ? qsTr("Checking or broadcasting the saved deposit…")
                    : root.bchNativeRelay.state === "acknowledged" ? qsTr("Wallet acknowledged broadcast. Chain confirmation has not been verified by this status check.")
                        + "\n" + qsTr("Transaction: ") + relay.txid

                    : root.bchNativeRelay.state === "attempted" || root.bchNativeRelay.state === "unknown"
                        ? qsTr("Broadcast may have been attempted. Check saved status; do not resend or construct a replacement transaction.")
                    : qsTr("Check saved status, review the saved payment, then approve broadcast.")
            }
        }

        ColumnLayout {
            objectName: "bchNativeProofPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchNativeProof.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("Receive XMZ payment proof")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Review and retain the authenticated payment proof for this trade. Saving a receipt does not confirm the XMZ payment or authorize settlement.")
            }
            M.StandardButton {
                objectName: "bchNativeProofCheck"; text: qsTr("Check proof receipt")
                enabled: root.bchNativeProof.checkAvailable === true
                onClicked: root.bchNativeProofError = root.dex.requestBchNativeProofCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchNativeProofReview"; text: qsTr("Review payment proof")
                enabled: root.bchNativeProof.reviewAvailable === true
                onClicked: root.bchNativeProofError = root.dex.requestBchNativeProofReview() ? "" : qsTr("Payment proof review could not start.")
            }
            M.TextPlain {
                objectName: "bchNativeProofDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchNativeProof.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("BCH funding output: ") + details.bchFundingOutpoint
                    + "\n" + qsTr("Shared deposit address: ") + details.destination
                    + "\n" + qsTr("Deposit: ") + root.exactAtomicText(details.amountAtoms,11) + " XMZ"
                    + "\n" + qsTr("XMZ transaction: ") + details.txid
                    + "\n" + qsTr("Review commitment: ") + review.commitment : ""
            }
            M.CheckBox {
                id: bchNativeProofConsent; objectName: "bchNativeProofConsent"
                visible: root.bchNativeProof.submitAvailable === true
                text: qsTr("I approve retaining this payment proof for the displayed trade, deposit address and amount.")
            }
            M.StandardButton {
                objectName: "bchNativeProofSubmit"; text: qsTr("Save approved proof")
                enabled: root.bchNativeProof.submitAvailable === true && bchNativeProofConsent.checked
                onClicked: {
                    var commitment = root.bchNativeProof.review.commitment
                    bchNativeProofConsent.checked = false
                    root.bchNativeProofError = root.dex.submitBchNativeProof(commitment) ? "" : qsTr("Proof receipt could not start. Check saved status before proceeding.")
                }
            }
            M.TextPlain {
                objectName: "bchNativeProofOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var receipt: root.bchNativeProof.receipt || ({})
                text: root.bchNativeProofError ? root.bchNativeProofError
                    : root.bchNativeProof.busy === true ? qsTr("Checking or retaining the payment proof…")
                    : root.bchNativeProof.state === "saved" ? qsTr("Payment proof retained. Chain confirmation has not been verified by this status check.")
                        + "\n" + qsTr("Receipt time (Unix seconds): ") + receipt.receivedAt
                    : root.bchNativeProof.state === "unknown"
                        ? qsTr("Receipt outcome is unknown. Check saved status before proceeding; do not assume the payment is confirmed.")
                    : qsTr("Check saved status, review the payment proof, then approve its receipt.")
            }
        }

        ColumnLayout {
            objectName: "bchNativeObservationPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchNativeObservation.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("Check XMZ deposit on-chain")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Verify the retained payment proof against the configured chain observer. Evidence applies to the displayed chain tip; settlement requires its own review and approval.")
            }
            M.StandardButton {
                objectName: "bchNativeObservationCheck"; text: qsTr("Check XMZ confirmation")
                enabled: root.bchNativeObservation.checkAvailable === true
                onClicked: root.bchNativeObservationError = root.dex.requestBchNativeObservation() ? "" : qsTr("Chain check could not start.")
            }
            M.TextPlain {
                objectName: "bchNativeObservationDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var observation: root.bchNativeObservation.observation || ({})
                visible: root.bchNativeObservation.state === "confirmed" && root.bchNativeObservation.chainObserved === true
                text: visible ? qsTr("XMZ transaction: ") + observation.txid
                    + "\n" + qsTr("Deposit address: ") + observation.destination
                    + "\n" + qsTr("Deposit: ") + root.exactAtomicText(observation.amountAtoms,11) + " XMZ"
                    + "\n" + qsTr("Confirmations at check: ") + observation.confirmations
                    + "\n" + qsTr("Transaction block height: ") + observation.height
                    + "\n" + qsTr("Chain tip height: ") + observation.tipHeight
                    + "\n" + qsTr("Chain tip: ") + observation.tip
                    + "\n" + qsTr("Checked: ") + new Date(Number(observation.observedAt)*1000).toLocaleString() : ""
            }
            M.TextPlain {
                objectName: "bchNativeObservationOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                text: root.bchNativeObservationError ? root.bchNativeObservationError
                    : root.bchNativeObservation.busy === true ? qsTr("Checking the XMZ payment on-chain…")
                    : root.bchNativeObservation.state === "confirmed" ? qsTr("Payment verified at the displayed chain tip. This is not settlement approval.")
                    : root.bchNativeObservation.state === "unknown" ? qsTr("Confirmation is unknown. Previous chain evidence has been cleared; run a fresh check.")
                    : root.bchNativeProof.state !== "saved" ? qsTr("Check the saved payment-proof receipt before requesting chain confirmation.")
                    : qsTr("Ready for a fresh XMZ chain check.")
            }
        }

        ColumnLayout {
            objectName: "evmTradePanel"
            Layout.fillWidth: true
            property var snapshot: root.evmTrade.snapshot || ({})
            property var offer: snapshot.offer || ({})
            visible: root.baseCoin === "BNB" && root.evmTrade.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("XMZ ↔ BNB trade (BNB testnet prototype)")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Each side keeps its own coins until the swap completes or refunds. Nobody, including Monzero, can take them. The app checks the trade every 20 seconds, so it can confirm, claim, refund or sweep on time even if you don't click anything.")
            }
            M.TextPlain {
                objectName: "evmTradeDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var p: parent
                visible: root.evmTrade.state === "ok" || p.snapshot.stage !== undefined
                text: !visible ? "" : (p.snapshot.role === "owner" ? qsTr("You buy XMZ with BNB.") : qsTr("You sell XMZ for BNB."))
                    + "\n" + qsTr("BNB: ") + root.exactAtomicText(p.offer.value, 18)
                    + "\n" + qsTr("XMZ: ") + root.exactAtomicText(p.offer.xmzAmount, 11)
                    + "\n" + qsTr("Status: ") + root.evmStageText(p.snapshot.role, p.snapshot.stage)
                    + (p.snapshot.deadlines ? "\n" + qsTr("Claim deadline: ") + new Date(Number(p.snapshot.deadlines.timeout1)*1000).toLocaleString()
                        + "\n" + qsTr("Refund opens: ") + new Date(Number(p.snapshot.deadlines.timeout2)*1000).toLocaleString() : "")
                    + "\n" + qsTr("Trade: ") + p.offer.tradeId
            }
            ColumnLayout {
                objectName: "evmTradeApproval"
                Layout.fillWidth: true
                visible: root.evmTrade.canApprove === true
                M.TextPlain {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    text: (parent.parent.snapshot.role === "owner"
                            ? qsTr("Approve to let the app lock your BNB in the swap contract for this trade.")
                            : qsTr("Approve to let the app lock your XMZ for this trade."))
                        + "\n" + qsTr("Check the amounts above. Approval covers only this trade: ") + root.evmTrade.commitment
                }
                M.StandardButton {
                    objectName: "evmTradeApprove"; text: qsTr("Approve locking my funds")
                    onClicked: root.evmTradeError = root.dex.approveEvmTrade(root.evmTrade.commitment) ? "" : qsTr("Approval could not start.")
                }
            }
            M.StandardButton {
                objectName: "evmTradeRefresh"; text: qsTr("Refresh")
                enabled: root.evmTrade.busy !== true
                onClicked: root.evmTradeError = root.dex.refreshEvmTrade() ? "" : qsTr("Refresh could not start.")
            }
            M.TextPlain {
                objectName: "evmTradeOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                text: root.evmTradeError ? root.evmTradeError
                    : root.evmTrade.state === "mismatch" ? qsTr("The trade folder now holds a different trade. Nothing was changed; check the trade folder.")
                    : root.evmTrade.state === "source-changed" ? qsTr("The trade settings changed while the app was running. Restart the app.")
                    : root.evmTrade.state === "refused" || root.evmTrade.state === "failed" ? qsTr("Last check failed: ") + (root.evmTrade.error || qsTr("unknown error")) + qsTr(". The app will try again.")
                    : root.evmTrade.stepError ? qsTr("Last check failed: ") + root.evmTrade.stepError + qsTr(". The status above is the trade's saved state; the app will try again.")
                    : root.evmTrade.approvalPending === true ? qsTr("Approval received. It is sent as soon as the current check finishes.")
                    : root.evmTrade.awaitingApproval === true ? qsTr("Waiting for your approval before locking anything.")
                    : root.evmTrade.busy === true ? qsTr("Checking the trade…")
                    : ""
            }
        }

        ColumnLayout {
            objectName: "bchPublicationRepairPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchPublicationRepair.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("Repair saved BCH trade records")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("If saving a trade was interrupted, review the retained record before repairing it. Repair uses the exact data on this device and does not send funds. Repair preparation before the signed record when both are incomplete.")
            }
            M.StandardButton {
                objectName: "bchPublicationRepairPrepared"; text: qsTr("Review preparation repair")
                enabled: root.bchPublicationRepair.reviewAvailable === true
                onClicked: root.bchPublicationRepairError = root.dex.requestBchPublicationRepairReview("prepared") ? "" : qsTr("Repair review could not start.")
            }
            M.StandardButton {
                objectName: "bchPublicationRepairSigned"; text: qsTr("Review signed record repair")
                enabled: root.bchPublicationRepair.reviewAvailable === true
                onClicked: root.bchPublicationRepairError = root.dex.requestBchPublicationRepairReview("signed") ? "" : qsTr("Repair review could not start.")
            }
            M.TextPlain {
                objectName: "bchPublicationRepairDetails"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                visible: root.bchPublicationRepair.state === "reviewed"
                text: qsTr("Trade: ") + (root.bchPublicationRepair.tradeId || "") + "\n"
                    + (root.bchPublicationRepair.stage === "signed" ? qsTr("Record: signed settlement") : qsTr("Record: settlement preparation"))
            }
            M.CheckBox {
                id: bchPublicationRepairConsent; objectName: "bchPublicationRepairConsent"
                Layout.fillWidth: true
                enabled: root.bchPublicationRepair.submitAvailable === true && root.bchPublicationRepair.state === "reviewed"
                text: qsTr("I approve restoring this exact saved record for the displayed trade.")
            }
            M.StandardButton {
                objectName: "bchPublicationRepairSubmit"; text: qsTr("Repair approved record")
                enabled: bchPublicationRepairConsent.enabled && bchPublicationRepairConsent.checked
                onClicked: {
                    var approval = (root.bchPublicationRepair.review || {}).commitment || ""
                    bchPublicationRepairConsent.checked = false
                    root.bchPublicationRepairError = root.dex.submitBchPublicationRepair(approval) ? "" : qsTr("Repair could not start. Review the saved record again.")
                }
            }
            M.TextPlain {
                objectName: "bchPublicationRepairOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                text: root.bchPublicationRepairError ? root.bchPublicationRepairError
                    : root.bchPublicationRepair.busy === true ? qsTr("Checking the saved record…")
                    : root.bchPublicationRepair.state === "reviewed" ? qsTr("Review complete. Approve the displayed record to finish saving it.")
                    : root.bchPublicationRepair.state === "repaired" ? qsTr("Saved record repaired. Run fresh settlement checks before continuing the trade.")
                    : root.bchPublicationRepair.state === "unknown" ? qsTr("Repair status is unknown. Review the saved record again before approving another attempt.")
                    : qsTr("Choose the record that needs repair. Nothing is changed until you approve it.")
            }
        }

        ColumnLayout {
            objectName: "bchSuccessReleaseReviewPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && (root.bchSuccessReleaseReview.configured === true || root.bchSuccessPreparation.configured === true)
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("Review BCH settlement")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Review the payout and confirmation requirements recorded for this trade. This review does not check current chain state or release funds.")
            }
            M.StandardButton {
                objectName: "bchSuccessReleaseReviewRequest"; text: qsTr("Review settlement")
                enabled: root.bchSuccessReleaseReview.reviewAvailable === true
                onClicked: root.bchSuccessReleaseReviewError = root.dex.requestBchSuccessReleaseReview() ? "" : qsTr("Settlement review could not start.")
            }
            M.TextPlain {
                objectName: "bchSuccessReleaseReviewDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var details: (root.bchSuccessReleaseReview.review || {}).details || ({})
                visible: root.bchSuccessReleaseReview.state === "reviewed"
                text: visible ? qsTr("BCH funding output: ") + details.bchFundingOutpoint
                    + "\n" + qsTr("BCH payout: ") + root.exactAtomicText(details.bchPayoutAtoms,8) + " BCH"
                    + "\n" + qsTr("BCH fee: ") + root.exactAtomicText(details.bchFeeAtoms,8) + " BCH"
                    + "\n" + qsTr("Payout script: ") + details.bchPayoutScript
                    + "\n" + qsTr("XMZ transaction: ") + details.nativeTxid
                    + "\n" + qsTr("XMZ deposit address: ") + details.nativeDestination
                    + "\n" + qsTr("XMZ deposit: ") + root.exactAtomicText(details.nativeAmountAtoms,11) + " XMZ"
                    + "\n" + qsTr("Required BCH confirmations: ") + details.minimumBchConfirmations
                    + "\n" + qsTr("Required XMZ confirmations: ") + details.minimumNativeConfirmations
                    + "\n" + qsTr("Cancellation margin (BCH blocks): ") + details.cancellationMarginBlocks : ""
            }
            ColumnLayout {
                objectName: "bchSuccessPreparationPanel"
                Layout.fillWidth: true
                visible: root.bchSuccessPreparation.configured === true
                enabled: visible
                M.StandardButton {
                    objectName: "bchSuccessPreparationCheck"; text: qsTr("Check saved preparation")
                    enabled: root.bchSuccessPreparation.checkAvailable === true
                    onClicked: root.bchSuccessPreparationError = root.dex.requestBchSuccessPreparationCheck() ? "" : qsTr("Saved preparation check could not start.")
                }
                M.CheckBox {
                    id: bchSuccessPreparationConsent; objectName: "bchSuccessPreparationConsent"
                    Layout.fillWidth: true
                    enabled: root.bchSuccessPreparation.submitAvailable === true && root.bchSuccessReleaseReview.state === "reviewed"
                    text: qsTr("I approve preparing settlement for the displayed payout and fee after fresh checks on both chains.")
                }
                M.StandardButton {
                    objectName: "bchSuccessPreparationSubmit"; text: qsTr("Prepare approved settlement")
                    enabled: bchSuccessPreparationConsent.enabled && bchSuccessPreparationConsent.checked
                    onClicked: {
                        var commitment = root.bchSuccessReleaseReview.review.commitment
                        bchSuccessPreparationConsent.checked = false
                        root.bchSuccessPreparationError = root.dex.submitBchSuccessPreparation(commitment) ? "" : qsTr("Preparation could not start. Review the current status before trying again.")
                    }
                }
                M.TextPlain {
                    objectName: "bchSuccessPreparationOutcome"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    text: root.bchSuccessPreparationError ? root.bchSuccessPreparationError
                        : root.bchSuccessPreparation.state === "checking" ? qsTr("Checking saved preparation. This does not check current chain state…")
                        : root.bchSuccessPreparation.busy === true ? qsTr("Checking both chains and preparing private settlement material…")
                        : root.bchSuccessPreparation.state === "saved" && root.bchSuccessPreparation.statusKnown === true
                            ? qsTr("Saved preparation restored. Current chain state has not been checked.")
                                + "\n" + qsTr("Prepared at (Unix seconds): ") + (root.bchSuccessPreparation.preparation || {}).preparedAt
                                + "\n" + ((root.bchSuccessPreparation.preparation || {}).signatureRetained === true
                                    ? qsTr("Identity signature retained. This does not prove that funds were released.") : qsTr("No identity signature retained."))
                        : root.bchSuccessPreparation.state === "checked" && root.bchSuccessPreparation.statusKnown === true
                            ? qsTr("No saved preparation found. Review and approve the trade before preparing settlement.")
                        : root.bchSuccessPreparation.state === "prepared" ? qsTr("Settlement material saved privately. Funds have not been released; signing and release require separate checks.")
                        : root.bchSuccessPreparation.state === "unknown" && root.bchSuccessPreparation.attempted === true
                            ? qsTr("Preparation outcome is unknown. Private material may have been saved. Nothing will retry automatically; check saved preparation before explicitly trying again.")
                        : root.bchSuccessPreparation.state === "unknown" ? qsTr("Preparation is unavailable. Request a fresh settlement review.")
                        : qsTr("Preparation stores private settlement material and checks both chains. It does not sign or release funds.")
                }
            }
            M.TextPlain {
                objectName: "bchSuccessReleaseReviewOutcome"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                text: root.bchSuccessReleaseReviewError ? root.bchSuccessReleaseReviewError
                    : root.bchSuccessReleaseReview.busy === true ? qsTr("Reviewing settlement…")
                    : root.bchSuccessReleaseReview.state === "reviewed" ? qsTr("Review loaded. Releasing funds will require separate approval and fresh checks on both chains.")
                    : root.bchSuccessReleaseReview.state === "unknown" ? qsTr("Review unavailable. Previous details have been cleared; request a fresh review.")
                    : root.bchNativeProof.state !== "saved" ? qsTr("Check the saved payment-proof receipt before reviewing settlement.")
                    : qsTr("Ready to review settlement.")
            }
        }

        Repeater {
            model: 2
            delegate: ColumnLayout {
                id: signingReviewPanel
                property bool signing: index === 0
                property var status: signing ? root.bchSuccessSignatureReview : root.bchSuccessSignedReleaseReview
                property string error: signing ? root.bchSuccessSignatureReviewError : root.bchSuccessSignedReleaseReviewError
                property string namePrefix: signing ? "bchSuccessSignatureReview" : "bchSuccessSignedReleaseReview"
                objectName: namePrefix + "Panel"
                Layout.fillWidth: true
                visible: root.baseCoin === "BCH" && status.configured === true
                enabled: visible
                M.TextPlain {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                    text: signingReviewPanel.signing ? qsTr("Review settlement signing") : qsTr("Review signed settlement release")
                }
                M.TextPlain {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                    text: qsTr("This review reads saved trade state. It does not sign, send settlement material, or check current confirmations.")
                }
                M.StandardButton {
                    objectName: signingReviewPanel.namePrefix + "Request"
                    text: signingReviewPanel.signing ? qsTr("Review signing") : qsTr("Review release")
                    enabled: signingReviewPanel.status.reviewAvailable === true
                    onClicked: {
                        if (signingReviewPanel.signing)
                            root.bchSuccessSignatureReviewError = root.dex.requestBchSuccessSignatureReview() ? "" : qsTr("Signing review could not start.")
                        else
                            root.bchSuccessSignedReleaseReviewError = root.dex.requestBchSuccessSignedReleaseReview() ? "" : qsTr("Release review could not start.")
                    }
                }
                M.TextPlain {
                    objectName: signingReviewPanel.namePrefix + "Details"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    property var details: (signingReviewPanel.status.review || {}).details || ({})
                    visible: signingReviewPanel.status.state === "reviewed"
                text: visible ? qsTr("BCH funding output: ") + details.bchFundingOutpoint
                    + "\n" + qsTr("BCH payout: ") + root.exactAtomicText(details.bchPayoutAtoms,8) + " BCH"
                    + "\n" + qsTr("BCH fee: ") + root.exactAtomicText(details.bchFeeAtoms,8) + " BCH"
                    + "\n" + qsTr("Payout script: ") + details.bchPayoutScript
                    + "\n" + qsTr("XMZ transaction: ") + details.nativeTxid
                    + "\n" + qsTr("XMZ deposit address: ") + details.nativeDestination
                    + "\n" + qsTr("XMZ deposit: ") + root.exactAtomicText(details.nativeAmountAtoms,11) + " XMZ"
                    + "\n" + qsTr("Required BCH confirmations: ") + details.minimumBchConfirmations
                    + "\n" + qsTr("Required XMZ confirmations: ") + details.minimumNativeConfirmations
                    + "\n" + qsTr("Cancellation margin (BCH blocks): ") + details.cancellationMarginBlocks : ""
                }
                M.TextPlain {
                    objectName: signingReviewPanel.namePrefix + "Outcome"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    text: signingReviewPanel.error ? signingReviewPanel.error
                        : signingReviewPanel.status.busy === true ? qsTr("Reviewing saved settlement state…")
                        : signingReviewPanel.status.state === "reviewed" ? qsTr("Review verified. Signing and release require their own approval and fresh checks on both chains.")
                        : signingReviewPanel.status.state === "unknown" ? qsTr("Review could not be verified. Check the saved trade state before trying again.") : ""
                }
            }
        }

        ColumnLayout {
            objectName: "bchFundingReceiptPanel"
            Layout.fillWidth: true
            visible: root.baseCoin === "BCH" && root.bchFundingReceipt.configured === true
            enabled: visible
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; font.bold: true
                text: qsTr("Peer BCH funding message")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: qsTr("Review and save the authenticated funding message for this trade. BCH confirmation and approval to fund XMZ are separate steps.")
            }
            M.StandardButton {
                objectName: "bchFundingReceiptCheck"
                text: qsTr("Check saved funding message")
                enabled: root.bchFundingReceipt.checkAvailable === true
                onClicked: root.bchFundingReceiptError = root.dex.requestBchFundingReceiptCheck() ? "" : qsTr("Saved-status check could not start.")
            }
            M.StandardButton {
                objectName: "bchFundingReceiptReview"
                text: qsTr("Review peer funding message")
                enabled: root.bchFundingReceipt.reviewAvailable === true
                onClicked: root.bchFundingReceiptError = root.dex.requestBchFundingReceiptReview() ? "" : qsTr("Peer-message review could not start.")
            }
            M.TextPlain {
                objectName: "bchFundingReceiptDetails"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var review: root.bchFundingReceipt.review || ({})
                property var details: review.details || ({})
                visible: !!review.commitment && (root.bchFundingReceipt.state === "reviewed" || root.bchFundingReceipt.state === "saved")
                text: visible ? qsTr("Trade: ") + details.swapId
                    + "\n" + qsTr("Peer identity: ") + root.bchFundingReceipt.peerIdentity
                    + "\n" + qsTr("Network: ") + details.bchNetwork
                    + "\n" + qsTr("Funding outpoint: ") + details.fundingOutpoint
                    + "\n" + qsTr("Funding amount: ") + root.exactAtomicText(details.principalAtoms,8) + " BCH"
                    + "\n" + qsTr("Funding fee: ") + root.exactAtomicText(details.feeAtoms,8) + " BCH"
                    + "\n" + qsTr("Contract script: ") + details.contractScript
                    + "\n" + qsTr("Review commitment: ") + review.commitment : ""
            }
            M.CheckBox {
                id: bchFundingReceiptConsent
                objectName: "bchFundingReceiptConsent"
                visible: root.bchFundingReceipt.submitAvailable === true
                text: qsTr("I approve saving this exact BCH funding message for the reviewed trade.")
            }
            M.StandardButton {
                objectName: "bchFundingReceiptSubmit"
                text: qsTr("Accept reviewed funding message")
                enabled: root.bchFundingReceipt.submitAvailable === true && bchFundingReceiptConsent.checked
                onClicked: {
                    var commitment = root.bchFundingReceipt.review.commitment
                    bchFundingReceiptConsent.checked = false
                    root.bchFundingReceiptError = root.dex.submitBchFundingReceipt(commitment) ? "" : qsTr("Acceptance could not start. Check saved status before proceeding.")
                }
            }
            M.TextPlain {
                objectName: "bchFundingReceiptOutcome"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                property var receipt: root.bchFundingReceipt.receipt || ({})
                text: root.bchFundingReceiptError ? root.bchFundingReceiptError
                    : root.bchFundingReceipt.busy === true ? qsTr("Checking the funding message…")
                    : root.bchFundingReceipt.state === "saved" && receipt.commitment
                        ? qsTr("Funding message saved. BCH confirmation and XMZ funding approval remain separate steps.")
                            + "\n" + qsTr("Receipt: ") + receipt.commitment
                    : root.bchFundingReceipt.state === "unknown"
                        ? qsTr("The result is unknown. Acceptance may have been saved. Check saved status before proceeding.")
                    : root.bchFundingReceipt.state === "not-saved"
                        ? qsTr("No saved funding message was found. Review the configured peer message before accepting it.")
                    : root.bchFundingReceipt.state === "reviewed"
                        ? qsTr("Confirm the exact transaction and amounts above before saving this message.")
                    : qsTr("Check saved status before reviewing the peer message.")
            }
        }

        Rectangle {
            Layout.fillWidth:true; Layout.preferredHeight:52; radius:8
            color:M.Style.middlePanelBackgroundColor; border.width:1; border.color:M.Style.inputBorderColorInActive
            RowLayout {
                anchors.fill:parent; anchors.margins:10
                M.TextPlain { color:M.Style.defaultFontColor; text:qsTr("Private adapter status") + translationManager.emptyString }
                M.TextPlain { color:"#ef6575"; font.bold:true; text:qsTr("LIVE BLOCKED") + " · " + root.readiness.checksPassed + "/" + root.readiness.checksRequired + " " + qsTr("checks") }
                M.TextPlain { Layout.fillWidth:true; elide:Text.ElideRight; color:M.Style.defaultFontColor; opacity:.65; text:root.readiness.blockers.join(" · ") }
            }
        }

        M.TextPlain {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.walletBackend.state === "recovery-required" ? "#ef6575" : M.Style.defaultFontColor
            text: root.walletBackendText() + translationManager.emptyString
        }

        M.TextPlain {
            Layout.fillWidth: true
            visible: root.walletBackend.attached
            wrapMode: Text.WordWrap
            color: M.Style.defaultFontColor
            objectName: "nativeRecoveryEvidence"
            text: (root.nativeRecovery.valid
                ? qsTr("Recovery evidence: local wallet confirms transaction ") + root.nativeRecovery.txid
                    + " (" + root.nativeRecovery.confirmations + " " + qsTr("confirmations") + "). "
                : qsTr("Recovery evidence: no current verified result. "))
                + qsTr("Read-only status; this does not authorize retrying a payment or enable trading.")
                + " " + qsTr("A confirmed payment does not prove that all swap funds have been recovered.")
                + translationManager.emptyString
        }

        ColumnLayout {
            objectName: "litecoinTradePanel"
            Layout.fillWidth: true
            spacing: page.spacing
            visible: root.baseCoin === root.swapCoin
            enabled: visible

            M.LightDexFundingPanel {
                objectName: "lightDexFundingPanel"
                Layout.fillWidth: true
                coin: root.swapCoin
                visible: root.lightDexFunding.configured === true || root.litecoinNegotiation.role === "litecoin-funder"
                backend: root.dex
                funding: root.lightDexFunding
                formatAmount: root.formatLtcAtomic
                parseAmount: root.parseLtcAtomic
            }
            M.StandardButton {
                objectName: "litecoinReservationPrepare"
                visible: root.baseCoin === root.swapCoin && root.lightDexFunding.configured !== true && root.litecoinReservation.configured === true && root.litecoinReservation.role !== "native-funder"
                enabled: visible && root.litecoinReservation.canPrepare === true
                text: root.chainText(qsTr("Reserve Litecoin inputs and exchange recovery")) + translationManager.emptyString
                onClicked: root.dex.prepareLitecoinReservation()
            }
            M.TextPlain {
                objectName: "litecoinReservationStatus"
                Layout.fillWidth: true
                visible: root.baseCoin === root.swapCoin && root.lightDexFunding.configured !== true && root.litecoinReservation.configured === true && root.litecoinReservation.role !== "native-funder"
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: {
                    var reservation = root.litecoinReservation
                    if (!visible) return ""
                    if (reservation.state === "unknown") return qsTr("Reservation outcome unknown. Reconnect using the saved trade and restore the same reservation; do not create a replacement.")
                    if (reservation.state === "preparing") return root.chainText(qsTr("Reserving Litecoin inputs without signing or broadcasting…"))
                    if (reservation.state === "reserved") return root.chainText(qsTr("Saved Litecoin reservation: ")) + reservation.review.amountAtomsDisplay + " " + root.swapCoin + ""
                        + "\n" + qsTr("Network fee: ") + reservation.review.feeAtomsDisplay + " " + root.swapCoin + ""
                        + qsTr("; fee limit: ") + reservation.review.maximumFeeAtomsDisplay + " " + root.swapCoin + ""
                        + "\n" + qsTr("Wallet: ") + reservation.review.wallet
                        + "\n" + qsTr("Lock transaction: ") + reservation.review.txid
                        + "\n" + qsTr("Inputs were reserved without signing or broadcasting funding. Recovery exchange status appears below.")
                    return root.chainText(qsTr("After private key exchange, reserve inputs for the agreed Litecoin amount and start recovery exchange. This does not send funds."))
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.baseCoin === root.swapCoin && root.lightDexFunding.configured !== true && root.litecoinReservation.configured === true && root.litecoinReservation.role !== "native-funder"
                M.StandardButton {
                    objectName: "litecoinLockFundingReviewCheck"
                    text: root.chainText(qsTr("Review saved Litecoin funding"))
                    enabled: root.litecoinReservation.fundingReviewAvailable === true
                    onClicked: root.dex.reviewLitecoinLockFunding()
                }
                M.TextPlain {
                    objectName: "litecoinLockFundingReview"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                    color: M.Style.defaultFontColor
                    property var review: root.litecoinReservation.fundingReview
                    visible: !!review && !!review.details && !!review.amounts
                    text: visible ? (review.details.action === "retransmit" ? root.chainText(qsTr("Review retry of the saved Litecoin transaction")) : root.chainText(qsTr("Review Litecoin lock funding")))
                        + "\n" + qsTr("Lock amount: ") + review.amounts.amountAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Network fee: ") + review.amounts.feeAtoms + root.chainText(qsTr(" LTC; fee limit: ")) + review.amounts.maximumFeeAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Total debit: ") + review.amounts.totalDebitAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Wallet: ") + review.details.wallet + qsTr("; inputs: ") + review.details.inputCount
                        + "\n" + qsTr("Contract: ") + review.details.contract
                        + "\n" + qsTr("Change: ") + review.amounts.changeAtoms + " " + root.swapCoin + " → " + review.details.change
                        + "\n" + qsTr("Refund destination: ") + review.details.refundAddress
                        + "\n" + qsTr("Transaction: ") + review.details.txid
                        + "\n" + qsTr("Quote expiry: ") + review.expiresAtUtc
                        + "\n" + qsTr("Regtest only. This review has not checked current chain state.") : ""
                }
                M.CheckBox {
                    id: ltcLockConsent
                    objectName: "litecoinLockFundingConsent"
                    visible: root.litecoinReservation.fundingSubmissionAvailable === true
                    text: root.chainText(qsTr("I approve sending this exact Litecoin transaction."))
                }
                M.StandardButton {
                    objectName: "litecoinLockFundingSubmit"
                    text: root.chainText(qsTr("Submit reviewed Litecoin funding"))
                    enabled: root.litecoinReservation.fundingSubmissionAvailable === true && ltcLockConsent.checked
                    onClicked: {
                        var commitment = root.litecoinReservation.fundingReview.commitment
                        ltcLockConsent.checked = false
                        root.dex.submitLitecoinLockFunding(commitment)
                    }
                }
                M.TextPlain {
                    objectName: "litecoinLockFundingOutcome"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                    property string state: root.litecoinReservation.fundingState || ""
                    text: state === "submitted" ? root.chainText(qsTr("The node acknowledged the saved Litecoin transaction. Confirmation is still required."))
                        : state === "observed" ? qsTr("Saved transaction checked. Node status: ") + root.litecoinReservation.fundingSubmission.observationState
                        : state === "unknown" || state === "uncertain" || state === "reconcile" ? qsTr("Funding needs reconciliation. Review the saved transaction before any retry; do not create a replacement.")
                        : state === "reviewing" ? qsTr("Checking the saved funding and recovery agreement…")
                        : state === "submitting" ? root.chainText(qsTr("Submitting the exact approved Litecoin transaction…")) : ""
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.baseCoin === root.swapCoin && root.litecoinReservation.configured === true
                RowLayout {
                    M.StandardButton {
                        objectName: "litecoinCancelReview"
                        text: root.chainText(qsTr("Review Litecoin cancellation"))
                        enabled: root.litecoinReservation.recoveryReviewAvailable === true
                        onClicked: root.dex.reviewLitecoinRecoverySpend("cancel")
                    }
                    M.StandardButton {
                        objectName: "litecoinPunishReview"
                        visible: root.litecoinReservation.role === "native-funder"
                        text: root.chainText(qsTr("Review Litecoin punishment"))
                        enabled: root.litecoinReservation.recoveryReviewAvailable === true
                        onClicked: root.dex.reviewLitecoinRecoverySpend("punish")
                    }
                    M.StandardButton {
                        objectName: "litecoinRefundSpendReview"
                        visible: root.litecoinReservation.role !== "native-funder"
                        text: root.chainText(qsTr("Review Litecoin refund"))
                        enabled: root.litecoinReservation.recoveryReviewAvailable === true
                        onClicked: root.dex.reviewLitecoinRecoverySpend("refund")
                    }
                }
                M.TextPlain {
                    objectName: "litecoinRecoverySpendTerms"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    property var review: root.litecoinReservation.recoveryReview
                    visible: !!review && !!review.details && !!review.amounts
                    text: visible ? (review.details.operation === "cancel" ? root.chainText(qsTr("Cancellation moves Litecoin into the agreed recovery contract. It does not return funds to your wallet yet."))
                            : review.details.operation === "punish" ? root.chainText(qsTr("Punishment pays Litecoin to the signed payout destination after the agreed delay following cancellation."))
                            : root.chainText(qsTr("Refund returns Litecoin to the signed refund destination.")))
                        + "\n" + qsTr("Action: ") + (review.details.action === "send" ? qsTr("first submission") : qsTr("retry of the exact saved transaction"))
                        + "\n" + qsTr("Amount: ") + review.amounts.amountAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Network fee: ") + review.amounts.feeAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Destination: ") + review.details.destination
                        + "\n" + qsTr("Transaction: ") + review.details.txid
                        + "\n" + qsTr("The node checks the saved transaction’s timelock before sending. This review has not checked current chain state.") : ""
                }
                M.CheckBox {
                    id: ltcRecoveryConsent
                    objectName: "litecoinRecoverySpendConsent"
                    visible: root.litecoinReservation.recoverySubmissionAvailable === true
                    text: root.chainText(qsTr("I approve this exact Litecoin recovery transaction and action."))
                }
                M.StandardButton {
                    objectName: "litecoinRecoverySpendSubmit"
                    text: root.chainText(qsTr("Submit reviewed Litecoin recovery"))
                    enabled: root.litecoinReservation.recoverySubmissionAvailable === true && ltcRecoveryConsent.checked
                    onClicked: {
                        var commitment = root.litecoinReservation.recoveryReview.commitment
                        ltcRecoveryConsent.checked = false
                        root.dex.submitLitecoinRecoverySpend(commitment)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    M.StandardButton {
                        objectName: "litecoinCancelCheck"
                        text: qsTr("Check cancellation confirmation")
                        enabled: root.litecoinReservation.recoveryCheckAvailable === true
                        onClicked: root.dex.checkLitecoinRecoverySpend("cancel")
                    }
                    M.StandardButton {
                        objectName: "litecoinPunishCheck"
                        visible: root.litecoinReservation.role === "native-funder"
                        text: qsTr("Check punishment confirmation")
                        enabled: root.litecoinReservation.recoveryCheckAvailable === true
                        onClicked: root.dex.checkLitecoinRecoverySpend("punish")
                    }
                    M.StandardButton {
                        objectName: "litecoinRefundSpendCheck"
                        visible: root.litecoinReservation.role !== "native-funder"
                        text: root.chainText(qsTr("Check Litecoin refund confirmation"))
                        enabled: root.litecoinReservation.recoveryCheckAvailable === true
                        onClicked: root.dex.checkLitecoinRecoverySpend("refund")
                    }
                }
                M.TextPlain {
                    objectName: "litecoinRecoveryConfirmation"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    property var observation: root.litecoinReservation.recoveryObservation
                    property string state: root.litecoinReservation.recoveryCheckState || ""
                    text: state === "confirmed" && observation ? (observation.operation === "cancel"
                        ? root.chainText(qsTr("Cancellation confirmed. This transaction moved Litecoin into the recovery contract; it is not a wallet refund."))
                        : root.chainText(qsTr("Litecoin refund confirmed to the signed destination.")))
                        + "\n" + qsTr("Confirmations at last check: ") + observation.confirmations
                        + " / " + observation.requiredConfirmations
                        + "\n" + qsTr("Transaction: ") + observation.txid
                        + "\n" + qsTr("Block: ") + observation.block
                        + "\n" + qsTr("Checked chain tip: ") + observation.tip
                        + "\n" + qsTr("This is a point-in-time check. Check again after a chain reorganisation; it does not confirm the XMZ payment.")
                        : state === "checking" ? root.chainText(qsTr("Checking current Litecoin recovery confirmation…"))
                        : state === "unknown" ? root.chainText(qsTr("No current verified Litecoin recovery confirmation. The transaction may lack confirmations, the chain may have changed, or the node transaction index may be unavailable. Check again; this does not authorize sending.")) : ""
                }
                M.TextPlain {
                    objectName: "litecoinRecoverySpendOutcome"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                    property string state: root.litecoinReservation.recoveryState || ""
                    text: state === "acknowledged" ? qsTr("The node acknowledged the saved recovery transaction. Confirmation is still required.")
                        : state === "mempool" ? qsTr("The exact saved recovery transaction is already in the node’s mempool. No retry was sent; confirmation is still required.")
                        : state === "unknown" || state === "uncertain" ? qsTr("Recovery submission is unverified. It may be too early or the reply may be missing. Inspect the saved trade and review again before retrying; do not create a replacement.")
                        : state === "reviewing" ? root.chainText(qsTr("Reviewing the saved Litecoin recovery transaction…"))
                        : state === "submitting" ? qsTr("Checking and submitting the approved recovery transaction…") : ""
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.baseCoin === root.swapCoin && root.litecoinNegotiation.settlementConfigured === true
                M.StandardButton {
                    objectName: "litecoinShareFundingProof"
                    text: qsTr("Share confirmed XMZ funding with peer")
                    visible: root.litecoinNegotiation.role === "native-funder"
                    enabled: root.litecoinNegotiation.canShareFunding === true
                    onClicked: root.dex.startLitecoinRedemption("")
                }
                M.TextPlain {
                    objectName: "litecoinRedemptionTerms"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    visible: root.litecoinNegotiation.canReleaseRedemption === true
                    text: root.chainText(qsTr("The peer's XMZ funding proof has been verified locally. Releasing your redemption signature allows the peer to claim the agreed Litecoin payment. Both chains will be checked again before release."))
                        + "\n" + root.chainText(qsTr("Locked Litecoin: ")) + (root.litecoinNegotiation.litecoinLockedAmount || "") + " " + root.swapCoin + ""
                        + "\n" + qsTr("Network fee deducted from this amount: ") + (root.litecoinNegotiation.litecoinRedemptionFee || "") + " " + root.swapCoin + ""
                        + "\n" + root.chainText(qsTr("Litecoin destination: ")) + (root.litecoinNegotiation.litecoinRedemptionDestination || "")
                        + "\n" + qsTr("Trade: ") + (root.litecoinNegotiation.tradeId || "")
                        + "\n" + qsTr("XMZ funding transaction: ") + (root.litecoinNegotiation.nativeFundingTxid || "")
                }
                M.CheckBox {
                    id: ltcRedeemConsent
                    objectName: "litecoinRedemptionConsent"
                    visible: root.litecoinNegotiation.canReleaseRedemption === true
                    text: root.chainText(qsTr("I approve releasing the Litecoin redemption signature for this funded trade."))
                }
                M.StandardButton {
                    objectName: "litecoinReleaseRedemption"
                    visible: root.litecoinNegotiation.role === "litecoin-funder"
                    text: qsTr("Release approved redemption signature")
                    enabled: root.litecoinNegotiation.canReleaseRedemption === true && ltcRedeemConsent.checked
                    onClicked: {
                        var txid = root.litecoinNegotiation.nativeFundingTxid
                        ltcRedeemConsent.checked = false
                        root.dex.startLitecoinRedemption(txid)
                    }
                }
                M.TextPlain {
                    objectName: "litecoinRedemptionSessionStatus"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                    property string state: root.litecoinNegotiation.settlementState || ""
                    text: state === "complete" ? root.chainText(qsTr("Funding proof and redemption message exchanged and acknowledged. No transaction was broadcast by this exchange; Litecoin submission requires separate approval."))
                        : state === "funding-verified" ? qsTr("XMZ funding verified. Waiting for your redemption release approval.")
                        : state === "waiting-for-redemption" ? qsTr("Funding proof acknowledged. Waiting for the peer's approved redemption signature.")
                        : state === "waiting-for-funding-ack" ? qsTr("Funding proof sent privately. Waiting for acknowledgement.")
                        : state === "waiting-for-redemption-ack" ? qsTr("Saved redemption message sent privately. Waiting for acknowledgement.")
                        : state === "unknown" ? qsTr("Redemption exchange is unverified. Reconnect using the same saved trade and inspect its journal; do not create replacement funding.")
                        : state === "configured" ? qsTr("Private redemption exchange configured. Complete funding before sharing its proof.")
                        : qsTr("Verifying and saving the private redemption exchange…")
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.baseCoin === root.swapCoin && root.litecoinRedemption.configured === true
                M.StandardButton {
                    objectName: "litecoinTransactionReview"
                    text: root.chainText(qsTr("Review Litecoin redemption transaction"))
                    enabled: root.litecoinRedemption.reviewAvailable === true
                    onClicked: root.dex.reviewLitecoinRedemptionTransaction()
                }
                M.TextPlain {
                    objectName: "litecoinTransactionTerms"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    property var review: root.litecoinRedemption.review
                    visible: !!review && !!review.details && !!review.amounts
                    text: visible ? root.chainText(qsTr("Claim the agreed Litecoin payment using the retained peer-authorised transaction."))
                        + "\n" + qsTr("Action: ") + (review.details.action === "send" ? qsTr("first submission") : qsTr("retry of the exact saved transaction"))
                        + "\n" + qsTr("Receive: ") + review.amounts.amountAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Network fee deducted from the lock: ") + review.amounts.feeAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Destination: ") + review.details.destination
                        + "\n" + qsTr("Transaction: ") + review.details.txid
                        + "\n" + qsTr("Trade: ") + review.details.tradeId
                        + "\n" + root.chainText(qsTr("This review has not checked current chain state. Submission verifies your XMZ funding and the Litecoin transaction before sending.")) : ""
                }
                M.CheckBox {
                    id: ltcTransactionConsent
                    objectName: "litecoinTransactionConsent"
                    visible: root.litecoinRedemption.submissionAvailable === true
                    text: root.chainText(qsTr("I approve this exact Litecoin redemption transaction and action."))
                }
                M.StandardButton {
                    objectName: "litecoinTransactionSubmit"
                    text: root.chainText(qsTr("Submit reviewed Litecoin redemption"))
                    enabled: root.litecoinRedemption.submissionAvailable === true && ltcTransactionConsent.checked
                    onClicked: {
                        var commitment = root.litecoinRedemption.review.commitment
                        ltcTransactionConsent.checked = false
                        root.dex.submitLitecoinRedemptionTransaction(commitment)
                    }
                }
                M.TextPlain {
                    objectName: "litecoinTransactionStatus"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    property string state: root.litecoinRedemption.state || ""
                    text: state === "acknowledged" ? root.chainText(qsTr("The node acknowledged the Litecoin redemption. Check confirmation separately."))
                        : state === "mempool" ? root.chainText(qsTr("The exact saved Litecoin redemption is in the mempool. It is not yet confirmed."))
                        : state === "uncertain" || state === "unknown" ? qsTr("The redemption result is unknown. Check the saved transaction before reviewing an explicit retry; do not create a replacement trade.")
                        : state === "reviewing" ? root.chainText(qsTr("Reviewing the retained Litecoin redemption…"))
                        : state === "submitting" ? root.chainText(qsTr("Submitting the approved Litecoin redemption…")) : ""
                }
                M.StandardButton {
                    objectName: "litecoinTransactionCheck"
                    text: root.chainText(qsTr("Check Litecoin redemption confirmation"))
                    enabled: root.litecoinRedemption.checkAvailable === true
                    onClicked: root.dex.checkLitecoinRedemptionTransaction()
                }
                M.TextPlain {
                    objectName: "litecoinTransactionConfirmation"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    property var observation: root.litecoinRedemption.observation
                    property string state: root.litecoinRedemption.checkState || ""
                    text: state === "confirmed" && observation ? root.chainText(qsTr("Litecoin redemption confirmed at the checked chain tip."))
                        + "\n" + qsTr("Transaction: ") + observation.txid
                        + "\n" + qsTr("Confirmations: ") + observation.confirmations + " / " + observation.requiredConfirmations
                        + "\n" + qsTr("Block: ") + observation.block + "\n" + qsTr("Checked tip: ") + observation.tip
                        + "\n" + qsTr("This does not confirm the XMZ payout. Check again after a chain reorganisation.")
                        : state === "checking" ? root.chainText(qsTr("Checking current Litecoin redemption confirmation…"))
                        : state === "unknown" ? root.chainText(qsTr("No current verified Litecoin redemption confirmation. The transaction may be unconfirmed or the chain may have changed.")) : ""
                }
            }
            M.TextPlain {
                objectName: "litecoinNegotiationStatus"
                visible: root.baseCoin === root.swapCoin && root.litecoinNegotiation && (root.litecoinNegotiation.attached === true || root.litecoinNegotiation.configured === true)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                text: {
                    var state = root.litecoinNegotiation && root.litecoinNegotiation.state || ""
                    var recovery = root.litecoinNegotiation && root.litecoinNegotiation.recoveryState || ""
                    if (recovery === "unknown") return qsTr("Recovery exchange was interrupted. Reconnect using the saved trade; do not fund a replacement lock.")
                    if (recovery === "complete") return qsTr("Both recovery messages are saved and acknowledged. Funding still requires separate review and approval.")
                    if (recovery === "waiting-for-peer") return qsTr("Recovery message saved and sent. Waiting for the trading partner’s message and acknowledgement.")
                    if (root.litecoinNegotiation && root.litecoinNegotiation.recoveryStarted === true) return qsTr("Preparing, signing and saving the recovery agreement over the private connection…")
                    if (state === "configured") return qsTr("Private peer configured. Connection starts after the reviewed trade is saved.")
                    if (state === "waiting-for-connection") return qsTr("Waiting for the configured trading partner to connect privately.")
                    if (state === "complete") return qsTr("Private key exchange completed. Funding requires a separate review and approval.")
                    if (state === "unknown") return qsTr("Key exchange was interrupted. Reconnect using the saved trade to reconcile delivery.")
                    if (state === "waiting-for-peer") return qsTr("Waiting for the trading partner’s signed key message.")
                    if (state === "waiting-for-acknowledgement") return qsTr("Key message saved and sent. Waiting for the trading partner’s acknowledgement.")
                    return qsTr("Connecting and verifying the private key exchange…")
                }
            }

            M.LitecoinTradeSetupPanel {
                Layout.fillWidth: true
                coin: root.swapCoin
                backend: root.dex
                market: root.baseCoin
                appStatus: root.litecoinApp
            }

            ColumnLayout {
                id: tradeFolderPanel
                objectName: "tradeFolderPanel"
                Layout.fillWidth: true
                property var trades: root.dex.tradeFolders || []
                visible: root.baseCoin === root.swapCoin && trades.length > 0
                M.TextPlain {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                    font.bold: true
                    text: qsTr("Your trades") + translationManager.emptyString
                }
                Repeater {
                    model: tradeFolderPanel.trades
                    M.TextPlain {
                        objectName: "tradeFolder_" + modelData.tradeId
                        Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                        text: (modelData.pair ? (/^btc-/.test(modelData.network || "") ? modelData.pair.replace(/^LTC/, "BTC") : modelData.pair) : qsTr("Unknown pair")) + " · " + (modelData.network || "") + " · " + modelData.tradeId.substring(0, 8)
                            + " · " + (modelData.loaded ? qsTr("open now")
                                : modelData.state === "active" ? qsTr("needs attention (not opened; several active or another network)")
                                : modelData.state === "load-failed" ? qsTr("could not be opened; keep its folder and follow the recovery runbook")
                                : modelData.state === "closed" ? qsTr("closed")
                                : qsTr("unreadable trade record; keep its folder"))
                            + translationManager.emptyString
                    }
                }
                M.CheckBox {
                    id: closeTradeConsent
                    objectName: "closeTradeConsent"
                    visible: !!root.dex.loadedTradeId
                    text: qsTr("All funds for this trade have been received or refunded. Stop reopening it automatically.") + translationManager.emptyString
                }
                M.StandardButton {
                    objectName: "closeTradeButton"
                    visible: !!root.dex.loadedTradeId
                    text: qsTr("Close trade") + translationManager.emptyString
                    enabled: closeTradeConsent.checked
                    onClicked: {
                        root.dex.closeLoadedTrade(closeTradeConsent.checked)
                        closeTradeConsent.checked = false
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.baseCoin === root.swapCoin && root.litecoinJournal && root.litecoinJournal.startConfigured === true
                objectName: "litecoinStartPanel"
                M.TextPlain {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    color: M.Style.defaultFontColor
                    text: root.chainText(qsTr("Create a Litecoin test trade. Review the signed terms before setup. Funding and every payment require separate approval.")) + translationManager.emptyString
                }
                M.TextPlain {
                    objectName: "litecoinStartRefusal"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                    color: M.Style.defaultFontColor
                    visible: !!root.dex.litecoinStartRefusal
                    text: visible ? qsTr("New trade blocked: ") + root.dex.litecoinStartRefusal
                        + qsTr(" Trades already started can still be completed, refunded or recovered.") + translationManager.emptyString : ""
                }
                M.StandardButton {
                    objectName: "litecoinStartReviewCheck"
                    text: qsTr("Review test trade") + translationManager.emptyString
                    enabled: root.litecoinJournal && root.litecoinJournal.startReviewAvailable === true && !root.dex.litecoinStartRefusal
                    onClicked: root.dex.requestLitecoinStartReview()
                }
                M.TextPlain {
                    objectName: "litecoinStartTerms"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                    color: M.Style.defaultFontColor
                    property var review: root.litecoinJournal && root.litecoinJournal.startReview
                    visible: !!review && !!review.details && !!review.amounts
                    text: visible ? qsTr("Your role: ") + (review.details.role === "native-funder" ? root.chainText(qsTr("Pay XMZ, receive LTC")) : root.chainText(qsTr("Pay LTC, receive XMZ")))
                        + "\n" + root.chainText(qsTr("LTC amount: ")) + review.amounts.ltcAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("XMZ lock, including reserve: ") + review.amounts.xmzAtoms + " XMZ"
                        + "\n" + qsTr("XMZ payout: ") + review.amounts.xmzPayoutAtoms + " XMZ"
                        + "\n" + qsTr("XMZ reserve return and refund: ") + review.details.terms.xmzRefund
                        + "\n" + root.chainText(qsTr("LTC payout: ")) + review.details.terms.ltcPayout
                        + "\n" + root.chainText(qsTr("LTC refund: ")) + review.details.terms.ltcRefund
                        + "\n" + qsTr("XMZ payout destination: ") + review.details.terms.xmzPayout
                        + "\n" + qsTr("Funding fee limits: ") + review.amounts.ltcFundingFeeMaxAtoms + " " + root.swapCoin + " / " + review.amounts.xmzNativeFeeMaxAtoms + " XMZ"
                        + "\n" + qsTr("XMZ claim / refund fee limits: ") + review.amounts.xmzClaimFeeMaxAtoms + " / " + review.amounts.xmzRefundFeeMaxAtoms + " XMZ"
                        + "\n" + root.chainText(qsTr("LTC redeem / cancel / refund fees: ")) + review.amounts.ltcRedeemFeeAtoms + " / " + review.amounts.ltcCancelFeeAtoms + " / " + review.amounts.ltcRefundFeeAtoms + " " + root.swapCoin + ""
                        + "\n" + root.chainText(qsTr("LTC early refund / punishment fees: ")) + review.amounts.ltcEarlyRefundFeeAtoms + " / " + review.amounts.ltcPunishFeeAtoms + " " + root.swapCoin + ""
                        + "\n" + qsTr("Application fee: ") + review.amounts.appFeeXmzAtoms + " XMZ"
                        + "\n" + qsTr("Required confirmations: ") + review.details.terms.ltcConfirmations + " " + root.swapCoin + " / " + review.details.terms.xmzConfirmations + " XMZ"
                        + "\n" + qsTr("Cancellation delay: ") + review.details.terms.cancelBlocks + root.chainText(qsTr(" LTC blocks; punishment delay after cancellation: ")) + review.details.terms.punishBlocks + root.chainText(qsTr(" LTC blocks"))
                        + "\n" + qsTr("Counterparty identity: ") + (review.details.role === "native-funder" ? review.details.terms.ltcFunder : review.details.terms.xmzFunder)
                        + "\n" + qsTr("Quote expires: ") + review.expiresAtUtc
                        + "\n" + (/regtest$/.test(review.details.terms.ltcNetwork) ? root.chainText(qsTr("Litecoin regtest only.")) : root.chainText(qsTr("Litecoin test network: test coins only, no real value.")))
                        + qsTr(" Current balances and chain state have not been checked.")
                        + translationManager.emptyString : ""
                }
                M.CheckBox {
                    id: ltcStartConsent
                    objectName: "litecoinStartConsent"
                    visible: root.litecoinJournal && root.litecoinJournal.startSubmissionAvailable === true
                    text: qsTr("I approve this role and these test-trade terms.") + translationManager.emptyString
                }
                M.StandardButton {
                    objectName: "litecoinStartSubmit"
                    text: qsTr("Create reviewed test trade") + translationManager.emptyString
                    enabled: root.litecoinJournal && root.litecoinJournal.startSubmissionAvailable === true && ltcStartConsent.checked && !root.dex.litecoinStartRefusal
                    onClicked: {
                        var commitment = root.litecoinJournal.startReview.commitment
                        ltcStartConsent.checked = false
                        root.dex.submitLitecoinStart(commitment)
                    }
                }
                M.TextPlain {
                    objectName: "litecoinStartOutcome"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                    property string startupState: root.litecoinJournal && root.litecoinJournal.startState || ""
                    text: startupState === "saved" ? qsTr("Trade setup saved. Funding requires separate approval.")
                        : startupState === "unknown" ? qsTr("Startup outcome is unknown. Refresh the saved trade before proceeding; do not create replacement keys.")
                        : startupState === "expired" ? qsTr("The reviewed quote expired. A current signed quote is required.")
                        : startupState === "unverified" ? qsTr("Trade setup could not be verified. Check the configured quote and saved trade.")
                        : startupState === "starting" ? qsTr("Creating the approved test trade…")
                        : startupState === "reviewing" ? qsTr("Verifying the signed test-trade terms…") : ""
                }
            }
            M.TextPlain {
                Layout.fillWidth: true
                visible: root.litecoinJournal && root.litecoinJournal.attached === true
                wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                objectName: "litecoinJournalEvidence"
                text: root.chainText(qsTr("Saved Litecoin/XMZ trade: ")) + (root.litecoinJournal && root.litecoinJournal.valid
                    ? root.litecoinJournal.snapshot.tradeId + " — " + root.litecoinJournal.snapshot.role
                    : qsTr("no validated journal result")) + ". "
                    + qsTr("Saved payments and broadcast acknowledgements are not current chain confirmations.")
                    + translationManager.emptyString
            }
            M.TextPlain {
                objectName: "litecoinJournalTerms"
                Layout.fillWidth: true
                visible: root.litecoinJournal && root.litecoinJournal.valid === true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: root.litecoinJournal && root.litecoinJournal.valid && root.litecoinJournal.amounts
                    ? qsTr("Saved funding terms: ") + root.litecoinJournal.amounts.ltc + " " + root.swapCoin + " / "
                        + root.litecoinJournal.amounts.xmzLock + " XMZ " + qsTr("locked (includes fee reserve)")
                        + "\n" + root.chainText(qsTr("LTC payout: ")) + root.litecoinJournal.snapshot.ltcPayout
                        + "\n" + root.chainText(qsTr("LTC refund: ")) + root.litecoinJournal.snapshot.ltcRefund
                        + "\n" + qsTr("XMZ payout: ") + root.litecoinJournal.snapshot.xmzPayout
                        + "\n" + qsTr("XMZ refund: ") + root.litecoinJournal.snapshot.xmzRefund
                        + "\n" + qsTr("Saved terms are not current balances or payment approval.")
                        + translationManager.emptyString : ""
            }
            M.TextPlain {
                Layout.fillWidth: true
                visible: root.litecoinPayment && root.litecoinPayment.valid === true
                wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                text: root.litecoinPayment && root.litecoinPayment.valid
                    ? qsTr("XMZ refund payment: ") + (root.litecoinPayment.snapshot.nativeRefundPayout.acknowledged
                        ? qsTr("broadcast acknowledged; confirmation unchecked")
                        : root.litecoinPayment.snapshot.nativeRefundPayout.attempted ? qsTr("broadcast attempted; outcome unknown")
                        : root.litecoinPayment.snapshot.nativeRefundPayout.prepared ? qsTr("prepared and saved") : qsTr("not prepared"))
                        + translationManager.emptyString : ""
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.baseCoin === root.swapCoin && root.litecoinApp.configured === true
                M.TextPlain {
                    objectName: "litecoinAppStatus"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
                    text: root.litecoinApp.state === "unknown"
                        ? root.chainText(qsTr("The shared Litecoin setup changed or could not be verified. Reopen the same saved trade with a consistent configuration; no new operation is authorised."))
                        : root.chainText(qsTr("One saved Litecoin trade is configured for startup and settlement. Refresh the funding or recovered-payment state when moving between phases. Every payment still requires separate approval."))
                }
                RowLayout {
                    M.StandardButton {
                        objectName: "litecoinFundingPhaseCheck"
                        visible: root.litecoinApp.fundingConfigured === true
                        enabled: root.litecoinApp.state === "configured" && root.litecoinApp.busy === false
                        text: qsTr("Refresh funding state")
                        onClicked: root.dex.requestLitecoinPhaseCheck("funding")
                    }
                    M.StandardButton {
                        objectName: "litecoinPaymentPhaseCheck"
                        visible: root.litecoinApp.paymentConfigured === true
                        enabled: root.litecoinApp.state === "configured" && root.litecoinApp.busy === false
                        text: qsTr("Refresh recovered-payment state")
                        onClicked: root.dex.requestLitecoinPhaseCheck("payment")
                    }
                }
            }
            M.StandardButton {
                objectName: "litecoinFundingPrepare"
                visible: root.litecoinFunding && root.litecoinFunding.fundingPreparationConfigured === true
                enabled: visible && root.litecoinFunding.fundingPreparationAvailable === true
                text: qsTr("Prepare XMZ funding (no broadcast)") + translationManager.emptyString
                onClicked: root.dex.prepareLitecoinFunding()
            }
            M.TextPlain {
                objectName: "litecoinFundingPreparationStatus"
                Layout.fillWidth: true
                visible: root.litecoinFunding && root.litecoinFunding.fundingPreparationConfigured === true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: visible ? qsTr("Funding preparation: ") + root.litecoinFunding.fundingPreparationState
                    + "\n" + qsTr("Source wallet: ") + root.litecoinFunding.fundingPreparationSource.wallet
                    + "\n" + qsTr("Account: ") + root.litecoinFunding.fundingPreparationSource.account
                    + qsTr("; priority: ") + root.litecoinFunding.fundingPreparationSource.priority
                    + "\n" + root.chainText(qsTr("Requires agreed recovery terms and a confirmed Litecoin lock. Review the saved payment before approving submission. If the outcome is unknown, refresh saved state."))
                    + translationManager.emptyString : ""
            }
            M.StandardButton {
                objectName: "litecoinFundingReviewCheck"
                visible: root.litecoinFunding && root.litecoinFunding.valid === true
                    && root.litecoinFunding.snapshot.role === "native-funder"
                    && !!root.litecoinFunding.snapshot.nativeFunding
                    && root.litecoinFunding.snapshot.nativeFunding.prepared === true
                enabled: visible && root.litecoinFunding.busy === false
                text: qsTr("Inspect saved XMZ funding") + translationManager.emptyString
                onClicked: root.dex.requestLitecoinFundingReview(root.litecoinFunding.snapshot.nativeFunding.attempted === true)
            }
            M.TextPlain {
                objectName: "litecoinFundingReview"
                Layout.fillWidth: true
                visible: root.litecoinFunding && root.litecoinFunding.valid === true
                    && !!root.litecoinFunding.fundingReview && root.litecoinFunding.fundingReview.commitment !== undefined
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: visible ? qsTr("Saved XMZ funding: ") + root.litecoinFunding.fundingReview.amounts.amountAtoms + " XMZ"
                    + "\n" + qsTr("Source wallet: ") + root.litecoinFunding.fundingReview.details.wallet
                    + "\n" + qsTr("Source account: ") + root.litecoinFunding.fundingReview.details.account
                    + "\n" + qsTr("Shared XMZ lock: ") + root.litecoinFunding.fundingReview.details.destination
                    + "\n" + qsTr("Network fee: ") + root.litecoinFunding.fundingReview.amounts.feeAtoms + " XMZ"
                    + "\n" + qsTr("Signed fee limit: ") + root.litecoinFunding.fundingReview.amounts.maximumFeeAtoms + " XMZ"
                    + "\n" + qsTr("Total wallet debit: ") + root.litecoinFunding.fundingReview.amounts.totalDebitAtoms + " XMZ"
                    + "\n" + root.chainText(qsTr("Litecoin lock: ")) + root.litecoinFunding.fundingReview.details.ltcContract
                    + "\n" + qsTr("Required safety window: ") + root.litecoinFunding.fundingReview.details.safetyBlocks + root.chainText(qsTr(" Litecoin blocks"))
                    + "\n" + qsTr("Recorded quote expiry (UTC): ") + root.litecoinFunding.fundingReview.expiresAtUtc
                    + "\n" + (root.litecoinFunding.fundingReview.details.action === "send"
                        ? qsTr("Action reviewed: first submission") : qsTr("Action reviewed: retransmission of the same payment"))
                    + "\n" + qsTr("Inspection only. No payment was sent; current chain confirmation has not been checked.")
                    + translationManager.emptyString : ""
            }
            M.CheckBox {
                id: ltcFundingConsent
                objectName: "litecoinFundingConsent"
                visible: root.litecoinFunding && root.litecoinFunding.fundingSubmissionAvailable === true
                text: qsTr("I reviewed the source wallet, lock destination, total debit, safety window and submission action.") + translationManager.emptyString
            }
            M.StandardButton {
                objectName: "litecoinFundingSubmit"
                visible: root.litecoinFunding && root.litecoinFunding.fundingSubmissionConfigured === true
                enabled: visible && root.litecoinFunding.fundingSubmissionAvailable === true && ltcFundingConsent.checked
                text: root.litecoinFunding && root.litecoinFunding.fundingReview && root.litecoinFunding.fundingReview.details
                    && root.litecoinFunding.fundingReview.details.action === "retransmit"
                    ? qsTr("Retransmit same test funding") : qsTr("Submit reviewed test funding")
                onClicked: {
                    var commitment = root.litecoinFunding.fundingReview.commitment
                    ltcFundingConsent.checked = false
                    root.dex.submitLitecoinFunding(commitment)
                }
            }
            M.TextPlain {
                objectName: "litecoinFundingSubmissionOutcome"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.litecoinFunding && !!root.litecoinFunding.fundingSubmission
                    && root.litecoinFunding.fundingSubmission.state !== undefined
                color: M.Style.defaultFontColor
                text: !visible ? "" : root.litecoinFunding.fundingSubmission.state === "acknowledged"
                    ? qsTr("Funding broadcast acknowledged. Current chain confirmation is still required.")
                    : root.litecoinFunding.fundingSubmission.state === "submitting" ? qsTr("Submitting the reviewed funding…")
                    : root.litecoinFunding.fundingSubmission.state === "not-started" ? qsTr("Funding submission did not start; reload and review the saved trade.")
                    : qsTr("Funding submission outcome unknown. Refresh the journal and reconcile the saved transaction before considering an explicit retry.")
            }
            M.StandardButton {
                objectName: "litecoinFundingConfirmationCheck"
                visible: root.litecoinFunding && root.litecoinFunding.fundingSubmissionConfigured === true
                enabled: visible && root.litecoinFunding.fundingCheckAvailable === true
                text: qsTr("Check XMZ funding confirmation") + translationManager.emptyString
                onClicked: root.dex.checkLitecoinFunding()
            }
            M.TextPlain {
                objectName: "litecoinFundingConfirmation"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                visible: root.litecoinFunding && root.litecoinFunding.fundingSubmissionConfigured === true
                color: M.Style.defaultFontColor
                text: !visible ? "" : root.litecoinFunding.fundingCheckState === "confirmed"
                    ? qsTr("At last check: ") + root.litecoinFunding.fundingConfirmation.fundingAmount + " XMZ, "
                        + root.litecoinFunding.fundingConfirmation.confirmations + qsTr(" native confirmations")
                        + "\n" + qsTr("Funding transaction: ") + root.litecoinFunding.fundingConfirmation.txid
                        + "\n" + qsTr("Lock destination: ") + root.litecoinFunding.fundingConfirmation.destination
                        + "\n" + root.litecoinFunding.fundingConfirmation.checkedAt
                        + "\n" + root.chainText(qsTr("This is a past observation of XMZ funding. It does not confirm the current Litecoin lock or completed settlement."))
                    : root.litecoinFunding.fundingCheckState === "checking" ? qsTr("Checking the saved XMZ funding transaction…")
                    : root.litecoinFunding.fundingCheckState === "unverified" ? qsTr("Funding confirmation unverified; no payment was sent by this check.")
                    : qsTr("Funding confirmation has not been checked in this session.")
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.litecoinPayment.walletRecoveryConfigured === true
                RowLayout {
                    M.StandardButton {
                        objectName: "litecoinWalletRestore"
                        text: qsTr("Restore recovered XMZ wallet")
                        enabled: root.litecoinPayment.walletRestoreAvailable === true
                        onClicked: root.dex.restoreLitecoinRecoveredWallet()
                    }
                    M.StandardButton {
                        objectName: "litecoinWalletOpen"
                        text: qsTr("Reopen saved XMZ wallet")
                        enabled: root.litecoinPayment.walletOpenAvailable === true
                        onClicked: root.dex.openLitecoinRecoveredWallet()
                    }
                    M.StandardButton {
                        objectName: "litecoinWalletCheck"
                        text: qsTr("Check open XMZ wallet")
                        enabled: root.litecoinPayment.walletCheckAvailable === true
                        onClicked: root.dex.checkLitecoinRecoveredWallet()
                    }
                }
                M.TextPlain {
                    objectName: "litecoinWalletRecoveryStatus"
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                    color: M.Style.defaultFontColor
                    text: root.litecoinPayment.walletRecoveryState === "verified" ? qsTr("Recovered wallet verified and funding transaction imported: ")
                            + root.litecoinPayment.recoveredWallet.address + "\n" + qsTr("No payment was prepared or sent. Payout/refund preparation requires a separate action.")
                        : root.litecoinPayment.walletRecoveryState === "unknown" ? qsTr("Wallet outcome unknown. Check the open wallet, or reopen the saved wallet after restart. Do not create a replacement.")
                        : root.litecoinPayment.walletRecoveryState === "restoring" ? qsTr("Verifying chain evidence and restoring the trade’s recovered wallet…")
                        : root.litecoinPayment.walletRecoveryState === "opening" ? qsTr("Reopening and verifying the saved recovered wallet…")
                        : root.litecoinPayment.walletRecoveryState === "checking" ? qsTr("Checking the open wallet’s recovered keys and funding transaction…")
                        : root.chainText(qsTr("Restore once after confirmed Litecoin redemption or refund. After restart, reopen the saved wallet. These actions do not send funds."))
                }
            }
            RowLayout {
                visible: root.litecoinPayment.paymentPreparationConfigured === true
                M.StandardButton {
                    objectName: "litecoinPayoutPrepare"
                    text: qsTr("Prepare XMZ payout without sending")
                    enabled: root.litecoinPayment.payoutPreparationAvailable === true
                    onClicked: root.dex.prepareLitecoinPayout()
                }
                M.StandardButton {
                    objectName: "litecoinRefundPrepare"
                    text: qsTr("Prepare XMZ refund without sending")
                    enabled: root.litecoinPayment.refundPreparationAvailable === true
                    onClicked: root.dex.prepareLitecoinRefund()
                }
            }
            M.TextPlain {
                objectName: "litecoinPaymentPreparationStatus"
                visible: root.litecoinPayment.paymentPreparationConfigured === true
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor
                text: root.litecoinPayment.paymentPreparationState === "preparing" ? qsTr("Checking both chains and preparing the payment…")
                    : root.litecoinPayment.paymentPreparationState === "prepared" ? qsTr("Payment saved without broadcasting. Review the amounts and destinations below before approving submission.")
                    : root.litecoinPayment.paymentPreparationState === "unknown" ? qsTr("Preparation outcome unknown. Refresh the saved trade and inspect any retained payment before proceeding.")
                    : qsTr("Preparation requires the recovered wallet. It saves a transaction; sending requires separate approval.")
            }
            M.StandardButton {
                objectName: "litecoinPayoutReviewCheck"
                visible: root.litecoinPayment && root.litecoinPayment.valid === true
                    && root.litecoinPayment.snapshot.role === "litecoin-funder"
                    && root.litecoinPayment.snapshot.nativePayout.prepared === true
                enabled: visible && root.litecoinPayment.busy === false
                text: qsTr("Inspect saved XMZ payout") + translationManager.emptyString
                onClicked: root.dex.requestLitecoinPayoutReview(root.litecoinPayment.snapshot.nativePayout.attempted === true)
            }
            M.TextPlain {
                objectName: "litecoinPayoutReview"
                Layout.fillWidth: true
                visible: root.litecoinPayment && root.litecoinPayment.valid === true
                    && !!root.litecoinPayment.payoutReview && root.litecoinPayment.payoutReview.commitment !== undefined
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: visible ? qsTr("Saved XMZ payout: ") + root.litecoinPayment.payoutReview.amounts.payoutAtoms + " XMZ"
                    + "\n" + qsTr("Destination: ") + root.litecoinPayment.payoutReview.details.destination
                    + "\n" + qsTr("Unused reserve: ") + root.litecoinPayment.payoutReview.amounts.reserveReturnAtoms + " XMZ"
                    + "\n" + qsTr("Reserve destination: ") + root.litecoinPayment.payoutReview.details.reserveDestination
                    + "\n" + qsTr("Network fee: ") + root.litecoinPayment.payoutReview.amounts.feeAtoms + " XMZ"
                    + "\n" + qsTr("Signed fee limit: ") + root.litecoinPayment.payoutReview.amounts.maximumFeeAtoms + " XMZ"
                    + "\n" + (root.litecoinPayment.payoutReview.details.action === "send"
                        ? qsTr("Action reviewed: first submission") : qsTr("Action reviewed: retransmission of the same payment"))
                    + "\n" + qsTr("Inspection only. No payment was sent; current chain confirmation has not been checked.")
                    + translationManager.emptyString : ""
            }
            M.StandardButton {
                objectName: "litecoinRefundReviewCheck"
                visible: root.litecoinPayment && root.litecoinPayment.valid === true
                    && root.litecoinPayment.snapshot.role === "native-funder"
                    && root.litecoinPayment.snapshot.nativeRefundPayout.prepared === true
                enabled: visible && root.litecoinPayment.busy === false
                text: qsTr("Inspect saved XMZ refund") + translationManager.emptyString
                onClicked: root.dex.requestLitecoinRefundReview(root.litecoinPayment.snapshot.nativeRefundPayout.attempted === true)
            }
            M.TextPlain {
                objectName: "litecoinRefundReview"
                Layout.fillWidth: true
                visible: root.litecoinPayment && root.litecoinPayment.valid === true
                    && !!root.litecoinPayment.refundReview && root.litecoinPayment.refundReview.commitment !== undefined
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: M.Style.defaultFontColor
                text: visible ? qsTr("Saved XMZ refund: ") + root.litecoinPayment.refundReview.amounts.refundAtoms + " XMZ"
                    + "\n" + qsTr("Destination: ") + root.litecoinPayment.refundReview.details.destination
                    + "\n" + qsTr("Network fee: ") + root.litecoinPayment.refundReview.amounts.feeAtoms + " XMZ"
                    + "\n" + qsTr("Signed fee limit: ") + root.litecoinPayment.refundReview.amounts.maximumFeeAtoms + " XMZ"
                    + "\n" + (root.litecoinPayment.refundReview.details.action === "send"
                        ? qsTr("Action reviewed: first submission") : qsTr("Action reviewed: retransmission of the same payment"))
                    + "\n" + qsTr("Inspection only. No payment was sent; current chain confirmation has not been checked.")
                    + translationManager.emptyString : ""
            }
            M.CheckBox {
                id: ltcRefundConsent
                objectName: "litecoinRefundConsent"
                visible: root.litecoinPayment && root.litecoinPayment.refundSubmissionAvailable === true
                text: qsTr("I reviewed the refund destination, amount, fee and submission action.") + translationManager.emptyString
            }
            M.StandardButton {
                objectName: "litecoinRefundSubmit"
                visible: root.litecoinPayment && root.litecoinPayment.refundSubmissionConfigured === true
                enabled: visible && root.litecoinPayment.refundSubmissionAvailable === true && ltcRefundConsent.checked
                text: root.litecoinPayment && root.litecoinPayment.refundReview && root.litecoinPayment.refundReview.details
                    && root.litecoinPayment.refundReview.details.action === "retransmit"
                    ? qsTr("Retransmit same test refund") : qsTr("Submit reviewed test refund")
                onClicked: {
                    var commitment = root.litecoinPayment.refundReview.commitment
                    ltcRefundConsent.checked = false; ltcPayoutConsent.checked = false
                    root.dex.submitLitecoinRefund(commitment)
                }
            }
            M.TextPlain {
                objectName: "litecoinRefundSubmissionOutcome"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.litecoinPayment && !!root.litecoinPayment.refundSubmission
                    && root.litecoinPayment.refundSubmission.state !== undefined
                color: M.Style.defaultFontColor
                text: !visible ? "" : root.litecoinPayment.refundSubmission.state === "acknowledged"
                    ? qsTr("Refund broadcast acknowledged. Current chain confirmation is still required.")
                    : root.litecoinPayment.refundSubmission.state === "submitting" ? qsTr("Submitting the reviewed refund…")
                    : root.litecoinPayment.refundSubmission.state === "not-started" ? qsTr("Refund submission did not start; reload and review the saved trade.")
                    : qsTr("Refund submission outcome unknown. Refresh the journal and reconcile the saved transaction before considering an explicit retry.")
            }
            M.StandardButton {
                objectName: "litecoinRefundConfirmationCheck"
                visible: root.litecoinPayment && root.litecoinPayment.refundSubmissionConfigured === true
                enabled: visible && root.litecoinPayment.refundCheckAvailable === true
                text: qsTr("Check refund confirmation") + translationManager.emptyString
                onClicked: root.dex.checkLitecoinRefund()
            }
            M.TextPlain {
                objectName: "litecoinRefundConfirmation"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                visible: root.litecoinPayment && root.litecoinPayment.refundSubmissionConfigured === true
                color: M.Style.defaultFontColor
                text: !visible ? "" : root.litecoinPayment.refundCheckState === "confirmed"
                    ? qsTr("At last check, the saved refund had ") + root.litecoinPayment.refundConfirmation.confirmations
                        + root.chainText(qsTr(" XMZ confirmations and its Litecoin refund was confirmed."))
                        + "\n" + root.litecoinPayment.refundConfirmation.refundAmount + " XMZ → "
                        + root.litecoinPayment.refundConfirmation.destination
                        + "\n" + qsTr("Transaction: ") + root.litecoinPayment.refundConfirmation.txid
                        + "\n" + qsTr("Check completed (UTC): ") + root.litecoinPayment.refundConfirmation.checkedAt
                        + "\n" + qsTr("This is a past observation. Refresh to check for later chain changes.")
                    : root.litecoinPayment.refundCheckState === "checking" ? qsTr("Checking the saved refund on both chains…")
                    : root.litecoinPayment.refundCheckState === "unverified"
                        ? qsTr("The latest check did not verify the refund. Earlier confirmation is no longer shown; no payment was sent by this check.")
                        : qsTr("No current refund confirmation check. Saved acknowledgements alone are not confirmations.")
            }
            M.CheckBox {
                id: ltcPayoutConsent
                objectName: "litecoinPayoutConsent"
                visible: root.litecoinPayment && root.litecoinPayment.payoutSubmissionAvailable === true
                text: qsTr("I reviewed the principal and reserve destinations, amounts, fee and submission action.") + translationManager.emptyString
            }
            M.StandardButton {
                objectName: "litecoinPayoutSubmit"
                visible: root.litecoinPayment && root.litecoinPayment.payoutSubmissionConfigured === true
                enabled: visible && root.litecoinPayment.payoutSubmissionAvailable === true && ltcPayoutConsent.checked
                text: root.litecoinPayment && root.litecoinPayment.payoutReview && root.litecoinPayment.payoutReview.details
                    && root.litecoinPayment.payoutReview.details.action === "retransmit"
                    ? qsTr("Retransmit same test payout") : qsTr("Submit reviewed test payout")
                onClicked: {
                    var commitment = root.litecoinPayment.payoutReview.commitment
                    ltcPayoutConsent.checked = false
                    root.dex.submitLitecoinPayout(commitment)
                }
            }
            M.TextPlain {
                objectName: "litecoinPayoutSubmissionOutcome"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.litecoinPayment && !!root.litecoinPayment.payoutSubmission
                    && root.litecoinPayment.payoutSubmission.state !== undefined
                color: M.Style.defaultFontColor
                text: !visible ? "" : root.litecoinPayment.payoutSubmission.state === "acknowledged"
                    ? qsTr("Payout broadcast acknowledged. Current chain confirmation is still required.")
                    : root.litecoinPayment.payoutSubmission.state === "submitting" ? qsTr("Submitting the reviewed payout…")
                    : root.litecoinPayment.payoutSubmission.state === "not-started" ? qsTr("Payout submission did not start; reload and review the saved trade.")
                    : qsTr("Payout submission outcome unknown. Refresh the journal and reconcile the saved transaction before considering an explicit retry.")
            }
            M.StandardButton {
                objectName: "litecoinPayoutConfirmationCheck"
                visible: root.litecoinPayment && root.litecoinPayment.payoutSubmissionConfigured === true
                enabled: visible && root.litecoinPayment.payoutCheckAvailable === true
                text: qsTr("Check payout confirmation") + translationManager.emptyString
                onClicked: root.dex.checkLitecoinPayout()
            }
            M.TextPlain {
                objectName: "litecoinPayoutConfirmation"
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                visible: root.litecoinPayment && root.litecoinPayment.payoutSubmissionConfigured === true
                color: M.Style.defaultFontColor
                text: !visible ? "" : root.litecoinPayment.payoutCheckState === "confirmed"
                    ? qsTr("At last check, the saved payout had ") + root.litecoinPayment.payoutConfirmation.confirmations
                        + root.chainText(qsTr(" XMZ confirmations and its Litecoin redemption was confirmed."))
                        + "\n" + root.litecoinPayment.payoutConfirmation.payoutAmount + " XMZ → "
                        + root.litecoinPayment.payoutConfirmation.destination
                        + "\n" + qsTr("Unused reserve: ") + root.litecoinPayment.payoutConfirmation.reserveAmount + " XMZ → "
                        + root.litecoinPayment.payoutConfirmation.reserveDestination
                        + "\n" + qsTr("Transaction: ") + root.litecoinPayment.payoutConfirmation.txid
                        + "\n" + qsTr("Check completed (UTC): ") + root.litecoinPayment.payoutConfirmation.checkedAt
                        + "\n" + qsTr("This is a past observation. Refresh to check for later chain changes.")
                    : root.litecoinPayment.payoutCheckState === "checking" ? qsTr("Checking the saved payout on both chains…")
                    : root.litecoinPayment.payoutCheckState === "unverified"
                        ? qsTr("The latest check did not verify the payout. Earlier confirmation is no longer shown; no payment was sent by this check.")
                        : qsTr("No current payout confirmation check. Saved acknowledgements alone are not confirmations.")
            }
            M.StandardButton {
                objectName: "litecoinJournalCheck"
                visible: root.litecoinJournal && root.litecoinJournal.attached === true
                enabled: root.litecoinJournal && root.litecoinJournal.busy === false
                text: root.chainText(qsTr("Refresh saved Litecoin trade")) + translationManager.emptyString
                onClicked: root.dex.requestLitecoinJournalCheck()
            }
        }

        M.StandardButton {
            objectName: "nativeRecoveryCheck"
            visible: root.walletBackend.attached
            enabled: root.nativeRecovery.checkAvailable === true
            text: qsTr("Check recorded payment (read-only)") + translationManager.emptyString
            onClicked: root.dex.requestNativeRecoveryCheck()
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.walletBackend.attached && root.walletBackend.state === "busy"
            M.TextPlain {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                text: qsTr("Cancelling does not undo RPC work. Recovery checks may be required before another operation.") + translationManager.emptyString
            }
            M.StandardButton {
                text: qsTr("Cancel pending wallet operation") + translationManager.emptyString
                enabled: root.walletBackend.attached && root.walletBackend.state === "busy"
                onClicked: root.dex.cancelNativeWalletOperation()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.walletBackend.attached
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                text: qsTr("XMZ network-fee preview only — no broadcast, no trade and no app protocol fee. A final transaction requires a new check. Invalid or interrupted requests may require recovery checks.") + translationManager.emptyString
            }
            M.LineEdit {
                id: nativeFeeDestination
                Layout.fillWidth: true
                labelText: qsTr("XMZ destination address")
                enabled: root.walletBackend.state === "open"
            }
            M.LineEdit {
                id: nativeFeeAmount
                Layout.fillWidth: true
                labelText: qsTr("Amount (XMZ)")
                enabled: root.walletBackend.state === "open"
            }
            M.StandardButton {
                text: qsTr("Preview network fee without broadcasting") + translationManager.emptyString
                enabled: root.walletBackend.state === "open"
                onClicked: root.nativePreviewError = root.dex.requestNativeFeePreview(nativeFeeDestination.text, nativeFeeAmount.text)
                    ? "" : qsTr("Preview was not started. Check the address, amount and wallet status.")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: M.Style.defaultFontColor
                text: root.nativePreview.valid
                    ? qsTr("Estimated network fee: ") + root.nativePreview.feeXmz + " XMZ\n"
                        + qsTr("For amount: ") + root.nativePreview.amountXmz + " XMZ\n"
                        + qsTr("Destination: ") + root.nativePreview.destination
                    : root.nativePreviewError || qsTr("No current verified fee preview.")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            M.TextPlain { color:M.Style.defaultFontColor; opacity:.65; text:qsTr("Markets") + translationManager.emptyString }
            Item { Layout.fillWidth:true }
            M.TextPlain { color:M.Style.defaultFontColor; opacity:.55; text:qsTr("Research only · settlement disabled on every market") + translationManager.emptyString }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.supportedPairs
                delegate: M.StandardButton { text:modelData; primary:root.baseCoin === root.baseOf(modelData); onClicked:root.selectPair(modelData) }
            }
        }
        M.TextPlain {
            objectName: "dexSettlementBlocker"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            color: "#ef6575"
            text: (root.marketInfo.settlementBlocker ? String(root.marketInfo.settlementBlocker) : qsTr("Settlement unavailable"))
                + translationManager.emptyString
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 76; radius: 10
            color: M.Style.titleBarButtonHoverColor; border.width: 1; border.color: M.Style.inputBorderColorInActive
            RowLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 30
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("Market") } M.TextPlain { color:M.Style.defaultFontColor; font.pixelSize:19; font.bold:true; text:root.pairName } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("Reference price") } M.TextPlain { color:"#49c991"; font.pixelSize:19; font.bold:true; text:root.hasMarketData ? root.lastPrice.toFixed(5)+" XMZ" : qsTr("No data") } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("24h change") } M.TextPlain { color:M.Style.defaultFontColor; text:qsTr("Unavailable") } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("24h high / low") } M.TextPlain { color:M.Style.defaultFontColor; text:qsTr("Unavailable") } }
                ColumnLayout { M.TextPlain { color:M.Style.defaultFontColor; opacity:.6; text:qsTr("Active offers") } M.TextPlain { color:M.Style.defaultFontColor; text:root.marketSnapshot.valid ? String(root.marketSnapshot.activeCount) : "0" } }
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
                    RowLayout { Layout.fillWidth:true; M.TextPlain { color:M.Style.defaultFontColor; font.bold:true; text:root.pairName } Item { Layout.fillWidth:true } M.TextPlain { color:M.Style.defaultFontColor; opacity:.5; text:qsTr("Illustrative trend · not price-scaled") } }
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
                    M.TextPlain { color:M.Style.defaultFontColor; font.bold:true; text:root.hasMarketData ? qsTr("Verified order book") : qsTr("Order book (no offers loaded)") }
                    RowLayout { Layout.fillWidth:true; M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;opacity:.5;text:qsTr("Price (XMZ)") } M.TextPlain { color:M.Style.defaultFontColor;opacity:.5;text:qsTr("Amount (")+root.baseCoin+")" } }
                    Repeater { model:asks; delegate:RowLayout { Layout.fillWidth:true; height:22; M.TextPlain { Layout.fillWidth:true;color:"#ef6575";text:p } M.TextPlain { color:M.Style.defaultFontColor;text:a } } }
                    M.TextPlain { Layout.fillWidth:true;color:"#49c991";font.pixelSize:18;font.bold:true;text:root.hasMarketData ? root.lastPrice.toFixed(5) : qsTr("No data") }
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
                    M.LineEdit { id:priceInput;Layout.fillWidth:true;labelText:qsTr("Price (XMZ)");text:root.limitOrder?(root.hasMarketData?root.lastPrice.toFixed(5):""):qsTr("Market price");placeholderText:root.limitOrder?qsTr("No market data — enter price manually"):"";readOnly:!root.limitOrder }
                    M.LineEdit { id:amountInput;Layout.fillWidth:true;labelText:qsTr("Amount (")+root.baseCoin+")";placeholderText:"0.01" }
                    M.LineEdit { Layout.fillWidth:true;labelText:qsTr("Order total (XMZ)");text:root.fixed(root.totalValue,5);readOnly:true }
                    M.LineEdit { Layout.fillWidth:true;labelText:qsTr("Protocol fee (XMZ)");text:root.fixed(root.tradingFeeValue,5);readOnly:true }
                    Rectangle {
                        Layout.columnSpan:2;Layout.fillWidth:true;Layout.preferredHeight:82;radius:7;color:M.Style.middlePanelBackgroundColor
                        ColumnLayout { anchors.fill:parent;anchors.margins:10;spacing:4
                            RowLayout { Layout.fillWidth:true;M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;text:qsTr("Monzero network fee") } M.TextPlain { color:"#d5a6ff";font.bold:true;text:root.networkFeeXMZ.toFixed(4)+" XMZ" } }
                            RowLayout { Layout.fillWidth:true;M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;text:root.externalFeeLabel } M.TextPlain { color:M.Style.defaultFontColor;font.bold:true;text:qsTr("Not estimated") } }
                            M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;opacity:.55;font.pixelSize:12;wrapMode:Text.WordWrap;text:qsTr("Initial release plan: no app trading fee. Native chain fees still apply; the XMZ amount is a demo estimate and external-chain fees are not estimated. Settlement remains unavailable.") }
                        }
                    }
                    M.TextPlain { Layout.columnSpan:2;Layout.fillWidth:true;color:M.Style.defaultFontColor;opacity:.62;text:qsTr("Demo balances only · XMZ pays Monzero settlement fees; the external coin pays its own chain network fee") }
                    M.TextPlain { Layout.columnSpan:2;Layout.fillWidth:true;visible:root.previewError!=="";color:"#ef6575";text:root.previewError }
                    M.StandardButton { Layout.fillWidth:true;text:qsTr("Preview buy ")+root.baseCoin;enabled:root.totalValue>0;onClicked:root.preview("BUY") }
                    M.StandardButton { Layout.fillWidth:true;text:qsTr("Preview sell ")+root.baseCoin;enabled:root.totalValue>0;onClicked:root.preview("SELL") }
                    M.StandardButton { Layout.columnSpan:2;Layout.fillWidth:true;text:qsTr("Place live order");enabled:root.readiness.ready && root.marketInfo.executable === true;tooltip:enabled?qsTr("Readiness checks passed; confirmation flow still required"):root.readiness.blockers.join("\n") }
                }
            }
            Rectangle {
                Layout.preferredWidth:300;Layout.preferredHeight:350;radius:10;color:M.Style.titleBarButtonHoverColor;border.width:1;border.color:M.Style.inputBorderColorInActive
                ColumnLayout { anchors.fill:parent;anchors.margins:11;spacing:6
                    M.TextPlain { color:M.Style.defaultFontColor;font.bold:true;text:qsTr("Recent trades") }
                    M.TextPlain {
                        Layout.fillWidth:true; wrapMode:Text.WordWrap
                        color:M.Style.defaultFontColor; opacity:.65
                        text:qsTr("Trade execution is disabled in this research preview, so no trade history can exist yet.")
                    }
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
                    RowLayout { anchors.fill:parent;anchors.margins:10;M.TextPlain { color:sideText==="BUY"?"#49c991":"#ef6575";font.bold:true;text:sideText+" "+root.baseCoin } M.TextPlain { Layout.fillWidth:true;color:M.Style.defaultFontColor;text:amountText+" @ "+priceText } M.TextPlain { color:"#d5a6ff";text:settlementLabel+": "+settlementText+" XMZ · fees "+feeText+" XMZ · "+qsTr("external fee: ")+minerFeeText } M.InlineButton { text:qsTr("Cancel");onClicked:{ root.dex.cancelLocalOffer(offerId); root.refreshOffers() } } }
                } }
            }
        }
        }
    }
}
