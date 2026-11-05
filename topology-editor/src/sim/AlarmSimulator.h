#pragma once

#include "model/AlarmLogModel.h"

#include <QObject>
#include <QRandomGenerator>

class TopologyScene;
class QTimer;

// 定时随机改变节点状态和链路负载，模拟监控数据。
// 每个周期的变更攒成一批，一次性提交给告警日志模型。
class AlarmSimulator : public QObject
{
    Q_OBJECT
public:
    AlarmSimulator(TopologyScene *scene, QObject *parent = nullptr);

    bool isRunning() const;
    void start();
    void stop();
    void setIntervalMs(int ms);
    void resetAll();   // 所有节点恢复正常

signals:
    void events(const QVector<AlarmEvent> &batch);
    void runningChanged(bool running);

private:
    void tick();

    TopologyScene *m_scene;
    QTimer *m_timer;
    QRandomGenerator m_rng;
};
