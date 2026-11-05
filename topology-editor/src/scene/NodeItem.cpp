#include "scene/NodeItem.h"
#include "scene/EdgeItem.h"
#include "scene/TopologyScene.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

bool NodeItem::lodEnabled = true;
bool NodeItem::blinkPhase = true;

namespace {
constexpr qreal kH = NodeItem::kHalfSize;
const QRectF kBody(-kH, -kH, 2 * kH, 2 * kH);
const QRectF kLabel(-60, kH + 2, 120, 16);
// 绘制范围：主体 + 文字 + 选中框/状态点的余量（开启了 DontAdjustForAntialiasing，需要自己留余量）
const QRectF kBounds = kBody.united(kLabel).adjusted(-4, -4, 4, 4);
} // namespace

NodeItem::NodeItem(int id, NodeType type, const QString &name)
    : m_id(id), m_type(type), m_name(name)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(1);
}

NodeItem::~NodeItem()
{
    // 析构时 QGraphicsItem 不会再回调 itemChange，这里主动从场景注册表里注销
    if (auto *s = static_cast<TopologyScene *>(scene()))
        s->unregisterNode(this);
}

QRectF NodeItem::boundingRect() const { return kBounds; }

QPainterPath NodeItem::shape() const
{
    QPainterPath p;
    p.addRect(kBody);
    return p;
}

void NodeItem::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    update();
}

void NodeItem::setNodeType(NodeType type)
{
    if (m_type == type)
        return;
    m_type = type;
    update();
}

void NodeItem::setStatus(NodeStatus status)
{
    if (m_status == status)
        return;
    const NodeStatus old = m_status;
    m_status = status;
    if (auto *s = static_cast<TopologyScene *>(scene()))
        s->nodeStatusChanged(this, old);
    update();
    for (EdgeItem *e : std::as_const(m_edges))
        e->update();
}

void NodeItem::setIp(const QString &ip) { m_ip = ip; }

void NodeItem::addEdge(EdgeItem *edge)
{
    if (!m_edges.contains(edge))
        m_edges.append(edge);
}

void NodeItem::removeEdge(EdgeItem *edge) { m_edges.removeAll(edge); }

bool NodeItem::isConnectedTo(const NodeItem *other) const
{
    for (const EdgeItem *e : m_edges)
        if (e->otherEnd(this) == other)
            return true;
    return false;
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    switch (change) {
    case ItemPositionChange:
        if (auto *s = static_cast<TopologyScene *>(scene()); s && s->snapToGrid())
            return s->snapPoint(value.toPointF());
        break;
    case ItemPositionHasChanged:
        // 增量更新：只调整与本节点相连的连线
        for (EdgeItem *e : std::as_const(m_edges))
            e->adjust();
        break;
    case ItemSceneChange:
        if (auto *s = static_cast<TopologyScene *>(scene()))
            s->unregisterNode(this);
        break;
    case ItemSceneHasChanged:
        if (auto *s = static_cast<TopologyScene *>(scene()))
            s->registerNode(this);
        break;
    default:
        break;
    }
    return QGraphicsItem::itemChange(change, value);
}

void NodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const qreal lod = lodEnabled ? option->levelOfDetailFromTransform(p->worldTransform()) : 1.0;

    QColor color = statusColor(m_status);
    if (m_status == NodeStatus::Alarm && !blinkPhase)
        color = color.lighter(160);

    // LOD 0：缩得很小时只画一个色块，不描边、不抗锯齿、不画文字
    if (lod < 0.2) {
        p->fillRect(kBody, isSelected() ? QColor(0x1e, 0x6f, 0xd9) : color);
        return;
    }

    const bool selected = option->state & QStyle::State_Selected;

    // LOD 1：圆角块 + 边框
    p->setPen(QPen(selected ? QColor(0x1e, 0x6f, 0xd9) : color.darker(130), selected ? 3 : 1.5));
    p->setBrush(QColor(0xff, 0xff, 0xff));
    p->drawRoundedRect(kBody, 8, 8);

    if (lod < 0.5) {
        p->fillRect(kBody.adjusted(6, 6, -6, -6), color);
        return;
    }

    // LOD 2：完整绘制图标、状态点和名称
    paintIcon(p, kBody.adjusted(9, 9, -9, -9));

    p->setPen(Qt::white);
    p->setBrush(color);
    p->drawEllipse(QPointF(kH - 5, -kH + 5), 6, 6);

    p->setPen(QColor(0x22, 0x26, 0x2e));
    QFont f = p->font();
    f.setPixelSize(12);
    p->setFont(f);
    p->drawText(kLabel, Qt::AlignHCenter | Qt::AlignTop, m_name);
}

void NodeItem::paintIcon(QPainter *p, const QRectF &r) const
{
    const QColor ink(0x2f, 0x3b, 0x4c);
    p->setPen(QPen(ink, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    const QPointF c = r.center();
    const qreal w = r.width(), h = r.height();

    switch (m_type) {
    case NodeType::Router: {
        p->drawEllipse(r.adjusted(2, 6, -2, -6));
        // 四个方向的箭头
        p->drawLine(QPointF(c.x() - w * 0.25, c.y()), QPointF(c.x() + w * 0.25, c.y()));
        p->drawLine(QPointF(c.x(), c.y() - h * 0.12), QPointF(c.x(), c.y() + h * 0.12));
        break;
    }
    case NodeType::Switch: {
        p->drawRoundedRect(r.adjusted(0, h * 0.25, 0, -h * 0.25), 3, 3);
        for (int i = 0; i < 4; ++i) {
            const qreal x = r.left() + w * (0.2 + 0.2 * i);
            p->drawPoint(QPointF(x, c.y()));
        }
        break;
    }
    case NodeType::Server: {
        for (int i = 0; i < 3; ++i) {
            QRectF slot(r.left() + 3, r.top() + i * h / 3 + 1, w - 6, h / 3 - 3);
            p->drawRect(slot);
            p->drawPoint(QPointF(slot.right() - 4, slot.center().y()));
        }
        break;
    }
    case NodeType::Firewall: {
        p->drawRect(r.adjusted(1, 3, -1, -3));
        const qreal top = r.top() + 3, bottom = r.bottom() - 3;
        const qreal rowH = (bottom - top) / 3;
        for (int row = 1; row < 3; ++row)
            p->drawLine(QPointF(r.left() + 1, top + row * rowH), QPointF(r.right() - 1, top + row * rowH));
        for (int row = 0; row < 3; ++row) {
            const qreal x = (row % 2) ? c.x() - w * 0.2 : c.x() + w * 0.15;
            p->drawLine(QPointF(x, top + row * rowH), QPointF(x, top + (row + 1) * rowH));
        }
        break;
    }
    case NodeType::Workstation: {
        p->drawRoundedRect(QRectF(r.left() + 2, r.top() + 2, w - 4, h * 0.6), 2, 2);
        p->drawLine(QPointF(c.x(), r.top() + 2 + h * 0.6), QPointF(c.x(), r.bottom() - 4));
        p->drawLine(QPointF(c.x() - w * 0.25, r.bottom() - 3), QPointF(c.x() + w * 0.25, r.bottom() - 3));
        break;
    }
    case NodeType::Cloud: {
        QPainterPath path;
        path.addEllipse(QRectF(r.left(), c.y() - h * 0.05, w * 0.45, h * 0.35));
        path.addEllipse(QRectF(r.left() + w * 0.2, c.y() - h * 0.3, w * 0.5, h * 0.5));
        path.addEllipse(QRectF(r.left() + w * 0.5, c.y() - h * 0.1, w * 0.5, h * 0.4));
        p->drawPath(path.simplified());
        break;
    }
    }
}
