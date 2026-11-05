#include "scene/TopologyScene.h"
#include "commands/Commands.h"
#include "scene/EdgeItem.h"
#include "scene/NodeItem.h"

#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QUndoStack>
#include <QVarLengthArray>
#include <cmath>

TopologyScene::TopologyScene(QUndoStack *undoStack, QObject *parent)
    : QGraphicsScene(parent), m_undo(undoStack), m_blinkTimer(new QTimer(this))
{
    setSceneRect(-30000, -30000, 60000, 60000);
    setItemIndexMethod(BspTreeIndex);
    setBspTreeDepth(0); // 0 = 由 Qt 按图元数量自动选择深度

    // 全局只有一个闪烁定时器，每次只刷新处于告警状态的节点
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, &TopologyScene::onBlink);
    m_blinkTimer->start();
}

TopologyScene::~TopologyScene()
{
    // 在子类部分还有效时删除图元，图元析构会回调 unregisterXxx()
    clearTopology();
}

void TopologyScene::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    if (mode != Mode::Select)
        clearSelection();
    emit modeChanged(mode);
}

QPointF TopologyScene::snapPoint(const QPointF &p) const
{
    return QPointF(std::round(p.x() / kGrid) * kGrid, std::round(p.y() / kGrid) * kGrid);
}

// ---------- 注册表 ----------

void TopologyScene::registerNode(NodeItem *n)
{
    m_nodes.insert(n->id(), n);
    m_nextId = std::max(m_nextId, n->id() + 1);
    ++m_statusCount[int(n->status())];
    if (n->status() == NodeStatus::Alarm)
        m_alarmNodes.insert(n);
}

void TopologyScene::unregisterNode(NodeItem *n)
{
    if (m_nodes.remove(n->id()) == 0)
        return;
    --m_statusCount[int(n->status())];
    m_alarmNodes.remove(n);
    m_dragStart.remove(n);
    if (m_connectFrom == n)
        m_connectFrom = nullptr;
}

void TopologyScene::registerEdge(EdgeItem *e) { m_edges.insert(e); }
void TopologyScene::unregisterEdge(EdgeItem *e) { m_edges.remove(e); }

void TopologyScene::nodeStatusChanged(NodeItem *n, NodeStatus oldStatus)
{
    --m_statusCount[int(oldStatus)];
    ++m_statusCount[int(n->status())];
    if (n->status() == NodeStatus::Alarm)
        m_alarmNodes.insert(n);
    else
        m_alarmNodes.remove(n);
}

void TopologyScene::onBlink()
{
    NodeItem::blinkPhase = !NodeItem::blinkPhase;
    for (NodeItem *n : std::as_const(m_alarmNodes))
        n->update();
}

// ---------- 查询 ----------

QList<NodeItem *> TopologyScene::selectedNodes() const
{
    QList<NodeItem *> out;
    const auto items = selectedItems();
    for (QGraphicsItem *i : items)
        if (i->type() == NodeItem::Type)
            out << static_cast<NodeItem *>(i);
    return out;
}

QList<EdgeItem *> TopologyScene::selectedEdges() const
{
    QList<EdgeItem *> out;
    const auto items = selectedItems();
    for (QGraphicsItem *i : items)
        if (i->type() == EdgeItem::Type)
            out << static_cast<EdgeItem *>(i);
    return out;
}

NodeItem *TopologyScene::nodeAt(const QPointF &pos) const
{
    const auto hits = items(pos, Qt::IntersectsItemShape, Qt::DescendingOrder);
    for (QGraphicsItem *i : hits)
        if (i->type() == NodeItem::Type)
            return static_cast<NodeItem *>(i);
    return nullptr;
}

// ---------- 批量构建 ----------

void TopologyScene::clearTopology()
{
    m_dragStart.clear();
    m_connectFrom = nullptr;
    m_rubberLine = nullptr;
    clear();
    m_nodes.clear();
    m_edges.clear();
    m_alarmNodes.clear();
    m_statusCount.fill(0);
    m_nextId = 1;
}

NodeItem *TopologyScene::createNode(int id, NodeType type, const QString &name, const QPointF &pos)
{
    auto *n = new NodeItem(id, type, name);
    n->setPos(pos);
    addItem(n);
    return n;
}

