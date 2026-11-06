#include "dashboard/DashboardBackend.h"
#include "comm/CommManager.h"
#include "comm/DeviceSim.h"
#include "monitor/DeviceMonitor.h"
#include "scene/NodeItem.h"
#include "scene/TopologyScene.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QTimer>

namespace {

void pushTrend(QList<qreal> &list, qreal v)
{
    list.append(v);
    if (list.size() > DashboardBackend::kTrendCapacity)
        list.removeFirst();
}

// 最近 windowMs 内的 CPU，分成 buckets 段，每段取最大值（尖峰不会被平均掉）
QList<qreal> cpuSpark(const TelemetrySeries *s, qint64 now, qint64 windowMs, int buckets)
{
    QList<qreal> out;
    if (!s || s->isEmpty())
        return out;
    const qint64 t0 = now - windowMs;
    qsizetype i = s->lowerBound(t0, [](const TelemetryPoint &p) { return p.t; });
    const qint64 step = windowMs / buckets;
    for (int b = 0; b < buckets && i < s->size(); ++b) {
        const qint64 end = t0 + (b + 1) * step;
        qreal peak = -1;
        for (; i < s->size() && s->at(i).t < end; ++i)
            peak = std::max<qreal>(peak, s->at(i).v[0]);
        if (peak >= 0)
            out.append(peak);
    }
    return out;
}

} // namespace

DashboardBackend::DashboardBackend(TopologyScene *scene, DeviceMonitor *monitor, CommManager *comm,
                                   QAbstractItemModel *alarmLog, QObject *parent)
    : QObject(parent),
      m_scene(scene),
      m_monitor(monitor),
      m_comm(comm),
      m_alarmLog(alarmLog),
      m_devices(this),
      m_timer(new QTimer(this))
{
    Q_ASSERT_X(!s_instance, "DashboardBackend", "只能有一个实例");
    s_instance = this;
    m_uptime.start();
    m_linkText = QStringLiteral("链路未连接");

    connect(m_comm, &CommManager::stateChanged, this, &DashboardBackend::onLinkState);
    connect(m_comm, &CommManager::statsUpdated, this, &DashboardBackend::onLinkStats);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &DashboardBackend::tick);
    m_timer->start();
}

DashboardBackend::~DashboardBackend() = default;

DashboardBackend *DashboardBackend::create(QQmlEngine *qmlEngine, QJSEngine *)
{
    Q_ASSERT(s_instance);
    Q_ASSERT(qmlEngine->thread() == s_instance->thread());
    // 不让 QML 引擎在销毁时 delete 它
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void DashboardBackend::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    if (on) {
        refreshStats();
        refreshDevices();
        emit trendChanged();
        emit linkChanged();
    }
}

void DashboardBackend::countAlarmEvents(int n)
{
    m_alarmEvents += n;
}

void DashboardBackend::setAbnormalOnly(bool on)
{
    if (m_abnormalOnly == on)
        return;
    m_abnormalOnly = on;
    emit abnormalOnlyChanged();
    refreshDevices();
}

