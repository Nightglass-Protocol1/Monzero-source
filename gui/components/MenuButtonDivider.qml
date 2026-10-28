import QtQuick 2.9

import "." as MonzeroComponents
import "effects/" as MonzeroEffects

Rectangle {
    color: MonzeroComponents.Style.appWindowBorderColor
    height: 1

    MonzeroEffects.ColorTransition {
        targetObj: parent
        blackColor: MonzeroComponents.Style._b_appWindowBorderColor
        whiteColor: MonzeroComponents.Style._w_appWindowBorderColor
    }
}
