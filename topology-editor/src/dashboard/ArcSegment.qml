import QtQuick
import QtQuick.Shapes

// 一段圆弧。CurveRenderer 在 GPU 上解析绘制曲线，自带抗锯齿，缩放后依然清晰。
//
// 绕开 Qt 6.8 CurveRenderer 的两个问题（WebGL 下实测）：
// 1. 首帧之前（布局还在计算尺寸时）路径属性的变化会丢失，圆弧停留在尺寸为 0 时的样子。
//    所以路径画在固定的 200×200 坐标系里，线宽也用这个坐标系的单位，整体用 scale 缩放，
//    路径几何与控件尺寸完全无关。
// 2. 单段弧超过 180 度时绘制出错，拆成两段首尾相接。
Item {
    id: arc

    property real thickness: 10               // 线宽，单位是直径 200 的坐标系，随控件一起缩放
    property color color: Theme.accent
    property real startAngle: 0               // 0 度在三点钟方向，顺时针
    property real sweepAngle: 90
    property bool roundCap: false

    readonly property real k: Math.max(0.001, Math.min(width, height) / 200)

    Shape {
        width: 200
        height: 200
        anchors.centerIn: parent
        scale: arc.k
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            id: path
            readonly property real r: 100 - arc.thickness / 2

            strokeColor: arc.color
            strokeWidth: arc.thickness
            fillColor: "transparent"
            capStyle: arc.roundCap ? ShapePath.RoundCap : ShapePath.FlatCap

            PathAngleArc {
                centerX: 100
                centerY: 100
                radiusX: path.r
                radiusY: path.r
                startAngle: arc.startAngle
                sweepAngle: arc.sweepAngle / 2
            }
            PathAngleArc {
                moveToStart: false
                centerX: 100
                centerY: 100
                radiusX: path.r
                radiusY: path.r
                startAngle: arc.startAngle + arc.sweepAngle / 2
                sweepAngle: arc.sweepAngle / 2
            }
        }
    }
}
