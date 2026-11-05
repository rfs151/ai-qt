#pragma once

#include "TopologyTypes.h"

#include <QList>
#include <QPointF>
#include <QUndoCommand>
#include <QVariant>

class TopologyScene;
class NodeItem;
class EdgeItem;

// 所有权规则（避免撤销栈析构时重复释放）：
//   "把图元移出场景"的命令在处于该状态时负责删除图元。
//   - AddXxxCommand：被撤销（图元不在场景）时析构负责 delete
//   - DeleteItemsCommand：已执行（图元不在场景）时析构负责 delete
//   线性历史保证两者不会同时成立。

class AddNodeCommand : public QUndoCommand
{
public:
    AddNodeCommand(TopologyScene *scene, NodeItem *node, QUndoCommand *parent = nullptr);
    ~AddNodeCommand() override;
    void redo() override;
    void undo() override;

private:
    TopologyScene *m_scene;
    NodeItem *m_node;
    bool m_applied = false;
};

class AddEdgeCommand : public QUndoCommand
{
public:
    AddEdgeCommand(TopologyScene *scene, EdgeItem *edge, QUndoCommand *parent = nullptr);
    ~AddEdgeCommand() override;
    void redo() override;
    void undo() override;

private:
    TopologyScene *m_scene;
    EdgeItem *m_edge;
    bool m_applied = false;
};

// 删除节点时连带删除其所有连线，撤销时完整恢复
class DeleteItemsCommand : public QUndoCommand
{
public:
    DeleteItemsCommand(TopologyScene *scene, const QList<NodeItem *> &nodes, const QList<EdgeItem *> &edges,
                       QUndoCommand *parent = nullptr);
    ~DeleteItemsCommand() override;
    void redo() override;
    void undo() override;

private:
    TopologyScene *m_scene;
    QList<NodeItem *> m_nodes;
    QList<EdgeItem *> m_edges;
    bool m_applied = false;
};

class MoveNodesCommand : public QUndoCommand
{
public:
    enum { Id = 1001 };
    MoveNodesCommand(const QList<NodeItem *> &nodes, const QList<QPointF> &from, const QList<QPointF> &to,
                     const QString &text, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return m_mergeable ? Id : -1; }
    bool mergeWith(const QUndoCommand *other) override;
    void setMergeable(bool on) { m_mergeable = on; }

private:
    QList<NodeItem *> m_nodes;
    QList<QPointF> m_from;
    QList<QPointF> m_to;
    bool m_mergeable = false;
};

class SetNodePropertyCommand : public QUndoCommand
{
public:
    enum class Property { Name, Type, Ip, Status, X, Y };
    enum { Id = 1002 };

    SetNodePropertyCommand(NodeItem *node, Property prop, const QVariant &value, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return Id; }
    bool mergeWith(const QUndoCommand *other) override;

    static QVariant read(const NodeItem *node, Property prop);
    static void write(NodeItem *node, Property prop, const QVariant &value);

private:
    NodeItem *m_node;
    Property m_prop;
    QVariant m_old;
    QVariant m_new;
};
