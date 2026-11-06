pragma ComponentBehavior: Bound
import QtQuick

// 节点状态分布环图：四段按数量占比，中间显示健康度
Item {
    id: donut

    property list<int> counts: [0, 0, 0, 0]
    readonly property int total: counts[0] + counts[1] + counts[2] + counts[3]

    function startOf(i: int): real {
        let acc = 0
        for (let k = 0; k < i; ++k)
            acc += counts[k]
        return -90 + (total > 0 ? 360 * acc / total : 0)
    }
    function sweepOf(i: int): real {
        if (total <= 0)
            return 0
        const sweep = 360 * counts[i] / total
        // 段与段之间留 1.5 度缝；只有一段时画满整圈
        return counts[i] === total ? 360 : Math.max(0, sweep - 1.5)
    }

    ArcSegment {
        anchors.fill: parent
        startAngle: 0
        sweepAngle: 360
        thickness: 18
        color: Theme.grid
    }
    Repeater {
        model: 4
        ArcSegment {
            required property int index
            anchors.fill: parent
            thickness: 18
            color: Theme.statusColor(index)
            startAngle: donut.startOf(index)
            sweepAngle: donut.sweepOf(index)
            visible: sweepAngle > 0
            Behavior on startAngle { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
            Behavior on sweepAngle { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
        }
    }

    Column {
        anchors.centerIn: parent
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: donut.total > 0 ? (100 * donut.counts[0] / donut.total).toFixed(1) + "%" : "--"
            color: Theme.text
            font.pixelSize: Theme.px(22)
            font.bold: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "健康度"
            color: Theme.sub
            font.pixelSize: Theme.px(12)
        }
    }
}
