#pragma once

#include "dashboard/DeviceListModel.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class TopologyScene;
class DeviceMonitor;
class CommManager;
class QQmlEngine;
class QJSEngine;
class QTimer;
struct LinkStats;
enum class LinkState;

// 监控大屏的数据门面：汇总拓扑、监控、通信三处的数据，以属性的形式暴露给 QML。
// 对象由 C++ 创建（需要注入依赖），通过 QML_SINGLETON + create() 交给 QML 引擎使用，
// QML 里写 Backend.avgCpu 即可绑定。
//
// 刷新节奏：每秒采一次趋势点（大屏关闭时也在采，打开就有历史）；
// 设备卡片和 KPI 只在大屏可见时刷新。
class DashboardBackend : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Backend)
    QML_SINGLETON

    Q_PROPERTY(int nodeCount READ nodeCount NOTIFY statsChanged)
    Q_PROPERTY(int normalCount READ normalCount NOTIFY statsChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY statsChanged)
    Q_PROPERTY(int alarmCount READ alarmCount NOTIFY statsChanged)
    Q_PROPERTY(int offlineCount READ offlineCount NOTIFY statsChanged)
    Q_PROPERTY(int monitoredCount READ monitoredCount NOTIFY statsChanged)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY statsChanged)
    Q_PROPERTY(double avgCpu READ avgCpu NOTIFY statsChanged)
    Q_PROPERTY(double avgTemp READ avgTemp NOTIFY statsChanged)
    Q_PROPERTY(double maxTemp READ maxTemp NOTIFY statsChanged)
    Q_PROPERTY(double totalInMbps READ totalInMbps NOTIFY statsChanged)
    Q_PROPERTY(double totalOutMbps READ totalOutMbps NOTIFY statsChanged)
    Q_PROPERTY(int alarmEvents READ alarmEvents NOTIFY statsChanged)
    Q_PROPERTY(qint64 uptimeSec READ uptimeSec NOTIFY statsChanged)

    Q_PROPERTY(bool linkConnected READ linkConnected NOTIFY linkChanged)
    Q_PROPERTY(QString linkText READ linkText NOTIFY linkChanged)
    Q_PROPERTY(double framesPerSec READ framesPerSec NOTIFY linkChanged)
    Q_PROPERTY(double rxKBps READ rxKBps NOTIFY linkChanged)
    Q_PROPERTY(quint64 crcErrors READ crcErrors NOTIFY linkChanged)

    Q_PROPERTY(QList<qreal> cpuTrend READ cpuTrend NOTIFY trendChanged)
    Q_PROPERTY(QList<qreal> cpuMaxTrend READ cpuMaxTrend NOTIFY trendChanged)
    Q_PROPERTY(QList<qreal> inTrend READ inTrend NOTIFY trendChanged)
    Q_PROPERTY(QList<qreal> outTrend READ outTrend NOTIFY trendChanged)
    Q_PROPERTY(double trafficPeak READ trafficPeak NOTIFY trendChanged)
    Q_PROPERTY(int trendCapacity READ trendCapacity CONSTANT)

    Q_PROPERTY(DeviceListModel *devices READ devices CONSTANT)
    Q_PROPERTY(QAbstractItemModel *alarms READ alarms CONSTANT)
    Q_PROPERTY(bool abnormalOnly READ abnormalOnly WRITE setAbnormalOnly NOTIFY abnormalOnlyChanged)

public:
    DashboardBackend(TopologyScene *scene, DeviceMonitor *monitor, CommManager *comm, QAbstractItemModel *alarmLog,
                     QObject *parent = nullptr);
    ~DashboardBackend() override;

    // QML 引擎第一次访问 Backend 时调用，返回已存在的实例（所有权仍归 C++）
    static DashboardBackend *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // 大屏可见时才刷新卡片和 KPI
    void setActive(bool on);
    // 每台设备产生一次告警级事件就计数一次
    void countAlarmEvents(int n);

    int nodeCount() const { return m_nodeCount; }
    int normalCount() const { return m_status[0]; }
    int warningCount() const { return m_status[1]; }
    int alarmCount() const { return m_status[2]; }
    int offlineCount() const { return m_status[3]; }
    int monitoredCount() const { return m_monitored; }
    int onlineCount() const { return m_online; }
    double avgCpu() const { return m_avgCpu; }
    double avgTemp() const { return m_avgTemp; }
    double maxTemp() const { return m_maxTemp; }
    double totalInMbps() const { return m_in; }
    double totalOutMbps() const { return m_out; }
    int alarmEvents() const { return m_alarmEvents; }
    qint64 uptimeSec() const { return m_uptime.elapsed() / 1000; }

    bool linkConnected() const { return m_linkConnected; }
    QString linkText() const { return m_linkText; }
    double framesPerSec() const { return m_fps; }
    double rxKBps() const { return m_rxKBps; }
    quint64 crcErrors() const { return m_crcErrors; }

    QList<qreal> cpuTrend() const { return m_cpuTrend; }
    QList<qreal> cpuMaxTrend() const { return m_cpuMaxTrend; }
    QList<qreal> inTrend() const { return m_inTrend; }
    QList<qreal> outTrend() const { return m_outTrend; }
    double trafficPeak() const;
    int trendCapacity() const { return kTrendCapacity; }

    DeviceListModel *devices() { return &m_devices; }
    QAbstractItemModel *alarms() const { return m_alarmLog; }
    bool abnormalOnly() const { return m_abnormalOnly; }
    void setAbnormalOnly(bool on);

    Q_INVOKABLE void locate(int deviceId) { emit locateRequested(deviceId); }
    Q_INVOKABLE void back() { emit backRequested(); }
    Q_INVOKABLE void toggleFullScreen() { emit fullScreenRequested(); }
    Q_INVOKABLE void connectDevices() { emit connectRequested(); }

    static constexpr int kTrendCapacity = 120;   // 两分钟，每秒一点

signals:
    void statsChanged();
    void linkChanged();
    void trendChanged();
    void abnormalOnlyChanged();

    void locateRequested(int deviceId);
    void backRequested();
    void fullScreenRequested();
    void connectRequested();

private:
    void tick();
    void refreshStats();
    void refreshDevices();
    void onLinkState(LinkState state, const QString &detail);
    void onLinkStats(const LinkStats &st);

    static inline QPointer<DashboardBackend> s_instance;

    TopologyScene *m_scene;
    DeviceMonitor *m_monitor;
    CommManager *m_comm;
    QAbstractItemModel *m_alarmLog;
    DeviceListModel m_devices;
    QTimer *m_timer;
    QElapsedTimer m_uptime;
    bool m_active = false;
    bool m_abnormalOnly = false;

    int m_nodeCount = 0;
    int m_status[4] = {};
    int m_monitored = 0;
    int m_online = 0;
    double m_avgCpu = 0, m_maxCpu = 0, m_avgTemp = 0, m_maxTemp = 0, m_in = 0, m_out = 0;
    int m_alarmEvents = 0;

    bool m_linkConnected = false;
    QString m_linkText;
    double m_fps = 0, m_rxKBps = 0;
    quint64 m_crcErrors = 0;

    QList<qreal> m_cpuTrend, m_cpuMaxTrend, m_inTrend, m_outTrend;
};
