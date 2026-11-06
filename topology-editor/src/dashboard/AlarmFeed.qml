pragma ComponentBehavior: Bound
import QtQuick
import Topology.Dashboard

// 实时告警流：新告警从右侧滑入，点击定位到拓扑节点
ListView {
    id: feed

    clip: true
    spacing: Theme.px(6)
    model: Backend.alarms
    boundsBehavior: Flickable.StopAtBounds

    add: Transition {
        NumberAnimation { property: "x"; from: feed.width; duration: 320; easing.type: Easing.OutCubic }
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 320 }
    }
    addDisplaced: Transition {
        NumberAnimation { property: "y"; duration: 260; easing.type: Easing.OutCubic }
    }

    delegate: Rectangle {
        id: row

        required property int nodeId
        required property int level
        required property string timeText
        required property string nodeName
        required property string levelName
        required property string text

        readonly property color levelColor: Theme.statusColor(level)

        width: feed.width
        height: Theme.px(46)
        radius: Theme.px(4)
        color: hover.containsMouse ? Qt.alpha(levelColor, 0.20) : Qt.alpha(levelColor, 0.08)

        Rectangle {
            width: Theme.px(3)
            height: parent.height
            radius: 1
            color: row.levelColor
        }
        Rectangle {
            id: badge
            x: Theme.px(12)
            y: Theme.px(7)
            width: badgeText.implicitWidth + Theme.px(10)
            height: Theme.px(17)
            radius: Theme.px(3)
            color: Qt.alpha(row.levelColor, 0.22)
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: row.levelName
                color: row.levelColor
                font.pixelSize: Theme.px(11)
            }
        }
        Text {
            anchors.left: badge.right
            anchors.leftMargin: Theme.px(8)
            anchors.right: timeLabel.left
            anchors.rightMargin: Theme.px(6)
            anchors.verticalCenter: badge.verticalCenter
            text: row.nodeName
            color: Theme.text
            font.pixelSize: Theme.px(13)
            elide: Text.ElideRight
        }
        Text {
            id: timeLabel
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(8)
            anchors.verticalCenter: badge.verticalCenter
            text: row.timeText
            color: Theme.dim
            font.pixelSize: Theme.px(11)
        }
        Text {
            x: Theme.px(12)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.px(5)
            width: parent.width - Theme.px(20)
            text: row.text
            color: Theme.sub
            font.pixelSize: Theme.px(11)
            elide: Text.ElideRight
        }
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Backend.locate(row.nodeId)
        }
    }

    Text {
        anchors.centerIn: parent
        visible: feed.count === 0
        text: "暂无告警"
        color: Theme.dim
        font.pixelSize: Theme.px(13)
    }
}
