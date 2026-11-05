#pragma once

#include <QElapsedTimer>
#include <QGraphicsView>

class TopologyScene;

class TopologyView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit TopologyView(TopologyScene *scene, QWidget *parent = nullptr);

    qreal zoom() const { return transform().m11(); }
    double averageFrameMs() const { return m_avgFrameMs; }

    void zoomBy(qreal factor);
    void fitAll();
    void resetZoom();

signals:
    void viewportChanged();
    void zoomChanged(qreal zoom);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void updateDragMode();

    TopologyScene *m_scene;
    bool m_panning = false;
    QPoint m_lastPanPos;
    double m_avgFrameMs = 0.0;   // 指数滑动平均的单帧渲染耗时
};
