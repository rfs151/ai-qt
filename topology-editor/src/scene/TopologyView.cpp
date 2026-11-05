#include "scene/TopologyView.h"
#include "commands/Commands.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

#include <QKeyEvent>
#include <QScrollBar>
#include <QUndoStack>
#include <QWheelEvent>
#include <cmath>

namespace {
constexpr qreal kMinZoom = 0.01;
constexpr qreal kMaxZoom = 6.0;
} // namespace

TopologyView::TopologyView(TopologyScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent), m_scene(scene)
{
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    // 只重绘变化区域的包围盒；图元自己保证 paint 前设置好画笔，省去 save/restore
    setViewportUpdateMode(SmartViewportUpdate);
    setOptimizationFlags(DontSavePainterState | DontAdjustForAntialiasing);
    setCacheMode(CacheBackground);
    setFocusPolicy(Qt::StrongFocus);

    connect(m_scene, &TopologyScene::modeChanged, this, &TopologyView::updateDragMode);
    updateDragMode();
}

void TopologyView::updateDragMode()
{
    const bool select = m_scene->mode() == TopologyScene::Mode::Select;
    setDragMode(select ? RubberBandDrag : NoDrag);
    viewport()->setCursor(select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void TopologyView::zoomBy(qreal factor)
{
    const qreal target = std::clamp(zoom() * factor, kMinZoom, kMaxZoom);
    factor = target / zoom();
    if (qFuzzyCompare(factor, 1.0))
        return;
    scale(factor, factor);
    emit zoomChanged(zoom());
    emit viewportChanged();
}

void TopologyView::fitAll()
{
    QRectF r = m_scene->itemsBoundingRect();
    if (r.isEmpty())
        r = QRectF(-400, -300, 800, 600);
    fitInView(r.adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    // fitInView 可能超出缩放范围，这里修正一下
    if (zoom() > 1.5)
        setTransform(QTransform::fromScale(1.5, 1.5));
    centerOn(r.center());
    emit zoomChanged(zoom());
    emit viewportChanged();
}

void TopologyView::resetZoom()
{
    setTransform(QTransform());
    emit zoomChanged(zoom());
    emit viewportChanged();
}

void TopologyView::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    zoomBy(std::pow(1.0015, delta));
    event->accept();
}

void TopologyView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && event->modifiers() & Qt::AltModifier)) {
        m_panning = true;
        m_lastPanPos = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void TopologyView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint p = event->position().toPoint();
        const QPoint d = p - m_lastPanPos;
        m_lastPanPos = p;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void TopologyView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_panning = false;
        updateDragMode();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void TopologyView::keyPressEvent(QKeyEvent *event)
{
    // 方向键微调选中节点；连续按键在撤销栈中合并成一条命令
    QPointF delta;
    const qreal step = (event->modifiers() & Qt::ShiftModifier) ? TopologyScene::kGrid * 5 : TopologyScene::kGrid;
    switch (event->key()) {
    case Qt::Key_Left:  delta = {-step, 0}; break;
    case Qt::Key_Right: delta = {step, 0}; break;
    case Qt::Key_Up:    delta = {0, -step}; break;
    case Qt::Key_Down:  delta = {0, step}; break;
    default:
        QGraphicsView::keyPressEvent(event);
        return;
    }
    const auto nodes = m_scene->selectedNodes();
    if (nodes.isEmpty()) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    QList<QPointF> from, to;
    for (NodeItem *n : nodes) {
        from << n->pos();
        to << n->pos() + delta;
    }
    auto *cmd = new MoveNodesCommand(nodes, from, to, QStringLiteral("方向键移动 %1 个节点").arg(nodes.size()));
    cmd->setMergeable(true);
    m_scene->undoStack()->push(cmd);
}

void TopologyView::paintEvent(QPaintEvent *event)
{
    QElapsedTimer t;
    t.start();
    QGraphicsView::paintEvent(event);
    const double ms = t.nsecsElapsed() / 1e6;
    m_avgFrameMs = m_avgFrameMs == 0.0 ? ms : m_avgFrameMs * 0.8 + ms * 0.2;
}

void TopologyView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewportChanged();
}