double DashboardBackend::trafficPeak() const
{
    qreal peak = 100;
    for (qreal v : m_inTrend)
        peak = std::max(peak, v);
    for (qreal v : m_outTrend)
        peak = std::max(peak, v);
    // 取整到 1/2/5 × 10^n，坐标刻度好读
    const double mag = std::pow(10.0, std::floor(std::log10(peak)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (peak <= m * mag)
            return m * mag;
    return peak;
}

void DashboardBackend::tick()
{
    refreshStats();
    pushTrend(m_cpuTrend, m_avgCpu);
    pushTrend(m_cpuMaxTrend, m_maxCpu);
    pushTrend(m_inTrend, m_in);
    pushTrend(m_outTrend, m_out);
    if (m_active) {
        refreshDevices();
        emit trendChanged();
    }
}

void DashboardBackend::refreshStats()
{
    m_nodeCount = int(m_scene->nodes().size());
    for (int s = 0; s < 4; ++s)
        m_status[s] = m_scene->statusCount(NodeStatus(s));

    // 只统计在线设备的最新采样；上万节点压测时监控设备也限制在 300 台以内
    const QList<int> ids = m_monitor->deviceIds();
    m_monitored = int(ids.size());
    m_online = 0;
    double cpuSum = 0, tempSum = 0;
    m_maxCpu = m_maxTemp = m_in = m_out = 0;
    for (int id : ids) {
        if (!m_monitor->isOnline(id))
            continue;
        const Sample *s = m_monitor->latest(id);
        if (!s)
            continue;
        ++m_online;
        cpuSum += s->v[0];
        tempSum += s->v[1];
        m_maxCpu = std::max<double>(m_maxCpu, s->v[0]);
        m_maxTemp = std::max<double>(m_maxTemp, s->v[1]);
        m_in += s->v[2];
        m_out += s->v[3];
    }
    m_avgCpu = m_online ? cpuSum / m_online : 0;
    m_avgTemp = m_online ? tempSum / m_online : 0;
    if (m_active)
        emit statsChanged();
}

void DashboardBackend::refreshDevices()
{
    const qint64 now = Clock::nowMs();
    const QList<int> ids = m_monitor->deviceIds();
    QVector<DeviceRow> rows;
    rows.reserve(ids.size());
    for (int id : ids) {
        DeviceRow r;
        r.id = id;
        r.online = m_monitor->isOnline(id);
        if (const NodeItem *n = m_scene->node(id)) {
            r.name = n->name();
            r.type = nodeTypeName(n->nodeType());
            r.status = int(n->status());
        } else {
            // 外部模拟器的设备可能比拓扑里的多
            r.name = QStringLiteral("设备 #%1").arg(id);
            r.type = QStringLiteral("未入图");
            r.status = int(r.online ? NodeStatus::Normal : NodeStatus::Offline);
        }
        if (m_abnormalOnly && r.status == int(NodeStatus::Normal))
            continue;
        if (const Sample *s = m_monitor->latest(id)) {
            r.cpu = s->v[0];
            r.temp = s->v[1];
            r.inMbps = s->v[2];
            r.outMbps = s->v[3];
        }
        r.spark = cpuSpark(m_monitor->store()->series(id), now, 60000, 30);
        rows.append(std::move(r));
    }
    m_devices.setRows(std::move(rows));
}

void DashboardBackend::onLinkState(LinkState state, const QString &detail)
{
    m_linkConnected = state == LinkState::Connected;
    QString mode;
    switch (m_comm->kind()) {
    case LinkConfig::Virtual: mode = QStringLiteral("虚拟设备"); break;
    case LinkConfig::Tcp:     mode = QStringLiteral("TCP"); break;
    case LinkConfig::Serial:  mode = QStringLiteral("串口"); break;
    }
    const QString proto = m_comm->isModbus() ? QStringLiteral("Modbus") : QStringLiteral("自定义协议");
    switch (state) {
    case LinkState::Connected:    m_linkText = QStringLiteral("%1 · %2").arg(mode, proto); break;
    case LinkState::Connecting:   m_linkText = QStringLiteral("正在连接…"); break;
    case LinkState::Reconnecting: m_linkText = QStringLiteral("重连中 %1").arg(detail); break;
    case LinkState::Disconnected: m_linkText = QStringLiteral("链路未连接"); break;
    }
    if (state != LinkState::Connected)
        m_fps = m_rxKBps = 0;
    emit linkChanged();
}

void DashboardBackend::onLinkStats(const LinkStats &st)
{
    m_fps = st.modbus ? st.requestsPerSec : st.framesPerSec;
    m_rxKBps = st.rxBytesPerSec / 1024.0;
    m_crcErrors = st.crcErrors;
    if (m_active)
        emit linkChanged();
}
