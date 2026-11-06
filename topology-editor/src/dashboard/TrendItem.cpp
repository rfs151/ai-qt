#include "dashboard/TrendItem.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGRenderNode>
#include <QSGRendererInterface>
#include <QSGVertexColorMaterial>
#include <QVector2D>
#include <cmath>

namespace {

// QSGVertexColorMaterial 要求预乘 alpha
void setVertex(QSGGeometry::ColoredPoint2D &v, QPointF p, const QColor &c, float alpha)
{
    const float a = float(c.alphaF()) * alpha;
    v.set(float(p.x()), float(p.y()), uchar(c.red() * a), uchar(c.green() * a), uchar(c.blue() * a), uchar(255 * a));
}

QSGGeometryNode *makeNode()
{
    auto *node = new QSGGeometryNode;
    auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0,
                                QSGGeometry::UnsignedShortType);
    node->setGeometry(geo);
    node->setMaterial(new QSGVertexColorMaterial);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    return node;
}

// 软件渲染后端用：同样的折线和渐变，用 QPainter 画
class PainterTrendNode : public QSGRenderNode
{
public:
    explicit PainterTrendNode(QQuickWindow *window) : m_window(window) {}

    void sync(QVector<QPointF> pts, QSizeF size, QColor line, QColor fill, qreal width)
    {
        m_pts = std::move(pts);
        m_size = size;
        m_line = line;
        m_fill = fill;
        m_width = width;
        markDirty(DirtyMaterial);
    }

    void render(const RenderState *state) override
    {
        auto *p = static_cast<QPainter *>(
            m_window->rendererInterface()->getResource(m_window, QSGRendererInterface::PainterResource));
        if (!p || m_pts.size() < 2)
            return;
        p->save();
        // 裁剪区域是窗口坐标，必须在 setTransform 之前设置
        if (const QRegion *clip = state->clipRegion(); clip && !clip->isEmpty())
            p->setClipRegion(*clip, Qt::ReplaceClip);
        p->setTransform(matrix()->toTransform());
        p->setOpacity(inheritedOpacity());
        p->setRenderHint(QPainter::Antialiasing);

        QPainterPath area;
        area.moveTo(m_pts.first().x(), m_size.height());
        for (const QPointF &pt : m_pts)
            area.lineTo(pt);
        area.lineTo(m_pts.last().x(), m_size.height());
        QLinearGradient grad(0, 0, 0, m_size.height());
        grad.setColorAt(0, m_fill);
        grad.setColorAt(1, Qt::transparent);
        p->fillPath(area, grad);

        p->setPen(QPen(m_line, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPolyline(m_pts.constData(), int(m_pts.size()));
        p->restore();
    }

    RenderingFlags flags() const override { return BoundedRectRendering; }
    QRectF rect() const override { return QRectF(QPointF(), m_size); }

private:
    QQuickWindow *m_window;
    QVector<QPointF> m_pts;
    QSizeF m_size;
    QColor m_line, m_fill;
    qreal m_width = 1;
};

} // namespace

TrendItem::TrendItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void TrendItem::setValues(const QList<qreal> &v)
{
    m_values = v;
    emit valuesChanged();
    update();
}

template <typename T>
void TrendItem::setStyle(T &field, const T &value, void (TrendItem::*notify)())
{
    if (field == value)
        return;
    field = value;
    emit (this->*notify)();
    update();
}

void TrendItem::setCapacity(int c) { setStyle(m_capacity, std::max(2, c), &TrendItem::capacityChanged); }
void TrendItem::setMinValue(qreal v) { setStyle(m_min, v, &TrendItem::minValueChanged); }
void TrendItem::setMaxValue(qreal v) { setStyle(m_max, v, &TrendItem::maxValueChanged); }
void TrendItem::setLineColor(const QColor &c) { setStyle(m_lineColor, c, &TrendItem::lineColorChanged); }
void TrendItem::setFillColor(const QColor &c) { setStyle(m_fillColor, c, &TrendItem::fillColorChanged); }
void TrendItem::setLineWidth(qreal w) { setStyle(m_lineWidth, w, &TrendItem::lineWidthChanged); }

void TrendItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}

