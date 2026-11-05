#pragma once

#include "comm/CommTypes.h"
#include "comm/TelemetryStore.h"
#include "model/AlarmLogModel.h"

#include <QHash>
#include <QObject>

class TopologyScene;
class QTimer;

// 界面线程：接收采样批次，写入历史缓存，按阈值（带回差）判定设备状态，
// 驱动拓扑图上的节点颜色和链路负载，并产生告警事件。
class DeviceMonitor : public QObject
{
    Q_OBJECT
public:
    struct Thresholds
    {
        float cpuWarn = 80;
        float cpuAlarm = 95;
        float tempWarn = 70;
        float tempAlarm = 80;
        float hysteresis = 5;   // 回差：降级要比阈值再低这么多，避免在阈值附近来回跳
        qint64 offlineMs = 3000;
    };

    explicit DeviceMonitor(TopologyScene *scene, QObject *parent = nullptr);

    TelemetryStore *store() { return &m_store; }
    const Thresholds &thresholds() const { return m_th; }
    const Sample *latest(int deviceId) const;
    int onlineCount() const;

    void onSamples(const QVector<Sample> &batch);
    // 拓扑重建或用户主动断开：清空状态。restoreNormal 为 true 时把受监控的节点恢复为正常
    void reset(bool restoreNormal);

signals:
    void alarmEvents(const QVector<AlarmEvent> &events);

private:
    struct DevState
    {
        Sample last;
        qint64 lastSeen = 0;
        bool online = true;
    };

    NodeStatus evaluate(const Sample &s, NodeStatus current, QString *reason) const;
    void checkOffline();

    TopologyScene *m_scene;
    TelemetryStore m_store;
    Thresholds m_th;
    QHash<int, DevState> m_devs;
    QTimer *m_offlineTimer;
};
