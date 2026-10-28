import QtQuick 2.9

import "../components" as MonzeroComponents

TextEdit {
    color: MonzeroComponents.Style.defaultFontColor
    font.family: MonzeroComponents.Style.fontRegular.name
    selectionColor: MonzeroComponents.Style.textSelectionColor
    wrapMode: Text.Wrap
    readOnly: true
    selectByMouse: true
    // Workaround for https://bugreports.qt.io/browse/QTBUG-50587
    onFocusChanged: {
        if(focus === false)
            deselect()
    }
}
