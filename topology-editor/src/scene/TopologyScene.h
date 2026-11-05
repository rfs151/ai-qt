#pragma once

#include "TopologyTypes.h"

#include <QGraphicsScene>
#include <QHash>
#include <QPointF>
#include <QSet>
#include <array>

class NodeItem;
class EdgeItem;
class QUndoStack;
class QGraphicsLineItem;
class QTimer;

class TopologyScene : public QGraphicsScene
{
    Q_OBJECT
public:
    enum class Mode { Select, AddNode, Connect };

    explicit TopologyScene(QUndoStack *undoStack, QObject *parent = nullptr);
    ~TopologyScene() override;

    QUndoStack *undoStack() const { return m_undo; }

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    void setNodeTypeToAdd(NodeType t) { m_addType = t; }

    bool snapToGrid() const { return m_snap; }
    void setSnapToGrid(bool on) { m_snap = on; }
    QPointF snapPoint(const QPointF &p) const;
    static constexpr int kGrid = 20;

    // 注册表：节点/连线进出场景时自动维护，避免频繁遍历 items()
    void registerNode(NodeItem *n);
    void unregisterNode(NodeItem *n);
    void registerEdge(EdgeItem *e);
    void unregisterEdge(EdgeItem *e);
    void nodeStatusChanged(NodeItem *n, NodeStatus oldStatus);

    const QHash<int, NodeItem *> &nodes() const { return m_nodes; }
    const QSet<EdgeItem *> &edges() const { return m_edges; }
    NodeItem *node(int id) const { return m_nodes.value(id); }
    int statusCount(NodeStatus s) const { return m_statusCount[int(s)]; }
    int takeNextId() { return m_nextId++; }

    QList<NodeItem *> selectedNodes() const;
    QList<EdgeItem *> selectedEdges() const;

    // 不经过撤销栈的批量构建接口（加载文件、压力测试用）
    void clearTopology();
    NodeItem *createNode(int id, NodeType type, const QString &name, const QPointF &pos);
    EdgeItem *createEdge(NodeItem *a, NodeItem *b, int load);

    void setBspIndexEnabled(bool on);

signals:
    void modeChanged(TopologyScene::Mode mode);
    void message(const QString &text);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    NodeItem *nodeAt(const QPointF &pos) const;
    void onBlink();

    QUndoStack *m_undo;
    Mode m_mode = Mode::Select;
    NodeType m_addType = NodeType::Server;
    bool m_snap = true;
    int m_nextId = 1;

    QHash<int, NodeItem *> m_nodes;
    QSet<EdgeItem *> m_edges;
    QSet<NodeItem *> m_alarmNodes;
    std::array<int, kNodeStatusCount> m_statusCount{};

    QTimer *m_blinkTimer;

    // 拖动记录：按下时保存选中节点位置，松开时若有变化生成一条移动命令
    QHash<NodeItem *, QPointF> m_dragStart;

    // 连线模式下的临时线
    NodeItem *m_connectFrom = nullptr;
    QGraphicsLineItem *m_rubberLine = nullptr;
};
