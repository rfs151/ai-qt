#pragma once

#include <QGraphicsView>

class QTimer;

// 鸟瞰图：与主视图共享同一个 scene。
// 不跟随场景实时刷新，而是"标记脏 + 定时刷新"，避免每帧把一万个节点画两遍。
class MiniMapView : public QGraphicsView
{
    Q_OBJECT
public:
    MiniMapView(QGraphicsView *mainView, QWidget *parent = nullptr);

    void markDirty() { m_dirty = true; }
    QSize sizeHint() const override { return {260, 200}; }

protected:
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void refresh();

    QGraphicsView *m_main;
    QTimer *m_timer;
    bool m_dirty = true;
    int m_idleTicks = 0;
};
