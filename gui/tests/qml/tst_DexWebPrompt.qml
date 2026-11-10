import QtQuick 2.9
import QtTest 1.2
import "../../components" as Components

// The wallet's two questions when the DEX web page is in use: accept the page's connection
// (with the code the page shows), and approve a trade from the terms the wallet holds.
TestCase {
    id: test
    name: "DexWebPrompt"
    function releaseFocus() {}
    when: windowShown
    visible: true
    width: 1000
    height: 800
    property QtObject windowSettings: QtObject { id: appWindow; property bool themeTransition: false
        property var currentWallet: QtObject { function address(a, b) { return "FXmain" } } }
    property QtObject panel: QtObject { id: middlePanel; property string state: ""; property var filled: []
        property var transferView: QtObject { function sendTo(address, paymentId, description, amount) { middlePanel.filled = [address, amount] } } }
    property QtObject translations: QtObject { id: translationManager; property string emptyString: "" }
    property QtObject web: QtObject {
        property var pairRequest: ({})
        property var connection: ({})
        property bool keysWanted: false
        signal xmzAddRequested(string xmz)
        signal xmzReturnRequested()
        function dismissKeys() { keysWanted = false }
        property string accepted: ""
        property string refused: ""
        function acceptPairing(id) { accepted = id; pairRequest = ({}); connection = { origin: "https://monzero.org", account: "0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b" }; return true }
        function refusePairing(id) { refused = id; pairRequest = ({}) }
    }
    property QtObject dex: QtObject {
        property var evmTradeStatus: ({})
        property var dexWalletStatus: ({ state: "ready", unlocked: "40", address: "FXtrading", keysExist: false, keysReady: false })
        property string password: ""
        property string returnedTo: "none"
        function setUpTradingKeys(p) { password = p; return true }
        function sendFromDexWallet(address, amount) { returnedTo = address; return true }
        property string approved: ""
        function approveEvmTrade(commitment) { approved = commitment; return true }
        property int abandoned: 0
        function abandonEvmTrade() { abandoned++; evmTradeStatus = ({}); return true }
    }
    Components.DexWebPrompt { id: prompt; dex: test.dex; web: test.web }

    function named(name) { var o = findChild(prompt, name); return o }
    function popup(name) { for (var i = 0; i < prompt.resources.length; ++i) if (prompt.resources[i].objectName === name) return prompt.resources[i]; return null }
    function inside(item, name) { return findChild(item, name) }

    function test_1_connection_is_accepted_in_the_wallet() {
        var dialog = popup("dexWebPairDialog")
        verify(dialog); verify(!dialog.opened)
        web.pairRequest = { id: "req1", origin: "https://monzero.org", account: "0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b", code: "571016" }
        tryVerify(function () { return dialog.opened })
        compare(inside(dialog.contentItem, "dexWebPairCode").text, "571016")
        mouseClick(inside(dialog.contentItem, "dexWebPairAccept"))
        compare(web.accepted, "req1")
        tryVerify(function () { return !dialog.opened })
    }
    function test_2_refusing_closes_the_request() {
        var dialog = popup("dexWebPairDialog")
        web.pairRequest = { id: "req2", origin: "https://monzero.org", account: "0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b", code: "000123" }
        tryVerify(function () { return dialog.opened })
        mouseClick(inside(dialog.contentItem, "dexWebPairRefuse"))
        compare(web.refused, "req2")
        tryVerify(function () { return !dialog.opened })
    }
    function test_3_trade_terms_come_from_the_wallet_and_approval_is_here() {
        var dialog = popup("dexWebApproveDialog")
        verify(!dialog.opened)
        dex.evmTradeStatus = { awaitingApproval: true, commitment: "0xabc", snapshot: { role: "owner", offer: { chainId: 56, value: "5000000000000000", xmzAmount: "300000000000000" } } }
        tryVerify(function () { return dialog.opened })
        var terms = inside(dialog.contentItem, "dexWebApproveTerms").text
        verify(terms.indexOf("You pay 0.005 BNB") === 0, terms)
        verify(terms.indexOf("You receive 3000 XMZ") > 0, terms)
        mouseClick(inside(dialog.contentItem, "dexWebApprove"))
        compare(dex.approved, "0xabc")
        dex.evmTradeStatus = { awaitingApproval: false, commitment: "0xabc" }
        tryVerify(function () { return !dialog.opened })
    }
    function test_4_seller_sees_the_xmz_as_what_it_pays() {
        var dialog = popup("dexWebApproveDialog")
        dex.evmTradeStatus = { awaitingApproval: true, commitment: "0xdef", snapshot: { role: "claimer", offer: { chainId: 1, value: "2000000000000000", xmzAmount: 5000000000000 } } }
        tryVerify(function () { return dialog.opened })
        var terms = inside(dialog.contentItem, "dexWebApproveTerms").text
        verify(terms.indexOf("You pay 50 XMZ") === 0, terms)
        verify(terms.indexOf("You receive 0.002 ETH") > 0, terms)
        mouseClick(inside(dialog.contentItem, "dexWebApproveLater"))
        tryVerify(function () { return !dialog.opened })
        compare(dex.approved, "0xabc")   // "Not now" approves nothing
        dex.evmTradeStatus = ({})
    }
    function test_5_no_trade_prompt_without_a_connected_page() {
        var dialog = popup("dexWebApproveDialog")
        web.connection = ({})
        dex.evmTradeStatus = { awaitingApproval: true, commitment: "0x999", snapshot: { role: "owner", offer: { chainId: 1, value: "1", xmzAmount: "1" } } }
        wait(200)
        verify(!dialog.opened)   // trades started in the app are approved on its DEX page
    }
    function test_6_password_is_asked_in_the_wallet_when_the_page_needs_the_keys() {
        var dialog = popup("dexWebKeysDialog")
        verify(!dialog.opened)
        web.keysWanted = true
        tryVerify(function () { return dialog.opened })
        compare(inside(dialog.contentItem, "dexWebKeysTitle").text, "Choose a trading password")
        inside(dialog.contentItem, "dexWebKeysPassword").text = "pw-1"
        verify(!inside(dialog.contentItem, "dexWebKeysGo").enabled)   // new keys: the password twice
        inside(dialog.contentItem, "dexWebKeysRepeat").text = "pw-1"
        mouseClick(inside(dialog.contentItem, "dexWebKeysGo"))
        compare(dex.password, "pw-1")
        dex.dexWalletStatus = { state: "ready", unlocked: "40", address: "FXtrading", keysExist: true, keysReady: true }
        tryVerify(function () { return !dialog.opened })
        web.keysWanted = false
    }
    function test_7_existing_keys_are_unlocked_with_one_password_and_can_be_put_off() {
        var dialog = popup("dexWebKeysDialog")
        dex.dexWalletStatus = { state: "ready", unlocked: "40", address: "FXtrading", keysExist: true, keysReady: false }
        web.keysWanted = true
        tryVerify(function () { return dialog.opened })
        compare(inside(dialog.contentItem, "dexWebKeysTitle").text, "Enter your trading password")
        verify(!inside(dialog.contentItem, "dexWebKeysRepeat").visible)
        mouseClick(inside(dialog.contentItem, "dexWebKeysLater"))
        tryVerify(function () { return !dialog.opened })
        compare(dex.password, "pw-1")
    }
    function test_8_adding_xmz_opens_the_send_page_filled_in() {
        web.xmzAddRequested("125.5")
        compare(middlePanel.state, "Transfer")
        compare(middlePanel.filled[0], "FXtrading")
        compare(middlePanel.filled[1], "125.5")
    }
    function test_9_moving_xmz_back_goes_only_to_this_wallet_and_only_when_confirmed() {
        var dialog = popup("dexWebReturnDialog")
        web.xmzReturnRequested()
        tryVerify(function () { return dialog.opened })
        compare(dex.returnedTo, "none")
        verify(inside(dialog.contentItem, "dexWebReturnText").text.indexOf("40 XMZ") === 0)
        mouseClick(inside(dialog.contentItem, "dexWebReturnGo"))
        compare(dex.returnedTo, "FXmain")
        mouseClick(inside(dialog.contentItem, "dexWebReturnClose"))
        tryVerify(function () { return !dialog.opened })
    }
    function test_a_running_trade_shows_a_status_line_in_the_wallet() {
        var bar = findChild(prompt, "dexWebTradeBar"), text = findChild(prompt, "dexWebTradeBarText")
        web.connection = { origin: "https://monzero.org", account: "0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b" }
        dex.evmTradeStatus = ({})
        verify(!bar.visible)
        dex.evmTradeStatus = { configured: true, snapshot: { role: "claimer", stage: "XmzLockSent", approved: true, done: false, progress: { xmzLock: { confirmations: 8, needed: 10 } } } }
        verify(bar.visible)
        verify(text.text.indexOf("step 2 of 4: locking your XMZ (8 of 10 confirmations)") > 0, text.text)
        dex.evmTradeStatus = { configured: true, snapshot: { role: "owner", stage: "Sweeping", approved: true, done: false, progress: { xmzSweep: { confirmations: 3, needed: 10 } } } }
        verify(text.text.indexOf("step 6 of 6: your XMZ is arriving (3 of 10 confirmations)") > 0, text.text)
        dex.evmTradeStatus = { configured: true, snapshot: { role: "owner", stage: "Done(Swapped)", approved: true, done: true } }
        verify(!bar.visible)
        dex.evmTradeStatus = { configured: true, snapshot: { role: "owner", stage: "Start", approved: false, done: false } }
        verify(!bar.visible)   // an old trade nobody approved is not "in progress"
        dex.evmTradeStatus = ({}); web.connection = ({})
    }
    function test_b_an_unapproved_trade_can_be_cancelled_from_the_prompt() {
        var dialog = popup("dexWebApproveDialog")
        web.connection = { origin: "https://monzero.org", account: "0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b" }
        dex.evmTradeStatus = { awaitingApproval: true, commitment: "0xc0c", snapshot: { role: "owner", stage: "Start", offer: { chainId: 56, value: "1", xmzAmount: "1" } } }
        tryVerify(function () { return dialog.opened })
        mouseClick(inside(dialog.contentItem, "dexWebApproveCancel"))
        compare(dex.abandoned, 1)
        tryVerify(function () { return !dialog.opened })
        compare(dex.approved, "0xabc")   // nothing was approved
        web.connection = ({})
    }
}
