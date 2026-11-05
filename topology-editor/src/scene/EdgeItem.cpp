#include "scene/EdgeItem.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

namespace {
constexpr qreal kMargin = 5.0;

QColor loadColor(int load)
{
    if (load >= 85) return QColor(0xd9, 0x30, 0x25);
    if (load >= 60) return QColor(0xe0, 0x9b, 0x10);
    return QColor(0x5b, 0x8d, 0xb8);
}
} // namespace

EdgeItem::EdgeItem(NodeItem *source, NodeItem *target, int load)
    : m_source(source), m_target(target), m_load(load)
{
    setFlags(ItemIsSelectable);
    setZValue(0);
    adjust();
}

EdgeItem::~EdgeItem()
{
    // 注意：这里不能访问两端节点，撤销栈析构时节点可能已先被删除。
    // 连线离开场景时一定已经 detach()，所以无需处理。
    if (auto *s = static_cast<TopologyScene *>(scene()))
        s->unregisterEdge(this);
}

void EdgeItem::attach()
{
    m_source->addEdge(this);
    m_target->addEdge(this);
    adjust();
}

void EdgeItem::detach()
{
    m_source->removeEdge(this);
    m_target->removeEdge(this);
}

void EdgeItem::setLoad(int load)
{
    load = qBound(0, load, 100);
    if (load == m_load)
        return;
    m_load = load;
    update();
}

void EdgeItem::adjust()
{
    const QLineF line(m_source->pos(), m_target->pos());
    if (line == m_line)
        return;
    prepareGeometryChange();
    m_line = line;
}

QRectF EdgeItem::boundingRect() const
{
    return QRectF(m_line.p1(), m_line.p2()).normalized().adjusted(-kMargin, -kMargin, kMargin, kMargin);
}

QPainterPath EdgeItem::shape() const
{
    QPainterPath path(m_line.p1());
    path.lineTo(m_line.p2());
    QPainterPathStroker stroker;
    stroker.setWidth(8);
    return stroker.createStroke(path);
}

QVariant EdgeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSceneChange) {
        if (auto *s = static_cast<TopologyScene *>(scene()))
            s->unregisterEdge(this);
    } else if (change == ItemSceneHasChanged) {
        if (auto *s = static_cast<TopologyScene *>(scene()))
            s->registerEdge(this);
    }
    return QGraphicsItem::itemChange(change, value);
}

void EdgeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const qreal lod = NodeItem::lodEnabled ? option->levelOfDetailFromTransform(p->worldTransform()) : 1.0;
    const bool selected = option->state & QStyle::State_Selected;
    const bool down = m_source->status() == NodeStatus::Offline || m_target->status() == NodeStatus::Offline;

    QColor color = selected ? QColor(0x1e, 0x6f, 0xd9) : loadColor(m_load);
    if (down)
        color = QColor(0xb0, 0xb4, 0xba);

    if (lod < 0.2) {
        // 远景：宽度为 0 的 cosmetic 线，最快的画线方式
        p->setPen(QPen(color, 0));
        p->drawLine(m_line);
        return;
    }

    QPen pen(color, selected ? 3.0 : 1.0 + m_load / 40.0);
    if (down)
        pen.setStyle(Qt::DashLine);
    p->setPen(pen);
    p->drawLine(m_line);
}
