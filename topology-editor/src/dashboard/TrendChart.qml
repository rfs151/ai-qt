pragma ComponentBehavior: Bound
import QtQuick
import Topology.Dashboard

// 带网格和刻度的趋势图，曲线用 C++ 的 TrendItem（Scene Graph 几何体）绘制
Item {
    id: chart

    property string title
    property real maximum: 100
    property string unit
    property list<real> series1
    property list<real> series2
    property string name1
    property string name2
    property color color1: Theme.accent
    property color color2: Theme.accent2
    property real threshold: -1     // 大于 0 时画一条阈值虚线
    property int capacity: 120

    readonly property int labelWidth: Theme.px(40)

    Text {
        id: titleText
        text: chart.title
        color: Theme.sub
        font.pixelSize: Theme.px(12)
    }
    Row {
        anchors.right: parent.right
        anchors.verticalCenter: titleText.verticalCenter
        spacing: Theme.px(14)
        Repeater {
            model: [
                { name: chart.name1, color: chart.color1, last: chart.series1.length ? chart.series1[chart.series1.length - 1] : 0 },
                { name: chart.name2, color: chart.color2, last: chart.series2.length ? chart.series2[chart.series2.length - 1] : 0 }
            ]
            Row {
                id: legend
                required property var modelData
                spacing: Theme.px(5)
                visible: legend.modelData.name.length > 0
                Rectangle {
                    width: Theme.px(10); height: Theme.px(3); radius: 1
                    color: legend.modelData.color
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: legend.modelData.name + " " + legend.modelData.last.toFixed(1) + " " + chart.unit
                    color: Theme.sub
                    font.pixelSize: Theme.px(11)
                }
            }
        }
    }

    Item {
        id: plot
        anchors.top: titleText.bottom
        anchors.topMargin: Theme.px(8)
        anchors.left: parent.left
        anchors.leftMargin: chart.labelWidth
        anchors.right: parent.right
        anchors.bottom: xAxis.top
        anchors.bottomMargin: Theme.px(4)

        // 4 条横向网格线 + 左侧刻度
        Repeater {
            model: 5
            Item {
                id: gridLine
                required property int index
                width: plot.width
                y: plot.height * index / 4
                Rectangle { width: parent.width; height: 1; color: Theme.grid }
                Text {
                    x: -chart.labelWidth
                    width: chart.labelWidth - Theme.px(6)
                    horizontalAlignment: Text.AlignRight
                    anchors.verticalCenter: parent.top
                    text: Math.round(chart.maximum * (4 - gridLine.index) / 4)
                    color: Theme.dim
                    font.pixelSize: Theme.px(10)
                }
            }
        }
        // 阈值虚线
        Row {
            visible: chart.threshold > 0
            width: plot.width
            clip: true
            y: plot.height * (1 - chart.threshold / chart.maximum)
            spacing: Theme.px(4)
            Repeater {
                model: Math.max(0, Math.ceil(plot.width / Theme.px(10)))
                Rectangle { width: Theme.px(6); height: 1; color: Theme.statusColors[2]; opacity: 0.7 }
            }
        }

        TrendItem {
            anchors.fill: parent
            values: chart.series2
            capacity: chart.capacity
            maxValue: chart.maximum
            lineColor: chart.color2
            fillColor: Qt.alpha(chart.color2, 0.10)
            lineWidth: Theme.s * 1.6
            visible: chart.name2.length > 0
        }
        TrendItem {
            anchors.fill: parent
            values: chart.series1
            capacity: chart.capacity
            maxValue: chart.maximum
            lineColor: chart.color1
            fillColor: Qt.alpha(chart.color1, 0.28)
            lineWidth: Theme.s * 2.2
        }
    }

    Item {
        id: xAxis
        anchors.left: plot.left
        anchors.right: plot.right
        anchors.bottom: parent.bottom
        height: Theme.px(14)
        Repeater {
            model: ["-2 分钟", "-90 秒", "-60 秒", "-30 秒", "现在"]
            Text {
                required property int index
                required property string modelData
                x: xAxis.width * index / 4 - (index === 4 ? width : index === 0 ? 0 : width / 2)
                text: modelData
                color: Theme.dim
                font.pixelSize: Theme.px(10)
            }
        }
    }
}
