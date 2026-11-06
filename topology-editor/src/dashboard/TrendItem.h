#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

// 折线 + 渐变填充，直接生成 Scene Graph 几何体，不经过 QPainter / Canvas。
// 线条按法线挤出成三角形，两侧各留 1px 透明羽化边做抗锯齿，不依赖 MSAA。
// 软件渲染后端（无 GPU 环境）不支持自定义几何体，自动改用 QSGRenderNode + QPainter 绘制。
// 数据从右侧进入：点数不足 capacity 时右对齐，形成"实时滚动"的效果。
class TrendItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<qreal> values READ values WRITE setValues NOTIFY valuesChanged)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged)
    Q_PROPERTY(qreal minValue READ minValue WRITE setMinValue NOTIFY minValueChanged)
    Q_PROPERTY(qreal maxValue READ maxValue WRITE setMaxValue NOTIFY maxValueChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY lineColorChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY fillColorChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)

public:
    explicit TrendItem(QQuickItem *parent = nullptr);

    QList<qreal> values() const { return m_values; }
    void setValues(const QList<qreal> &v);
    int capacity() const { return m_capacity; }
    void setCapacity(int c);
    qreal minValue() const { return m_min; }
    void setMinValue(qreal v);
    qreal maxValue() const { return m_max; }
    void setMaxValue(qreal v);
    QColor lineColor() const { return m_lineColor; }
    void setLineColor(const QColor &c);
    QColor fillColor() const { return m_fillColor; }
    void setFillColor(const QColor &c);
    qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal w);

signals:
    void valuesChanged();
    void capacityChanged();
    void minValueChanged();
    void maxValueChanged();
    void lineColorChanged();
    void fillColorChanged();
    void lineWidthChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QVector<QPointF> computePoints() const;   // 数据映射到本控件坐标，最多 capacity 个点
    QSGNode *updateSoftwareNode(QSGNode *old);

    template <typename T>
    void setStyle(T &field, const T &value, void (TrendItem::*notify)());

    QList<qreal> m_values;
    int m_capacity = 60;
    qreal m_min = 0;
    qreal m_max = 100;
    QColor m_lineColor = QColor(0x38, 0xbd, 0xf8);
    QColor m_fillColor = QColor(0x38, 0xbd, 0xf8, 70);
    qreal m_lineWidth = 2;
};
