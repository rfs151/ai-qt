pragma ComponentBehavior: Bound
import QtQuick

// 带标题和四角装饰的面板。子元素放进 content 区，标题栏右侧可放 tools
Rectangle {
    id: panel

    property string title
    property string subtitle
    default property alias content: body.data
    property alias tools: toolRow.data

    color: Qt.alpha(Theme.panel, 0.82)
    radius: Theme.px(6)
    border.color: Theme.panelEdge
    border.width: 1

    // 四角的亮色折角
    Repeater {
        model: 4
        Item {
            id: corner
            required property int index
            readonly property int len: Theme.px(10)
            width: len
            height: len
            x: (index % 2) ? panel.width - len : 0
            y: index >= 2 ? panel.height - len : 0
            Rectangle {
                width: corner.len; height: 2; color: Theme.accent; opacity: 0.8
                y: corner.index >= 2 ? corner.len - 2 : 0
            }
            Rectangle {
                width: 2; height: corner.len; color: Theme.accent; opacity: 0.8
                x: (corner.index % 2) ? corner.len - 2 : 0
            }
        }
    }

    Item {
        id: header
        x: Theme.px(14)
        y: Theme.px(10)
        width: panel.width - 2 * x
        height: Theme.px(24)

        Rectangle {
            id: mark
            width: Theme.px(3)
            height: Theme.px(14)
            radius: 1
            color: Theme.accent
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: titleText
            text: panel.title
            color: Theme.text
            font.pixelSize: Theme.px(15)
            font.bold: true
            anchors.left: mark.right
            anchors.leftMargin: Theme.px(8)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: panel.subtitle
            color: Theme.dim
            font.pixelSize: Theme.px(11)
            anchors.left: titleText.right
            anchors.leftMargin: Theme.px(8)
            anchors.baseline: titleText.baseline
        }
        Row {
            id: toolRow
            spacing: Theme.px(8)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Item {
        id: body
        anchors.fill: parent
        anchors.topMargin: header.y + header.height + Theme.px(8)
        anchors.margins: Theme.px(14)
    }
}