EdgeItem *TopologyScene::createEdge(NodeItem *a, NodeItem *b, int load)
{
    auto *e = new EdgeItem(a, b, load);
    e->attach();
    addItem(e);
    return e;
}

void TopologyScene::setBspIndexEnabled(bool on)
{
    setItemIndexMethod(on ? BspTreeIndex : NoIndex);
}

// ---------- 绘制 ----------

void TopologyScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, QColor(0xf7, 0xf8, 0xfa));

    // 网格太密时不画，避免缩小后满屏线条既看不清又拖慢速度
    const qreal scale = painter->worldTransform().m11();
    int step = kGrid;
    while (step * scale < 12)
        step *= 5;
    if (step > kGrid * 125)
        return;

    const qreal left = std::floor(rect.left() / step) * step;
    const qreal top = std::floor(rect.top() / step) * step;

    QVarLengthArray<QLineF, 256> lines;
    for (qreal x = left; x < rect.right(); x += step)
        lines.append(QLineF(x, rect.top(), x, rect.bottom()));
    for (qreal y = top; y < rect.bottom(); y += step)
        lines.append(QLineF(rect.left(), y, rect.right(), y));

    painter->setPen(QPen(QColor(0xe3, 0xe6, 0xeb), 0));
    painter->drawLines(lines.constData(), int(lines.size()));
}

// ---------- 交互 ----------

void TopologyScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    switch (m_mode) {
    case Mode::AddNode: {
        if (nodeAt(event->scenePos()))
            break;
        const int id = takeNextId();
        auto *n = new NodeItem(id, m_addType, QStringLiteral("%1-%2").arg(nodeTypeName(m_addType)).arg(id));
        n->setPos(m_snap ? snapPoint(event->scenePos()) : event->scenePos());
        m_undo->push(new AddNodeCommand(this, n));
        return;
    }
    case Mode::Connect: {
        m_connectFrom = nodeAt(event->scenePos());
        if (m_connectFrom) {
            m_rubberLine = new QGraphicsLineItem(QLineF(m_connectFrom->pos(), event->scenePos()));
            m_rubberLine->setPen(QPen(QColor(0x1e, 0x6f, 0xd9), 2, Qt::DashLine));
            m_rubberLine->setZValue(10);
            addItem(m_rubberLine);
        }
        return;
    }
    case Mode::Select:
        break;
    }

    QGraphicsScene::mousePressEvent(event);

    m_dragStart.clear();
    const auto sel = selectedNodes();
    for (NodeItem *n : sel)
        m_dragStart.insert(n, n->pos());
}

void TopologyScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_mode == Mode::Connect && m_rubberLine && m_connectFrom) {
        m_rubberLine->setLine(QLineF(m_connectFrom->pos(), event->scenePos()));
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void TopologyScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_mode == Mode::Connect && event->button() == Qt::LeftButton) {
        if (m_rubberLine) {
            delete m_rubberLine;
            m_rubberLine = nullptr;
        }
        NodeItem *to = nodeAt(event->scenePos());
        if (m_connectFrom && to && to != m_connectFrom) {
            if (m_connectFrom->isConnectedTo(to))
                emit message(QStringLiteral("这两个节点已经连接"));
            else
                m_undo->push(new AddEdgeCommand(this, new EdgeItem(m_connectFrom, to)));
        }
        m_connectFrom = nullptr;
        return;
    }

    QGraphicsScene::mouseReleaseEvent(event);

    if (m_mode != Mode::Select || m_dragStart.isEmpty())
        return;

    QList<NodeItem *> moved;
    QList<QPointF> from, to;
    for (auto it = m_dragStart.cbegin(); it != m_dragStart.cend(); ++it) {
        if (it.key()->pos() != it.value()) {
            moved << it.key();
            from << it.value();
            to << it.key()->pos();
        }
    }
    m_dragStart.clear();
    if (!moved.isEmpty())
        m_undo->push(new MoveNodesCommand(moved, from, to, QStringLiteral("移动 %1 个节点").arg(moved.size())));
}
