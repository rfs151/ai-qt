import QtQuick

// 顶部指标卡：数字变化时滚动过渡
Rectangle {
    id: card

    property string title
    property real value: 0
    property int decimals: 0
    property string unit
    property string suffix          // 紧跟数字的部分，比如 "/ 300"
    property string detail
    property color accent: Theme.accent

    property real shown: value
    Behavior on shown { NumberAnimation { duration: 700; easing.type: Easing.OutCubic } }

    radius: Theme.px(6)
    border.color: Theme.panelEdge
    gradient: Gradient {
        GradientStop { position: 0; color: Qt.alpha(card.accent, 0.16) }
        GradientStop { position: 1; color: Qt.alpha(Theme.panel, 0.85) }
    }

    Rectangle {
        width: Theme.px(3)
        height: parent.height - Theme.px(24)
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        radius: 1
        color: card.accent
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: Theme.px(16)
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(8)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.px(2)

        Text {
            text: card.title
            color: Theme.sub
            font.pixelSize: Theme.px(12)
        }
        Row {
            spacing: Theme.px(4)
            Text {
                id: num
                text: card.shown.toFixed(card.decimals)
                color: card.accent
                font.pixelSize: Theme.px(30)
                font.bold: true
            }
            Text {
                text: card.suffix + (card.unit ? " " + card.unit : "")
                visible: text.length > 0
                color: Theme.sub
                font.pixelSize: Theme.px(13)
                anchors.baseline: num.baseline
            }
        }
        Text {
            text: card.detail
            color: Theme.dim
            font.pixelSize: Theme.px(11)
            elide: Text.ElideRight
            width: parent.width
        }
    }
}
