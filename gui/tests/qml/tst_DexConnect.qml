import QtQuick 2.9
import QtTest 1.2
import "../../components" as Components

// The wallet's simple DEX page: make or unlock the trading keys, open the website, see what is
// connected and how much XMZ is ready. No offers, prices or charts.
TestCase {
    id: test
    name: "DexConnect"
    function releaseFocus() {}
    when: windowShown
    visible: true
    width: 1000
    height: 900
    property QtObject windowSettings: QtObject { id: appWindow; property bool themeTransition: false; property var currentWallet: null }
    property QtObject translations: QtObject { id: translationManager; property string emptyString: "" }
    property QtObject panel: QtObject { id: middlePanel; property string sentTo: ""; function sendTo(address, paymentId, description) { sentTo = address } }
    property QtObject web: QtObject {
        property var connection: ({})
        property int disconnects: 0
        function disconnectPage() { disconnects++; connection = ({}) }
    }
    property QtObject dex: QtObject {
        property var dexWalletStatus: ({ state: "ready", balance: "0", unlocked: "0", address: "FXtest", keysExist: false, keysReady: false })
        property var evmTradeStatus: ({})
        property string password: ""
        function setUpTradingKeys(p) { password = p; return true }
        function sendFromDexWallet(a, n) { return true }
    }
    Components.DexConnect { id: page; width: 900; dex: test.dex; web: test.web }
    function named(name) { return findChild(page, name) }

    function test_1_new_user_creates_keys_with_a_repeated_password() {
        verify(named("dexConnectKeysTitle").text.indexOf("Create your trading keys") > 0)
        verify(!named("dexConnectOpenSite").enabled)
        named("dexConnectPassword").text = "secret-1"
        verify(!named("dexConnectKeys").enabled)
        named("dexConnectPasswordRepeat").text = "secret-1"
        verify(named("dexConnectKeys").enabled)
        mouseClick(named("dexConnectKeys"))
        compare(dex.password, "secret-1")
        compare(named("dexConnectPassword").text, "")
    }
    function test_2_existing_keys_need_one_password() {
        dex.dexWalletStatus = { state: "ready", balance: "0", unlocked: "0", address: "FXtest", keysExist: true, keysReady: false }
        verify(!named("dexConnectPasswordRepeat").visible)
        compare(named("dexConnectKeys").text, "Unlock")
    }
    function test_3_unlocked_keys_open_the_website_step() {
        dex.dexWalletStatus = { state: "ready", balance: "12.5", unlocked: "10", address: "FXtest", keysExist: true, keysReady: true, tradeAccount: "0x1" }
        verify(!named("dexConnectPassword").visible)
        verify(named("dexConnectOpenSite").enabled)
        verify(named("dexConnectSiteTitle").text.indexOf("Open the DEX website") > 0)
        verify(named("dexConnectBalance").text.indexOf("10 XMZ ready") === 0)
        verify(!named("dexConnectDisconnect").visible)
    }
    function test_4_connection_is_shown_and_can_be_dropped() {
        web.connection = { origin: "https://monzero.org", account: "0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b" }
        verify(named("dexConnectSiteTitle").text.indexOf("Connected") > 0)
        verify(named("dexConnectSiteStatus").text.indexOf("0xaD13C3F11Dd4FeE608e6764ac3e48b03952E630b") > 0)
        mouseClick(named("dexConnectDisconnect"))
        compare(web.disconnects, 1)
        verify(named("dexConnectSiteTitle").text.indexOf("Open the DEX website") > 0)
    }
    function test_5_add_xmz_opens_the_send_page_with_the_trading_address() {
        mouseClick(named("dexConnectAddXmz"))
        compare(middlePanel.sentTo, "FXtest")
    }
    function test_6_no_trade_card_without_a_trade() {
        verify(!named("dexConnectTrade").visible)
        dex.evmTradeStatus = { configured: true, snapshot: { role: "owner", stage: "Start", offer: { chainId: 56, value: "5000000000000000", xmzAmount: "300000000000000" } } }
        verify(named("dexConnectTrade").visible)
        dex.evmTradeStatus = ({})
    }
}