QVector<QPointF> TrendItem::computePoints() const
{
    // 顶点索引是 16 位，最多 16000 点
    const qsizetype cap = std::min<qsizetype>(std::max(m_capacity, 2), 16000);
    const qsizetype n = std::min(m_values.size(), cap);
    const qreal w = width(), h = height();
    if (n < 2 || w <= 0 || h <= 0)
        return {};

    const qreal pad = m_lineWidth + 1;
    const qreal range = m_max > m_min ? m_max - m_min : 1;
    const qreal dx = w / qreal(cap - 1);
    const qsizetype first = m_values.size() - n;
    QVector<QPointF> pts(n);
    for (qsizetype i = 0; i < n; ++i) {
        const qreal v = std::clamp(m_values[first + i], m_min, m_max);
        pts[i] = QPointF(w - qreal(n - 1 - i) * dx, h - pad - (v - m_min) / range * (h - 2 * pad));
    }
    return pts;
}

QSGNode *TrendItem::updateSoftwareNode(QSGNode *old)
{
    auto *node = static_cast<PainterTrendNode *>(old);
    if (!node)
        node = new PainterTrendNode(window());
    node->sync(computePoints(), size(), m_lineColor, m_fillColor, m_lineWidth);
    return node;
}

QSGNode *TrendItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        return updateSoftwareNode(old);

    QSGNode *root = old;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(makeNode());   // 填充
        root->appendChildNode(makeNode());   // 线条
    }
    auto *fillNode = static_cast<QSGGeometryNode *>(root->firstChild());
    auto *lineNode = static_cast<QSGGeometryNode *>(root->lastChild());
    QSGGeometry *fill = fillNode->geometry();
    QSGGeometry *line = lineNode->geometry();

    const QVector<QPointF> pts = computePoints();
    const qsizetype n = pts.size();
    const qreal h = height();
    if (n < 2) {
        fill->allocate(0, 0);
        line->allocate(0, 0);
        fillNode->markDirty(QSGNode::DirtyGeometry);
        lineNode->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    // 填充：三角带，曲线处是填充色，底边透明，形成竖直渐变
    fill->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    fill->allocate(int(2 * n), 0);
    auto *fv = fill->vertexDataAsColoredPoint2D();
    for (qsizetype i = 0; i < n; ++i) {
        setVertex(fv[2 * i], pts[i], m_fillColor, 1);
        setVertex(fv[2 * i + 1], QPointF(pts[i].x(), h), m_fillColor, 0);
    }

    // 线条：每个点沿法线挤出 4 个顶点（外羽化、内、内、外羽化），相邻点之间 3 个四边形
    line->setDrawingMode(QSGGeometry::DrawTriangles);
    line->allocate(int(4 * n), int(18 * (n - 1)));
    auto *lv = line->vertexDataAsColoredPoint2D();
    const float half = float(m_lineWidth) / 2;
    const float feather = 1.0f;
    for (qsizetype i = 0; i < n; ++i) {
        const QVector2D prev(pts[std::max<qsizetype>(i - 1, 0)]);
        const QVector2D next(pts[std::min(i + 1, n - 1)]);
        const QVector2D tangent = (next - prev).normalized();
        QVector2D normal(-tangent.y(), tangent.x());
        // 斜接：拐角处按与相邻线段法线夹角放大，避免线变细；限制倍数防止尖角过长
        const QVector2D seg = (i + 1 < n ? next - QVector2D(pts[i]) : QVector2D(pts[i]) - prev).normalized();
        const float cosA = std::abs(QVector2D::dotProduct(normal, QVector2D(-seg.y(), seg.x())));
        const float miter = 1.0f / std::max(cosA, 0.4f);
        normal *= miter;
        const QPointF p = pts[i];
        const QPointF nOuter = (normal * (half + feather)).toPointF();
        const QPointF nInner = (normal * half).toPointF();
        setVertex(lv[4 * i + 0], p + nOuter, m_lineColor, 0);
        setVertex(lv[4 * i + 1], p + nInner, m_lineColor, 1);
        setVertex(lv[4 * i + 2], p - nInner, m_lineColor, 1);
        setVertex(lv[4 * i + 3], p - nOuter, m_lineColor, 0);
    }
    quint16 *idx = line->indexDataAsUShort();
    for (qsizetype i = 0; i + 1 < n; ++i) {
        for (int k = 0; k < 3; ++k) {
            const auto a = quint16(4 * i + k), b = quint16(a + 1);
            const auto c = quint16(4 * (i + 1) + k), d = quint16(c + 1);
            *idx++ = a; *idx++ = b; *idx++ = c;
            *idx++ = b; *idx++ = d; *idx++ = c;
        }
    }

    fillNode->markDirty(QSGNode::DirtyGeometry);
    lineNode->markDirty(QSGNode::DirtyGeometry);
    return root;
}
