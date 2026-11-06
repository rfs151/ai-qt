import QtQuick
import Topology.Dashboard

// 设备矩阵里的一张卡片：状态、CPU、温度、流量、最近一分钟 CPU 曲线
Rectangle {
    id: card

    required property int deviceId
    required property string name
    required property string typeName
    required property int status
    required property bool online
    required property real cpu
    required property real temp
    required property real inMbps
    required property real outMbps
    required property var spark

    readonly property color statusColor: Theme.statusColor(status)
    readonly property bool alarming: status === 2

    radius: Theme.px(5)
    color: hover.containsMouse ? Qt.lighter(Theme.panel, 1.35) : Qt.lighter(Theme.panel, 1.12)
    border.width: alarming ? 2 : 1
    border.color: status === 0 ? Theme.panelEdge : statusColor
    opacity: online ? 1 : 0.55

    // 告警卡片的外发光脉冲
    Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.px(3)
        radius: card.radius + Theme.px(3)
        color: "transparent"
        border.width: Theme.px(3)
        border.color: card.statusColor
        visible: card.alarming
        SequentialAnimation on opacity {
            running: card.alarming
            loops: Animation.Infinite
            NumberAnimation { from: 0.7; to: 0.0; duration: 900; easing.type: Easing.OutQuad }
            PauseAnimation { duration: 200 }
        }
    }

    Rectangle {
        id: dot
        x: Theme.px(10)
        y: Theme.px(12)
        width: Theme.px(8)
        height: width
        radius: width / 2
        color: card.statusColor
    }
    Text {
        anchors.left: dot.right
        anchors.leftMargin: Theme.px(6)
        anchors.right: typeText.left
        anchors.rightMargin: Theme.px(4)
        anchors.verticalCenter: dot.verticalCenter
        text: card.name
        color: Theme.text
        font.pixelSize: Theme.px(13)
        font.bold: true
        elide: Text.ElideRight
    }
    Text {
        id: typeText
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(10)
        anchors.verticalCenter: dot.verticalCenter
        text: card.online ? card.typeName : "离线"
        color: card.online ? Theme.dim : Theme.statusColors[3]
        font.pixelSize: Theme.px(11)
    }

    // CPU 数字 + 进度条
    Text {
        id: cpuText
        x: Theme.px(10)
        y: Theme.px(28)
        text: card.cpu.toFixed(0)
        color: Theme.levelColor(card.cpu, 80, 95, Theme.text)
        font.pixelSize: Theme.px(24)
        font.bold: true
    }
    Text {
        anchors.left: cpuText.right
        anchors.leftMargin: Theme.px(2)
        anchors.baseline: cpuText.baseline
        text: "% CPU"
        color: Theme.sub
        font.pixelSize: Theme.px(11)
    }
    Column {
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(10)
        y: Theme.px(32)
        spacing: Theme.px(1)
        Text {
            anchors.right: parent.right
            text: card.temp.toFixed(1) + " ℃"
            color: Theme.levelColor(card.temp, 70, 80, Theme.sub)
            font.pixelSize: Theme.px(11)
        }
        Text {
            anchors.right: parent.right
            text: "↓" + card.inMbps.toFixed(0) + "  ↑" + card.outMbps.toFixed(0)
            color: Theme.dim
            font.pixelSize: Theme.px(10)
        }
    }

    Rectangle {
        id: bar
        x: Theme.px(10)
        y: Theme.px(62)
        width: parent.width - Theme.px(20)
        height: Theme.px(4)
        radius: height / 2
        color: Theme.grid
        Rectangle {
            width: parent.width * Math.min(card.cpu, 100) / 100
            height: parent.height
            radius: parent.radius
            color: Theme.levelColor(card.cpu, 80, 95, Theme.accent)
            Behavior on width { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
        }
    }

    TrendItem {
        anchors.left: bar.left
        anchors.right: bar.right
        anchors.top: bar.bottom
        anchors.topMargin: Theme.px(4)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(6)
        values: card.spark
        capacity: 30
        lineColor: card.status === 0 ? Theme.accent : card.statusColor
        fillColor: Qt.alpha(lineColor, 0.18)
        lineWidth: 1.5 * Theme.s
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: Backend.locate(card.deviceId)
    }
}
