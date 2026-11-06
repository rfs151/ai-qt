#include "monitor/DeviceMonitor.h"
#include "comm/DeviceSim.h"
#include "scene/EdgeItem.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

#include <QTimer>

DeviceMonitor::DeviceMonitor(TopologyScene *scene, QObject *parent)
    : QObject(parent), m_scene(scene), m_offlineTimer(new QTimer(this))
{
    m_offlineTimer->setInterval(500);
    connect(m_offlineTimer, &QTimer::timeout, this, &DeviceMonitor::checkOffline);
    m_offlineTimer->start();
}

const Sample *DeviceMonitor::latest(int deviceId) const
{
    auto it = m_devs.constFind(deviceId);
    return it == m_devs.cend() ? nullptr : &it->last;
}

int DeviceMonitor::onlineCount() const
{
    return int(std::count_if(m_devs.cbegin(), m_devs.cend(), [](const DevState &d) { return d.online; }));
}

QList<int> DeviceMonitor::deviceIds() const
{
    QList<int> ids = m_devs.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool DeviceMonitor::isOnline(int deviceId) const
{
    auto it = m_devs.constFind(deviceId);
    return it != m_devs.cend() && it->online;
}

NodeStatus DeviceMonitor::evaluate(const Sample &s, NodeStatus current, QString *reason) const
{
    const float h = m_th.hysteresis;
    // 单个指标的级别：升级立即生效，降级要越过回差
    auto level = [&](float v, float warn, float alarm) {
        if (v >= alarm || (current == NodeStatus::Alarm && v >= alarm - h))
            return NodeStatus::Alarm;
        if (v >= warn || ((current == NodeStatus::Alarm || current == NodeStatus::Warning) && v >= warn - h))
            return NodeStatus::Warning;
        return NodeStatus::Normal;
    };
    const float cpu = s.v[0];
    const float temp = s.v[1];
    const NodeStatus cpuLv = level(cpu, m_th.cpuWarn, m_th.cpuAlarm);
    const NodeStatus tempLv = level(temp, m_th.tempWarn, m_th.tempAlarm);
    const NodeStatus result = std::max(cpuLv, tempLv);

    if (reason && result != NodeStatus::Normal) {
        if (cpuLv >= tempLv)
            *reason = QStringLiteral("CPU %1% 超过%2阈值 %3%")
                          .arg(cpu, 0, 'f', 1)
                          .arg(cpuLv == NodeStatus::Alarm ? QStringLiteral("告警") : QStringLiteral("警告"))
                          .arg(cpuLv == NodeStatus::Alarm ? m_th.cpuAlarm : m_th.cpuWarn);
        else
            *reason = QStringLiteral("温度 %1℃ 超过%2阈值 %3℃")
                          .arg(temp, 0, 'f', 1)
                          .arg(tempLv == NodeStatus::Alarm ? QStringLiteral("告警") : QStringLiteral("警告"))
                          .arg(tempLv == NodeStatus::Alarm ? m_th.tempAlarm : m_th.tempWarn);
    }
    return result;
}

void DeviceMonitor::onSamples(const QVector<Sample> &batch)
{
    if (batch.isEmpty())
        return;

    // 先全部入库，再只对每台设备的最新一条做判定和界面更新
    QHash<int, const Sample *> newest;
    for (const Sample &s : batch) {
        m_store.append(s);
        newest[s.deviceId] = &s;
    }

    QVector<AlarmEvent> events;
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = newest.cbegin(); it != newest.cend(); ++it) {
        const Sample &s = *it.value();
        DevState &d = m_devs[s.deviceId];
        const bool wasOffline = !d.online;
        d.last = s;
        d.lastSeen = s.tMs;
        d.online = true;

        NodeItem *n = m_scene->node(s.deviceId);
        if (!n)
            continue;   // 设备在拓扑里不存在（比如外部模拟器的设备数更多），只记录数据

        QString reason;
        // 离线恢复时从"正常"开始判定，不受离线前状态的回差影响
        const NodeStatus cur = wasOffline ? NodeStatus::Normal : n->status();
        const NodeStatus next = evaluate(s, cur, &reason);
        if (next != n->status()) {
            if (next == NodeStatus::Normal)
                reason = wasOffline ? QStringLiteral("通信恢复") : QStringLiteral("恢复正常");
            n->setStatus(next);
            events.append({now, n->id(), n->name(), next, reason});
        }
        // 链路负载取入流量占千兆口的百分比
        const int load = qBound(0, int(s.v[2] / 10.0f), 100);
        for (EdgeItem *e : n->edges())
            e->setLoad(load);
    }
    if (!events.isEmpty())
        emit alarmEvents(events);
}

void DeviceMonitor::checkOffline()
{
    const qint64 now = Clock::nowMs();
    QVector<AlarmEvent> events;
    const QDateTime wall = QDateTime::currentDateTime();
    for (auto it = m_devs.begin(); it != m_devs.end(); ++it) {
        DevState &d = it.value();
        if (!d.online || now - d.lastSeen < m_th.offlineMs)
            continue;
        d.online = false;
        if (NodeItem *n = m_scene->node(it.key()); n && n->status() != NodeStatus::Offline) {
            n->setStatus(NodeStatus::Offline);
            events.append({wall, n->id(), n->name(), NodeStatus::Offline,
                           QStringLiteral("通信超时：%1 秒未收到数据").arg(m_th.offlineMs / 1000)});
        }
    }
    if (!events.isEmpty())
        emit alarmEvents(events);
}

void DeviceMonitor::reset(bool restoreNormal)
{
    if (restoreNormal) {
        for (auto it = m_devs.cbegin(); it != m_devs.cend(); ++it)
            if (NodeItem *n = m_scene->node(it.key()))
                n->setStatus(NodeStatus::Normal);
    }
    m_devs.clear();
    m_store.clear();
}
