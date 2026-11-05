#include "sim/AlarmSimulator.h"
#include "scene/EdgeItem.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

#include <QTimer>

namespace {
const char *const kWarnTexts[] = {"CPU 使用率超过 80%", "内存使用率超过 85%", "接口丢包率升高", "响应时间超过 200ms"};
const char *const kAlarmTexts[] = {"CPU 使用率超过 95%", "磁盘空间不足", "链路中断", "心跳超时", "温度过高"};
} // namespace

AlarmSimulator::AlarmSimulator(TopologyScene *scene, QObject *parent)
    : QObject(parent), m_scene(scene), m_timer(new QTimer(this)), m_rng(20241031)
{
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &AlarmSimulator::tick);
}

bool AlarmSimulator::isRunning() const { return m_timer->isActive(); }

void AlarmSimulator::start()
{
    m_timer->start();
    emit runningChanged(true);
}

void AlarmSimulator::stop()
{
    m_timer->stop();
    emit runningChanged(false);
}

void AlarmSimulator::setIntervalMs(int ms) { m_timer->setInterval(ms); }

void AlarmSimulator::resetAll()
{
    for (NodeItem *n : m_scene->nodes())
        n->setStatus(NodeStatus::Normal);
}

void AlarmSimulator::tick()
{
    const auto &nodes = m_scene->nodes();
    if (nodes.isEmpty())
        return;

    const QList<NodeItem *> all = nodes.values();
    const int changes = std::max(1, int(all.size()) / 150);
    QVector<AlarmEvent> batch;
    batch.reserve(changes);
    const QDateTime now = QDateTime::currentDateTime();

    for (int i = 0; i < changes; ++i) {
        NodeItem *n = all[m_rng.bounded(int(all.size()))];
        const int roll = m_rng.bounded(100);
        NodeStatus next = n->status();
        QString text;

        if (n->status() == NodeStatus::Normal) {
            if (roll < 55) {
                next = NodeStatus::Warning;
                text = QString::fromUtf8(kWarnTexts[m_rng.bounded(4)]);
            } else if (roll < 85) {
                next = NodeStatus::Alarm;
                text = QString::fromUtf8(kAlarmTexts[m_rng.bounded(5)]);
            } else {
                next = NodeStatus::Offline;
                text = QStringLiteral("设备离线");
            }
        } else if (roll < 60) {
            // 异常节点大概率恢复，保证整体以绿色为主
            next = NodeStatus::Normal;
            text = QStringLiteral("恢复正常");
        } else if (n->status() == NodeStatus::Warning && roll < 80) {
            next = NodeStatus::Alarm;
            text = QStringLiteral("警告升级为告警");
        }

        if (next == n->status())
            continue;
        n->setStatus(next);
        batch.append({now, n->id(), n->name(), next, text});
    }

    // 链路负载波动：只抽样一部分，避免每周期重绘全部连线
    const auto &edges = m_scene->edges();
    if (!edges.isEmpty()) {
        const int total = int(edges.size());
        const int budget = std::max(1, total / 50);
        for (EdgeItem *e : edges)
            if (m_rng.bounded(total) < budget)
                e->setLoad(e->load() + m_rng.bounded(-25, 26));
    }

    if (!batch.isEmpty())
        emit events(batch);
}
