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
    property QtObject windowSettings: QtObject { id: appWindow; property bool themeTransition: false }
    property QtObject translations: QtObject { id: translationManager; property string emptyString: "" }
    property QtObject web: QtObject {
        property var pairRequest: ({})
        property var connection: ({})
        property string accepted: ""
        property string refused: ""
        function acceptPairing(id) { accepted = id; pairRequest = ({}); connection = { origin: "https://monzero.org", account: "0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b" }; return true }
        function refusePairing(id) { refused = id; pairRequest = ({}) }
    }
    property QtObject dex: QtObject {
        property var evmTradeStatus: ({})
        property string approved: ""
        function approveEvmTrade(commitment) { approved = commitment; return true }
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
}
