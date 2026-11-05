#include "scene/MiniMapView.h"

#include <QMouseEvent>
#include <QTimer>

MiniMapView::MiniMapView(QGraphicsView *mainView, QWidget *parent)
    : QGraphicsView(mainView->scene(), parent), m_main(mainView), m_timer(new QTimer(this))
{
    setInteractive(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setViewportUpdateMode(NoViewportUpdate);   // 场景变化不触发重绘
    setOptimizationFlags(DontSavePainterState | DontAdjustForAntialiasing);
    setRenderHint(QPainter::Antialiasing, false);
    setBackgroundBrush(QColor(0xee, 0xf0, 0xf3));
    setFrameShape(QFrame::NoFrame);
    viewport()->setCursor(Qt::PointingHandCursor);

    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &MiniMapView::refresh);
    m_timer->start();
}

void MiniMapView::refresh()
{
    // 告警状态会持续变化，所以即使没标脏也低频刷新一次
    if (!m_dirty && ++m_idleTicks < 4)
        return;
    m_idleTicks = 0;
    m_dirty = false;
    if (!isVisible())
        return;

    QRectF r = scene()->itemsBoundingRect();
    if (r.isEmpty())
        r = QRectF(-400, -300, 800, 600);
    fitInView(r.adjusted(-60, -60, 60, 60), Qt::KeepAspectRatio);
    viewport()->update();
}

void MiniMapView::drawForeground(QPainter *painter, const QRectF &)
{
    const QPolygonF visible = m_main->mapToScene(m_main->viewport()->rect());
    painter->setPen(QPen(QColor(0x1e, 0x6f, 0xd9), 0));
    painter->setBrush(QColor(0x1e, 0x6f, 0xd9, 40));
    painter->drawPolygon(visible);
}

void MiniMapView::mousePressEvent(QMouseEvent *event)
{
    m_main->centerOn(mapToScene(event->position().toPoint()));
    markDirty();
}

void MiniMapView::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        m_main->centerOn(mapToScene(event->position().toPoint()));
        markDirty();
    }
}

void MiniMapView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    markDirty();
}

void MiniMapView::wheelEvent(QWheelEvent *) {}
