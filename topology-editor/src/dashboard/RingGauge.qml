import QtQuick

// 270 度环形仪表，超过阈值变色
Item {
    id: gauge

    property real value: 0
    property real maximum: 100
    property real warn: 80
    property real alarm: 95
    property string label
    property string unit
    property string detail
    property color baseColor: Theme.accent

    readonly property color levelColor: Theme.levelColor(value, warn, alarm, baseColor)
    property real shown: Math.max(0, Math.min(value, maximum))
    Behavior on shown { NumberAnimation { duration: 800; easing.type: Easing.OutCubic } }

    ArcSegment {
        id: track
        anchors.fill: parent
        startAngle: 135
        sweepAngle: 270
        thickness: 11
        color: Theme.grid
        roundCap: true
    }
    ArcSegment {
        anchors.fill: parent
        startAngle: 135
        sweepAngle: Math.max(0.5, 270 * gauge.shown / gauge.maximum)
        thickness: track.thickness
        color: gauge.levelColor
        roundCap: true
        Behavior on color { ColorAnimation { duration: 300 } }
    }

    Column {
        anchors.centerIn: parent
        spacing: 0
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                id: num
                text: gauge.shown.toFixed(1)
                color: Theme.text
                font.pixelSize: Theme.px(22)
                font.bold: true
            }
            Text {
                text: gauge.unit
                color: Theme.sub
                font.pixelSize: Theme.px(12)
                anchors.baseline: num.baseline
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: gauge.label
            color: Theme.sub
            font.pixelSize: Theme.px(12)
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: gauge.detail
        color: Theme.dim
        font.pixelSize: Theme.px(11)
    }
}
