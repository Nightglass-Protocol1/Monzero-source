pragma Singleton

import QtQuick 2.5

QtObject {
    property bool blackTheme: true
    property QtObject fontMedium: FontLoader { id: _fontMedium; source: "qrc:/fonts/Roboto-Medium.ttf"; }
    property QtObject fontBold: FontLoader { id: _fontBold; source: "qrc:/fonts/Roboto-Bold.ttf"; }
    property QtObject fontLight: FontLoader { id: _fontLight; source: "qrc:/fonts/Roboto-Light.ttf"; }
    property QtObject fontRegular: FontLoader { id: _fontRegular; source: "qrc:/fonts/Roboto-Regular.ttf"; }

    property QtObject fontMonoMedium: FontLoader { id: _fontMonoMedium; source: "qrc:/fonts/RobotoMono-Medium.ttf"; }
    property QtObject fontMonoBold: FontLoader { id: _fontMonoBold; source: "qrc:/fonts/RobotoMono-Bold.ttf"; }
    property QtObject fontMonoLight: FontLoader { id: _fontMonoLight; source: "qrc:/fonts/RobotoMono-Light.ttf"; }
    property QtObject fontMonoRegular: FontLoader { id: _fontMonoRegular; source: "qrc:/fonts/RobotoMono-Regular.ttf"; }

    // Monzero teal: cyan is the brand/action colour while amber stays
    // reserved for warnings.  Keep the legacy `orange` name because it is the
    // accent token used throughout the upstream QML component set.
    property string grey: "#20383C"
    property string orange: "#29D6CB"
    property string white: "#FFFFFF"
    property string green: "#55D6A7"
    property string moneroGrey: "#29454A"
    property string warningColor: "#FFB454"
    property string accent: "#29D6CB"
    property string accentBright: "#79EEE5"
    property string accentMuted: "#177E79"
    property string surfaceRaised: "#10272C"

    property string defaultFontColor: blackTheme ? _b_defaultFontColor : _w_defaultFontColor
    property string dimmedFontColor: blackTheme ? _b_dimmedFontColor : _w_dimmedFontColor
    property string lightGreyFontColor: blackTheme ? _b_lightGreyFontColor : _w_lightGreyFontColor
    property string errorColor: blackTheme ? _b_errorColor : _w_errorColor
    property string textSelectionColor: blackTheme ? _b_textSelectionColor : _w_textSelectionColor
    property string textSelectedColor: blackTheme ? _b_textSelectedColor : _w_textSelectedColor

    property string inputBoxBackground: blackTheme ? _b_inputBoxBackground : _w_inputBoxBackground
    property string inputBoxBackgroundDisabled: blackTheme ? _b_inputBoxBackgroundDisabled : _w_inputBoxBackgroundDisabled
    property string inputBoxBackgroundError: blackTheme ? _b_inputBoxBackgroundError : _w_inputBoxBackgroundError
    property string inputBoxColor: blackTheme ? _b_inputBoxColor : _w_inputBoxColor
    property string legacy_placeholderFontColor: blackTheme ? _b_legacy_placeholderFontColor : _w_legacy_placeholderFontColor
    property string inputBorderColorActive: blackTheme ? _b_inputBorderColorActive : _w_inputBorderColorActive
    property string inputBorderColorInActive: blackTheme ? _b_inputBorderColorInActive : _w_inputBorderColorInActive
    property string inputBorderColorInvalid: blackTheme ? _b_inputBorderColorInvalid : _w_inputBorderColorInvalid

    property string buttonBackgroundColor: blackTheme ? _b_buttonBackgroundColor : _w_buttonBackgroundColor
    property string buttonBackgroundColorHover: blackTheme ? _b_buttonBackgroundColorHover : _w_buttonBackgroundColorHover
    property string buttonBackgroundColorDisabled: blackTheme ? _b_buttonBackgroundColorDisabled : _w_buttonBackgroundColorDisabled
    property string buttonBackgroundColorDisabledHover: blackTheme ? _b_buttonBackgroundColorDisabledHover : _w_buttonBackgroundColorDisabledHover
    property string buttonInlineBackgroundColor: blackTheme ? _b_buttonInlineBackgroundColor : _w_buttonInlineBackgroundColor
    property string buttonInlineBackgroundColorHover: blackTheme ? _b_buttonInlineBackgroundColorHover : _w_buttonInlineBackgroundColorHover
    property string buttonTextColor: blackTheme ? _b_buttonTextColor : _w_buttonTextColor
    property string buttonTextColorDisabled: blackTheme ? _b_buttonTextColorDisabled : _w_buttonTextColorDisabled
    property string buttonSecondaryBackgroundColor: blackTheme ? _b_buttonSecondaryBackgroundColor : _w_buttonSecondaryBackgroundColor
    property string buttonSecondaryBackgroundColorHover: blackTheme ? _b_buttonSecondaryBackgroundColorHover : _w_buttonSecondaryBackgroundColorHover
    property string buttonSecondaryTextColor: blackTheme ? _b_buttonSecondaryTextColor : _w_buttonSecondaryTextColor
    property string dividerColor: blackTheme ? _b_dividerColor : _w_dividerColor
    property real dividerOpacity: blackTheme ? _b_dividerOpacity : _w_dividerOpacity

    property string titleBarBackgroundGradientStart: blackTheme ? _b_titleBarBackgroundGradientStart : _w_titleBarBackgroundGradientStart
    property string titleBarBackgroundGradientStop: blackTheme ? _b_titleBarBackgroundGradientStop : _w_titleBarBackgroundGradientStop
    property string titleBarBackgroundBorderColor: blackTheme ? _b_titleBarBackgroundBorderColor : _w_titleBarBackgroundBorderColor
    property string titleBarLogoSource: blackTheme ? _b_titleBarLogoSource : _w_titleBarLogoSource
    property string titleBarMinimizeSource: blackTheme ? _b_titleBarMinimizeSource : _w_titleBarMinimizeSource
    property string titleBarFullscreenSource: blackTheme ? _b_titleBarFullscreenSource : _w_titleBarFullscreenSource
    property string titleBarCloseSource: blackTheme ? _b_titleBarCloseSource : _w_titleBarCloseSource
    property string titleBarButtonHoverColor: blackTheme ? _b_titleBarButtonHoverColor : _w_titleBarButtonHoverColor

    property string wizardBackgroundGradientStart: blackTheme ? _b_wizardBackgroundGradientStart : _w_wizardBackgroundGradientStart
    property string middlePanelBackgroundGradientStart: blackTheme ? _b_middlePanelBackgroundGradientStart : _w_middlePanelBackgroundGradientStart
    property string middlePanelBackgroundGradientStop: blackTheme ? _b_middlePanelBackgroundGradientStop : _w_middlePanelBackgroundGradientStop
    property string middlePanelBackgroundColor: blackTheme ? _b_middlePanelBackgroundColor : _w_middlePanelBackgroundColor
    property string menuButtonFallbackBackgroundColor: blackTheme ? _b_menuButtonFallbackBackgroundColor : _w_menuButtonFallbackBackgroundColor
    property string menuButtonGradientStart: blackTheme ? _b_menuButtonGradientStart : _w_menuButtonGradientStart
    property string menuButtonGradientStop: blackTheme ? _b_menuButtonGradientStop : _w_menuButtonGradientStop
    property string menuButtonTextColor: blackTheme ? _b_menuButtonTextColor : _w_menuButtonTextColor
    property string menuButtonImageRightColorActive: blackTheme ? _b_menuButtonImageRightColorActive : _w_menuButtonImageRightColorActive
    property string menuButtonImageRightColor: blackTheme ? _b_menuButtonImageRightColor : _w_menuButtonImageRightColor
    property string menuButtonImageRightSource: blackTheme ? _b_menuButtonImageRightSource : _w_menuButtonImageRightSource
    property string menuButtonImageDotArrowSource: blackTheme ? _b_menuButtonImageDotArrowSource : _w_menuButtonImageDotArrowSource
    property string inlineButtonTextColor: blackTheme ? _b_inlineButtonTextColor : _w_inlineButtonTextColor
    property string inlineButtonBorderColor: blackTheme ? _b_inlineButtonBorderColor : _w_inlineButtonBorderColor
    property string appWindowBackgroundColor: blackTheme ? _b_appWindowBackgroundColor : _w_appWindowBackgroundColor
    property string appWindowBorderColor: blackTheme ? _b_appWindowBorderColor : _w_appWindowBorderColor
    property bool progressBarProgressTextBold: blackTheme ? _b_progressBarProgressTextBold : _w_progressBarProgressTextBold
    property string progressBarBackgroundColor: blackTheme ? _b_progressBarBackgroundColor : _w_progressBarBackgroundColor
    property string leftPanelBackgroundGradientStart: blackTheme ? _b_leftPanelBackgroundGradientStart : _w_leftPanelBackgroundGradientStart
    property string leftPanelBackgroundGradientStop: blackTheme ? _b_leftPanelBackgroundGradientStop : _w_leftPanelBackgroundGradientStop
    property string historyHeaderTextColor: blackTheme ? _b_historyHeaderTextColor : _w_historyHeaderTextColor
    property var accountColors: blackTheme ? _b_accountColors : _w_accountColors

    property string _b_defaultFontColor: "#EDF9F8"
    property string _b_dimmedFontColor: "#A8C4C6"
    property string _b_lightGreyFontColor: "#D0E5E6"
    property string _b_errorColor: "#FF6B8A"
    property string _b_textSelectionColor: "#177E79"
    property string _b_textSelectedColor: "white"

    property string _b_inputBoxBackground: "#07171C"
    property string _b_inputBoxBackgroundDisabled: "#203338"
    property string _b_inputBoxBackgroundError: "#351A2A"
    property string _b_inputBoxColor: "#EDF9F8"
    property string _b_legacy_placeholderFontColor: "#78999D"
    property string _b_inputBorderColorActive: "#29D6CB"
    property string _b_inputBorderColorInActive: "#30545B"
    property string _b_inputBorderColorInvalid: "#FF6B8A"

    property string _b_buttonBackgroundColor: "#22BDB4"
    property string _b_buttonBackgroundColorHover: "#29D6CB"
    property string _b_buttonBackgroundColorDisabled: "#345053"
    property string _b_buttonBackgroundColorDisabledHover: "#3D5C60"
    property string _b_buttonInlineBackgroundColor: "#18383D"
    property string _b_buttonInlineBackgroundColorHover: "#28535A"
    property string _b_buttonTextColor: "#032126"
    property string _b_buttonTextColorDisabled: "#A8C4C6"
    property string _b_buttonSecondaryBackgroundColor: "#163238"
    property string _b_buttonSecondaryBackgroundColorHover: "#28535A"
    property string _b_buttonSecondaryTextColor: "#D8F7F4"
    property string _b_dividerColor: "#5BDBD1"
    property real _b_dividerOpacity: 0.24

    property string _b_titleBarBackgroundGradientStart: "#0C2D34";
    property string _b_titleBarBackgroundGradientStop: "#06161B"
    property string _b_titleBarBackgroundBorderColor: "#28535A"
    property string _b_titleBarLogoSource: "qrc:///images/monzero-titlebar.png"
    property string _b_titleBarMinimizeSource: "qrc:///images/minimize.svg"
    property string _b_titleBarFullscreenSource: "qrc:///images/fullscreen.svg"
    property string _b_titleBarCloseSource: "qrc:///images/close.svg"
    property string _b_titleBarButtonHoverColor: "#35227770"

    property string _b_wizardBackgroundGradientStart: "#081E24"
    property string _b_middlePanelBackgroundGradientStart: "#0C272E"
    property string _b_middlePanelBackgroundGradientStop: "#040F14"
    property string _b_middlePanelBackgroundColor: "#081D23"
    property string _b_menuButtonFallbackBackgroundColor: "#214A50"
    property string _b_menuButtonGradientStart: "#23636A"
    property string _b_menuButtonGradientStop: "#00000000"
    property string _b_menuButtonTextColor: "#D8F7F4"
    property string _b_menuButtonImageRightColorActive: "#79EEE5"
    property string _b_menuButtonImageRightColor: "#7FA5A8"
    property string _b_menuButtonImageRightSource: "qrc:///images/right.svg"
    property string _b_menuButtonImageDotArrowSource: "qrc:///images/arrow-right-medium-white.png"
    property string _b_inlineButtonTextColor: "#79EEE5"
    property string _b_inlineButtonBorderColor: "#28535A"
    property string _b_appWindowBackgroundColor: "#030E13"
    property string _b_appWindowBorderColor: "#28535A"
    property bool _b_progressBarProgressTextBold: true
    property string _b_progressBarBackgroundColor: "#3328535A"
    property string _b_leftPanelBackgroundGradientStart: "#0B2930"
    property string _b_leftPanelBackgroundGradientStop: "#06171C"
    property string _b_historyHeaderTextColor: "#B5D5D5"
    property var _b_accountColors: ["#29D6CB", "#D66DFF", "#756BFF", "#53B9D6", "#55D6A7", "#E3A85B", "#E76D9B", "#8E7AB5"]

    property string _w_defaultFontColor: "black"
    property string _w_dimmedFontColor: "#3f3f3f"
    property string _w_lightGreyFontColor: "#515151"
    property string _w_errorColor: "#FA6800"
    property string _w_textSelectionColor: "#BBBBBB"
    property string _w_textSelectedColor: "black"

    property string _w_inputBoxBackground: "white"
    property string _w_inputBoxBackgroundDisabled: Qt.rgba(0, 0, 0, 0.20)
    property string _w_inputBoxBackgroundError: "#FFDDDD"
    property string _w_inputBoxColor: "black"
    property string _w_legacy_placeholderFontColor: "#BABABA"
    property string _w_inputBorderColorActive: Qt.rgba(0, 0, 0, 0.30)
    property string _w_inputBorderColorInActive: Qt.rgba(0, 0, 0, 0.16)
    property string _w_inputBorderColorInvalid: Qt.rgba(255, 0, 0, 0.50)

    property string _w_buttonBackgroundColor: "#087E79"
    property string _w_buttonBackgroundColorHover: "#066B68"
    property string _w_buttonBackgroundColorDisabled: "#bbbbbb"
    property string _w_buttonBackgroundColorDisabledHover: "#D1D1D1"
    property string _w_buttonInlineBackgroundColor: "#d9d9d9"
    property string _w_buttonInlineBackgroundColorHover: "#C8C8C8"
    property string _w_buttonTextColor: "white"
    property string _w_buttonTextColorDisabled: "black"
    property string _w_buttonSecondaryBackgroundColor: "#d9d9d9"
    property string _w_buttonSecondaryBackgroundColorHover: "#C8C8C8"
    property string _w_buttonSecondaryTextColor: "#4d4d4d"
    property string _w_dividerColor: "black"
    property real _w_dividerOpacity: 0.20

    property string _w_titleBarBackgroundGradientStart: "#fcfcfc"
    property string _w_titleBarBackgroundGradientStop: "#FBFBFB"
    property string _w_titleBarBackgroundBorderColor: "#DEDEDE"
    property string _w_titleBarLogoSource: "qrc:///images/themes/white/monzero-titlebar.png"
    property string _w_titleBarMinimizeSource: "qrc:///images/themes/white/minimize.svg"
    property string _w_titleBarFullscreenSource: "qrc:///images/themes/white/fullscreen.svg"
    property string _w_titleBarCloseSource: "qrc:///images/themes/white/close.svg"
    property string _w_titleBarButtonHoverColor: "#11000000"

    property string _w_wizardBackgroundGradientStart: "white"
    property string _w_middlePanelBackgroundGradientStart: "white"
    property string _w_middlePanelBackgroundGradientStop: "#ededed"
    property string _w_middlePanelBackgroundColor: "#f5f5f5"
    property string _w_menuButtonFallbackBackgroundColor: "#09000000"
    property string _w_menuButtonGradientStart: "#08000000"
    property string _w_menuButtonGradientStop: "#10FFFFFF"
    property string _w_menuButtonTextColor: "#787878"
    property string _w_menuButtonImageRightSource: "qrc:///images/right.svg"
    property string _w_menuButtonImageRightColorActive: "#087E79"
    property string _w_menuButtonImageRightColor: "#808080"
    property string _w_menuButtonImageDotArrowSource: "qrc:///images/arrow-right-medium-white.png"
    property string _w_inlineButtonTextColor: "#4d4d4d"
    property string _w_inlineButtonBorderColor: "transparent"
    property string _w_appWindowBackgroundColor: "black"
    property string _w_appWindowBorderColor: "#dedede"
    property bool _w_progressBarProgressTextBold: false
    property string _w_progressBarBackgroundColor: "#24000000"
    property string _w_leftPanelBackgroundGradientStart: "white"
    property string _w_leftPanelBackgroundGradientStop: "#f5f5f5"
    property string _w_historyHeaderTextColor: "#515151"
    property var _w_accountColors: ["#6E513C", "#6E513C", "#842129", "#458421", "#742184", "#291DBE", "#846F21", "#217F84", "#696969"]
}
