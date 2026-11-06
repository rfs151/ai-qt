pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Topology.Dashboard

// 监控大屏主界面。数据全部来自 C++ 单例 Backend，这里只有布局和动画
Rectangle {
    id: root

    color: Theme.bg

    // 大屏是独立窗口，快捷键要在这里处理
    Shortcut {
        sequences: ["Esc", "F10"]
        onActivated: Backend.back()
    }
    Shortcut {
        sequence: "F11"
        onActivated: Backend.toggleFullScreen()
    }

    Binding {
        target: Theme
        property: "s"
        value: Math.max(0.65, Math.min(root.width / 1600, root.height / 900))
    }

    // 背景：中心辉光 + 淡网格
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.bgGlow }
            GradientStop { position: 0.45; color: Theme.bg }
            GradientStop { position: 1.0; color: Theme.bg }
        }
    }
    Repeater {
        model: Math.max(0, Math.ceil(root.width / Theme.px(48)))
        Rectangle {
            required property int index
            x: index * Theme.px(48)
            width: 1
            height: root.height
            color: Theme.grid
            opacity: 0.35
        }
    }
    Repeater {
        model: Math.max(0, Math.ceil(root.height / Theme.px(48)))
        Rectangle {
            required property int index
            y: index * Theme.px(48)
            height: 1
            width: root.width
            color: Theme.grid
            opacity: 0.35
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.px(16)
        spacing: Theme.px(12)

        // ---------------------------------------------------------- 标题栏
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.px(48)

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.px(2)
                Text {
                    text: "网络运行监控中心"
                    color: Theme.text
                    font.pixelSize: Theme.px(24)
                    font.bold: true
                    font.letterSpacing: Theme.px(3)
                }
                Text {
                    text: "NETWORK OPERATIONS CENTER · Qt Quick + C++ 数据层"
                    color: Theme.dim
                    font.pixelSize: Theme.px(11)
                    font.letterSpacing: 1
                }
            }

            // 标题下方的扫描光条
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.panelEdge
                Rectangle {
                    id: scan
                    width: parent.width * 0.18
                    height: 2
                    y: -0.5
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 0.5; color: Theme.accent }
                        GradientStop { position: 1; color: "transparent" }
                    }
                    NumberAnimation on x {
                        from: -scan.width
                        to: root.width
                        duration: 6000
                        loops: Animation.Infinite
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.px(14)

                // 链路状态
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    height: Theme.px(28)
                    width: linkRow.implicitWidth + Theme.px(20)
                    radius: height / 2
                    color: Qt.alpha(linkDot.color, 0.12)
                    border.color: Qt.alpha(linkDot.color, 0.5)
                    Row {
                        id: linkRow
                        anchors.centerIn: parent
                        spacing: Theme.px(8)
                        Rectangle {
                            id: linkDot
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.px(8)
                            height: width
                            radius: width / 2
                            color: Backend.linkConnected ? Theme.statusColors[0] : Theme.statusColors[3]
                            SequentialAnimation on opacity {
                                running: Backend.linkConnected
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 700 }
                                NumberAnimation { to: 1.0; duration: 700 }
                            }
                        }
                        Text {
                            text: Backend.linkText
                            color: Theme.text
                            font.pixelSize: Theme.px(12)
                        }
                    }
                }
                Text {
                    id: clock
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.text
                    font.pixelSize: Theme.px(18)
                    font.bold: true
                    Timer {
                        interval: 1000
                        running: true
                        repeat: true
                        triggeredOnStart: true
                        onTriggered: clock.text = Qt.formatDateTime(new Date(), "yyyy-MM-dd  HH:mm:ss")
                    }
                }
                DashButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "全屏 F11"
                    onClicked: Backend.toggleFullScreen()
                }
                DashButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "关闭 Esc"
                    onClicked: Backend.back()
                }
            }
        }

        // ---------------------------------------------------------- 指标卡
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.px(92)
            spacing: Theme.px(12)

            KpiCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "拓扑节点"
                value: Backend.nodeCount
                unit: "个"
                detail: "受监控设备 " + Backend.monitoredCount + " 台"
            }
            KpiCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "在线设备"
                value: Backend.onlineCount
                suffix: "/ " + Backend.monitoredCount
                accent: Theme.statusColors[0]
                detail: Backend.monitoredCount > 0
                        ? "在线率 " + (100 * Backend.onlineCount / Backend.monitoredCount).toFixed(1) + "%"
                        : "未接入设备"
            }
            KpiCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "告警 / 警告"
                value: Backend.alarmCount
                suffix: "/ " + Backend.warningCount
                accent: Backend.alarmCount > 0 ? Theme.statusColors[2] : Theme.statusColors[1]
                detail: "离线 " + Backend.offlineCount + " · 累计告警事件 " + Backend.alarmEvents
            }
            KpiCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "全网吞吐"
                value: (Backend.totalInMbps + Backend.totalOutMbps) / 1000
                decimals: 2
                unit: "Gbps"
                accent: Theme.accent2
                detail: "入 " + Backend.totalInMbps.toFixed(0) + " · 出 " + Backend.totalOutMbps.toFixed(0) + " Mbps"
            }
            KpiCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "通信速率"
                value: Backend.framesPerSec
                unit: "帧/秒"
                accent: "#2dd4bf"
                detail: "接收 " + Backend.rxKBps.toFixed(1) + " KB/s · CRC 错误 " + Backend.crcErrors
            }
        }

        // ---------------------------------------------------------- 中部三栏
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 5
            spacing: Theme.px(12)

            Panel {
                Layout.preferredWidth: root.width * 0.24
                Layout.fillHeight: true
                title: "运行状态"
                subtitle: "全部拓扑节点"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.px(10)

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.verticalStretchFactor: 3
                        spacing: Theme.px(12)

                        StatusDonut {
                            Layout.fillHeight: true
                            Layout.preferredWidth: height
                            Layout.maximumWidth: parent.width * 0.6
                            counts: [Backend.normalCount, Backend.warningCount, Backend.alarmCount, Backend.offlineCount]
                        }
                        Column {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: true
                            spacing: Theme.px(8)
                            Repeater {
                                model: 4
                                Row {
                                    id: legendRow
                                    required property int index
                                    spacing: Theme.px(6)
                                    Rectangle {
                                        width: Theme.px(10); height: width; radius: 2
                                        color: Theme.statusColor(legendRow.index)
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: Theme.statusNames[legendRow.index]
                                        color: Theme.sub
                                        font.pixelSize: Theme.px(12)
                                    }
                                    Text {
                                        text: [Backend.normalCount, Backend.warningCount,
                                               Backend.alarmCount, Backend.offlineCount][legendRow.index]
                                        color: Theme.text
                                        font.pixelSize: Theme.px(12)
                                        font.bold: true
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.verticalStretchFactor: 2
                        spacing: Theme.px(8)
                        RingGauge {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            value: Backend.avgCpu
                            label: "平均 CPU"
                            unit: "%"
                        }
                        RingGauge {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            value: Backend.avgTemp
                            warn: 70
                            alarm: 80
                            label: "平均温度"
                            unit: "℃"
                            baseColor: "#2dd4bf"
                            detail: "最高 " + Backend.maxTemp.toFixed(1) + " ℃"
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "全网负载趋势"
                subtitle: "每秒采样 · 近 2 分钟"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.px(12)
                    TrendChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: "CPU 利用率（%）"
                        unit: "%"
                        maximum: 100
                        threshold: 80
                        name1: "平均"
                        name2: "最高"
                        color2: "#fb923c"
                        series1: Backend.cpuTrend
                        series2: Backend.cpuMaxTrend
                        capacity: Backend.trendCapacity
                    }
                    TrendChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: "汇总流量（Mbps）"
                        unit: "Mbps"
                        maximum: Backend.trafficPeak
                        name1: "入"
                        name2: "出"
                        color1: Theme.accent2
                        color2: "#2dd4bf"
                        series1: Backend.inTrend
                        series2: Backend.outTrend
                        capacity: Backend.trendCapacity
                    }
                }
            }

            Panel {
                Layout.preferredWidth: root.width * 0.24
                Layout.fillHeight: true
                title: "实时告警"
                subtitle: "点击定位"
                AlarmFeed {
                    anchors.fill: parent
                }
            }
        }

        // ---------------------------------------------------------- 设备矩阵
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 4
            title: "设备矩阵"
            subtitle: grid.count + " 台 · 点击卡片在拓扑图中定位"
            tools: [
                DashButton {
                    text: "仅看异常"
                    checkable: true
                    checked: Backend.abnormalOnly
                    onClicked: Backend.abnormalOnly = checked
                }
            ]

            GridView {
                id: grid

                readonly property int gap: Theme.px(10)
                readonly property int columns: Math.max(1, Math.floor(width / Theme.px(210)))

                anchors.fill: parent
                clip: true
                cellWidth: width / columns
                cellHeight: Theme.px(118)
                boundsBehavior: Flickable.StopAtBounds
                model: Backend.devices

                // DeviceCard 的 required 属性按名字从模型角色自动填充
                delegate: DeviceCard {
                    width: grid.cellWidth - grid.gap
                    height: grid.cellHeight - grid.gap
                }

                // 自绘滚动条（不引入 Qt Quick Controls，WASM 包更小）
                Rectangle {
                    visible: grid.visibleArea.heightRatio < 1
                    anchors.right: parent.right
                    y: grid.visibleArea.yPosition * grid.height
                    width: Theme.px(4)
                    height: grid.visibleArea.heightRatio * grid.height
                    radius: width / 2
                    color: Theme.panelEdge
                }

                Column {
                    anchors.centerIn: parent
                    visible: grid.count === 0
                    spacing: Theme.px(12)
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Backend.abnormalOnly ? "当前没有异常设备" : "暂无设备数据"
                        color: Theme.dim
                        font.pixelSize: Theme.px(14)
                    }
                    DashButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !Backend.linkConnected
                        text: "连接虚拟设备"
                        onClicked: Backend.connectDevices()
                    }
                }
            }
        }
    }
}
