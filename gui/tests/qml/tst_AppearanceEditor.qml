import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
// Settings > Layout > Appearance: presets, a colour by hand, the card picture and reset all land
// in Style.custom and in the saved settings
Item {
    id: root
    width: 1000; height: 1400
    property var appWindow: app
    property var translationManager: translations
    QtObject { id: app; property bool themeTransition: false }
    QtObject { id: translations; property string emptyString: "" }
    QtObject { id: persistentSettings; property string customTheme: "{}" }
    function releaseFocus() {}

    M.AppearanceEditor { id: editor; width: root.width }

    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) { var f = find(item.children[i], name); if (f) return f }
        return null
    }
    function press(name) {
        var o = find(editor, name)
        for (var i = 0; i < o.children.length; ++i) if (o.children[i].clicked !== undefined) { o.children[i].clicked(null); return }
        throw new Error("no button " + name)
    }

    TestCase {
        name: "AppearanceEditor"
        when: windowShown
        function init() { editor.save({}) }

        function test_a_preset_sets_the_colours_and_is_saved() {
            press("preset_Ocean")
            compare(M.Style.accent, "#4DA3FF")
            compare(M.Style.buttonBackgroundColor, "#4DA3FF")
            compare(M.Style.appWindowBackgroundColor, "#050B16")
            compare(JSON.parse(persistentSettings.customTheme).cardStart, "#1D4E89")
        }
        function test_one_colour_by_hand_and_back() {
            var hex = find(editor, "hex_text")
            hex.text = "#ffeedd"; hex.editingFinished()
            compare(M.Style.defaultFontColor, "#FFEEDD")
            hex.text = "nope"; hex.editingFinished()
            compare(M.Style.defaultFontColor, "#FFEEDD", "an invalid colour changes nothing")
            hex.text = ""; hex.editingFinished()
            verify(M.Style.defaultFontColor !== "#FFEEDD", "empty is the theme's own again")
        }
        function test_a_preset_keeps_the_card_picture() {
            editor.set("cardImage", "file:///home/me/Pictures/card.png")
            editor.set("cardDim", 0.2)
            press("preset_Sunset")
            compare(M.Style.custom.cardImage, "file:///home/me/Pictures/card.png")
            compare(M.Style.custom.cardDim, 0.2)
            compare(M.Style.accent, "#FF8A4C")
        }
        function test_reset_restores_the_standard_look() {
            press("preset_Mono")
            press("resetAppearance")
            compare(M.Style.accent, "#29D6CB")
            compare(persistentSettings.customTheme, "{}")
        }
    }
}
