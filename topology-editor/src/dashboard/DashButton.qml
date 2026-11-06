import QtQuick

Rectangle {
    id: btn

    property string text
    property bool checkable: false
    property bool checked: false
    signal clicked

    implicitWidth: label.implicitWidth + Theme.px(22)
    implicitHeight: Theme.px(28)
    radius: Theme.px(4)
    color: checked ? Qt.alpha(Theme.accent, 0.25) : mouse.containsMouse ? Qt.alpha(Theme.accent, 0.14) : "transparent"
    border.color: checked || mouse.containsMouse ? Theme.accent : Theme.panelEdge
    Behavior on color { ColorAnimation { duration: 120 } }

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.text
        color: btn.checked ? Theme.accent : Theme.text
        font.pixelSize: Theme.px(12)
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (btn.checkable)
                btn.checked = !btn.checked
            btn.clicked()
        }
    }
}
