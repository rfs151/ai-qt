#pragma once

#include <QGraphicsItem>
#include <QLineF>

class NodeItem;

// 节点之间的连线。是否挂在节点上由 attach()/detach() 控制，
// 这样撤销/重做时可以把连线摘下来保存，而不必销毁重建。
class EdgeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };

    EdgeItem(NodeItem *source, NodeItem *target, int load = 20);
    ~EdgeItem() override;

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    NodeItem *source() const { return m_source; }
    NodeItem *target() const { return m_target; }
    const NodeItem *otherEnd(const NodeItem *n) const { return n == m_source ? m_target : m_source; }

    int load() const { return m_load; }   // 链路负载 0-100
    void setLoad(int load);

    void attach();   // 加入两端节点的连线表
    void detach();   // 从两端节点的连线表移除
    void adjust();   // 节点移动后重新计算几何

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    NodeItem *m_source;
    NodeItem *m_target;
    QLineF m_line;
    int m_load;
};
