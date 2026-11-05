#pragma once

#include "comm/TelemetryStore.h"

#include <QColor>
#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QTimer;

// QPainter 自绘的多通道实时曲线：
//   - 多个纵向分区（lane）共用时间轴，每个分区有自己的量程和单位；
//   - min/max 降采样，几十万点也只画几千个点；
//   - 实时滚动 / 暂停后滚轮缩放、拖动平移，双击回到实时；
//   - 十字游标读数，点击图例隐藏/显示通道；
//   - 刷新由 30Hz 定时器驱动，只在数据变化或实时滚动时重绘。
class RealtimeChart : public QWidget
{
    Q_OBJECT
public:
    struct Channel
    {
        int index;   // TelemetryPoint::v 中的下标
        QString name;
        QColor color;
    };
    struct Lane
    {
        QString unit;
        QVector<Channel> channels;
        double yMin = 0;
        double yMax = 100;
        bool autoScale = false;
        QVector<double> markers;   // 水平参考线（比如告警阈值）
    };

    explicit RealtimeChart(QWidget *parent = nullptr);

    void setLanes(const QVector<Lane> &lanes);
    const QVector<Lane> &lanes() const { return m_lanes; }
    void setStore(const TelemetryStore *store) { m_store = store; }
    void setSeries(int deviceId, const QString &title);
    int deviceId() const { return m_deviceId; }

    void setSpanSeconds(double seconds);   // <= 0 表示显示全部
    bool isPaused() const { return m_paused; }
    void setPaused(bool paused);

    // 历史回放模式：关闭实时滚动，固定显示 [t0, t1]，双击回到这个范围
    void setLiveEnabled(bool on) { m_liveEnabled = on; }
    void showRange(qint64 t0, qint64 t1);
    void setEmptyText(const QString &text) { m_emptyText = text; }

    QSize sizeHint() const override { return {640, 260}; }

signals:
    void pausedChanged(bool paused);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct View
    {
        qint64 t0;
        qint64 t1;
    };
    View currentView(const TelemetrySeries *s) const;
    QRect plotArea() const;
    QRect laneRect(int lane) const;
    void freezeAt(const View &v);
    void onFrame();

    QVector<Lane> m_lanes;
    const TelemetryStore *m_store = nullptr;
    int m_deviceId = 0;
    QString m_title;

    double m_spanMs = 60000;   // <= 0 全部
    bool m_paused = false;
    bool m_liveEnabled = true;
    View m_home{0, 0};
    QString m_emptyText = QStringLiteral("暂无数据：连接链路后，在拓扑图中选择一个设备");
    qint64 m_viewEnd = 0;      // 暂停时的右边界
    double m_pausedSpanMs = 0; // 暂停时的可见跨度

    QVector<bool> m_hidden;    // 按 Channel::index
    QVector<QPair<QRect, int>> m_legendHits;

    int m_cursorX = -1;
    int m_dragStartX = -1;
    qint64 m_dragStartEnd = 0;

    QTimer *m_frameTimer;
    quint64 m_drawnVersion = 0;
    double m_paintMs = 0;
    qsizetype m_lastIn = 0;
    qsizetype m_lastOut = 0;
};
