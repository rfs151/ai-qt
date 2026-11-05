#include "commands/Commands.h"
#include "scene/EdgeItem.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

// ---------- AddNodeCommand ----------

AddNodeCommand::AddNodeCommand(TopologyScene *scene, NodeItem *node, QUndoCommand *parent)
    : QUndoCommand(parent), m_scene(scene), m_node(node)
{
    setText(QStringLiteral("添加节点 %1").arg(node->name()));
}

AddNodeCommand::~AddNodeCommand()
{
    if (!m_applied && !m_node->scene())
        delete m_node;
}

void AddNodeCommand::redo()
{
    m_scene->addItem(m_node);
    m_applied = true;
}

void AddNodeCommand::undo()
{
    m_scene->removeItem(m_node);
    m_applied = false;
}

// ---------- AddEdgeCommand ----------

AddEdgeCommand::AddEdgeCommand(TopologyScene *scene, EdgeItem *edge, QUndoCommand *parent)
    : QUndoCommand(parent), m_scene(scene), m_edge(edge)
{
    setText(QStringLiteral("连接 %1 → %2").arg(edge->source()->name(), edge->target()->name()));
}

AddEdgeCommand::~AddEdgeCommand()
{
    if (!m_applied && !m_edge->scene())
        delete m_edge;
}

void AddEdgeCommand::redo()
{
    m_edge->attach();
    m_scene->addItem(m_edge);
    m_applied = true;
}

void AddEdgeCommand::undo()
{
    m_edge->detach();
    m_scene->removeItem(m_edge);
    m_applied = false;
}

// ---------- DeleteItemsCommand ----------

DeleteItemsCommand::DeleteItemsCommand(TopologyScene *scene, const QList<NodeItem *> &nodes,
                                       const QList<EdgeItem *> &edges, QUndoCommand *parent)
    : QUndoCommand(parent), m_scene(scene), m_nodes(nodes), m_edges(edges)
{
    // 收集被删节点上的所有连线
    for (NodeItem *n : nodes)
        for (EdgeItem *e : n->edges())
            if (!m_edges.contains(e))
                m_edges << e;
    setText(QStringLiteral("删除 %1 个节点、%2 条连线").arg(m_nodes.size()).arg(m_edges.size()));
}

DeleteItemsCommand::~DeleteItemsCommand()
{
    if (!m_applied)
        return;
    // 先删连线再删节点；连线析构不会访问节点
    qDeleteAll(m_edges);
    qDeleteAll(m_nodes);
}

void DeleteItemsCommand::redo()
{
    for (EdgeItem *e : std::as_const(m_edges)) {
        e->detach();
        m_scene->removeItem(e);
    }
    for (NodeItem *n : std::as_const(m_nodes))
        m_scene->removeItem(n);
    m_applied = true;
}

void DeleteItemsCommand::undo()
{
    for (NodeItem *n : std::as_const(m_nodes))
        m_scene->addItem(n);
    for (EdgeItem *e : std::as_const(m_edges)) {
        e->attach();
        m_scene->addItem(e);
    }
    m_applied = false;
}

// ---------- MoveNodesCommand ----------

MoveNodesCommand::MoveNodesCommand(const QList<NodeItem *> &nodes, const QList<QPointF> &from,
                                   const QList<QPointF> &to, const QString &text, QUndoCommand *parent)
    : QUndoCommand(text, parent), m_nodes(nodes), m_from(from), m_to(to)
{
}

void MoveNodesCommand::redo()
{
    for (int i = 0; i < m_nodes.size(); ++i)
        m_nodes[i]->setPos(m_to[i]);
}

void MoveNodesCommand::undo()
{
    for (int i = 0; i < m_nodes.size(); ++i)
        m_nodes[i]->setPos(m_from[i]);
}

bool MoveNodesCommand::mergeWith(const QUndoCommand *other)
{
    // 只合并同一组节点的连续方向键移动
    auto *o = static_cast<const MoveNodesCommand *>(other);
    if (o->m_nodes != m_nodes)
        return false;
    m_to = o->m_to;
    return true;
}

// ---------- SetNodePropertyCommand ----------

SetNodePropertyCommand::SetNodePropertyCommand(NodeItem *node, Property prop, const QVariant &value,
                                               QUndoCommand *parent)
    : QUndoCommand(parent), m_node(node), m_prop(prop), m_old(read(node, prop)), m_new(value)
{
    static const char *const names[] = {"名称", "类型", "IP", "状态", "X", "Y"};
    setText(QStringLiteral("修改 %1 的%2").arg(node->name(), QString::fromUtf8(names[int(prop)])));
}

void SetNodePropertyCommand::redo() { write(m_node, m_prop, m_new); }
void SetNodePropertyCommand::undo() { write(m_node, m_prop, m_old); }

bool SetNodePropertyCommand::mergeWith(const QUndoCommand *other)
{
    auto *o = static_cast<const SetNodePropertyCommand *>(other);
    if (o->m_node != m_node || o->m_prop != m_prop)
        return false;
    m_new = o->m_new;
    return true;
}

QVariant SetNodePropertyCommand::read(const NodeItem *n, Property prop)
{
    switch (prop) {
    case Property::Name:   return n->name();
    case Property::Type:   return int(n->nodeType());
    case Property::Ip:     return n->ip();
    case Property::Status: return int(n->status());
    case Property::X:      return n->pos().x();
    case Property::Y:      return n->pos().y();
    }
    return {};
}

void SetNodePropertyCommand::write(NodeItem *n, Property prop, const QVariant &v)
{
    switch (prop) {
    case Property::Name:   n->setName(v.toString()); break;
    case Property::Type:   n->setNodeType(NodeType(v.toInt())); break;
    case Property::Ip:     n->setIp(v.toString()); break;
    case Property::Status: n->setStatus(NodeStatus(v.toInt())); break;
    case Property::X:      n->setX(v.toDouble()); break;
    case Property::Y:      n->setY(v.toDouble()); break;
    }
}
