#pragma once

#include "TopologyTypes.h"

#include <QGraphicsItem>
#include <QList>

class EdgeItem;

// 拓扑节点。不继承 QObject（QGraphicsObject），一万个节点时能省下可观的内存。
class NodeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };

    NodeItem(int id, NodeType type, const QString &name);
    ~NodeItem() override;

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    int id() const { return m_id; }

    QString name() const { return m_name; }
    void setName(const QString &name);

    NodeType nodeType() const { return m_type; }
    void setNodeType(NodeType type);

    NodeStatus status() const { return m_status; }
    void setStatus(NodeStatus status);

    QString ip() const { return m_ip; }
    void setIp(const QString &ip);

    const QList<EdgeItem *> &edges() const { return m_edges; }
    void addEdge(EdgeItem *edge);
    void removeEdge(EdgeItem *edge);
    bool isConnectedTo(const NodeItem *other) const;

    // 全局开关：用于在界面上对比开/关 LOD 的渲染耗时
    static bool lodEnabled;
    // 告警闪烁相位，由场景里唯一的定时器翻转
    static bool blinkPhase;

    static constexpr qreal kHalfSize = 26.0;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void paintIcon(QPainter *p, const QRectF &r) const;

    int m_id;
    NodeType m_type;
    NodeStatus m_status = NodeStatus::Normal;
    QString m_name;
    QString m_ip;
    QList<EdgeItem *> m_edges;
};
