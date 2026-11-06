pragma Singleton
import QtQuick

// 大屏的颜色、尺寸。s 是按窗口大小算出的缩放系数（以 1600×900 为基准），由 Dashboard 设置
QtObject {
    property real s: 1

    readonly property color bg: "#060c18"
    readonly property color bgGlow: "#0f2547"
    readonly property color panel: "#0b1730"
    readonly property color panelEdge: "#1a3258"
    readonly property color grid: "#13284a"
    readonly property color text: "#e6edf7"
    readonly property color sub: "#8ba0c0"
    readonly property color dim: "#4f6587"
    readonly property color accent: "#38bdf8"
    readonly property color accent2: "#a78bfa"

    readonly property list<color> statusColors: ["#3fb950", "#f2a71b", "#f0524a", "#7d8590"]
    readonly property list<string> statusNames: ["正常", "警告", "告警", "离线"]

    function statusColor(st: int): color {
        return st >= 0 && st < statusColors.length ? statusColors[st] : dim
    }
    function px(v: real): real {
        return Math.round(v * s)
    }
    // 按阈值着色：正常用主色，超过 warn 变黄，超过 alarm 变红
    function levelColor(v: real, warn: real, alarm: real, base: color): color {
        return v >= alarm ? statusColors[2] : v >= warn ? statusColors[1] : base
    }
}
