#pragma once

#include "comm/CommTypes.h"
#include "comm/TelemetryStore.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QThread;
class QTimer;

struct HistoryAlarm
{
    qint64 wallMs = 0;
    int deviceId = 0;
    QString deviceName;
    int level = 0;   // NodeStatus 的整数值
    QString text;
};

struct HistoryQuery
{
    int deviceId = 0;
    qint64 fromWallMs = 0;
    qint64 toWallMs = 0;
    int maxPoints = 20000;   // 超过就在 SQL 里按时间桶聚合
};

struct HistoryResult
{
    int requestId = 0;
    HistoryQuery query;
    QVector<TelemetryPoint> points;   // t 为墙上时间（毫秒）
    QVector<HistoryAlarm> alarms;
    qint64 bucketMs = 0;              // 0 表示原始 1 秒数据，否则为聚合桶宽
    qint64 elapsedMs = 0;
    QString error;
};

struct HistoryStats
{
    QString path;
    qint64 sampleRows = 0;
    qint64 alarmRows = 0;
    qint64 fileBytes = 0;
    double rowsPerSec = 0;
    double lastCommitMs = 0;
    int retentionHours = 0;
};

Q_DECLARE_METATYPE(HistoryResult)
Q_DECLARE_METATYPE(HistoryStats)

// 运行在写库线程。所有 QSqlDatabase 操作都在这个线程里完成（Qt 要求连接只在创建它的线程使用）。
//
// 入库策略：遥测按"设备 × 1 秒"聚合成一行（CPU/温度取最大值保留峰值，流量取平均），
// 38 台设备 10Hz 上报时写入量从 380 行/秒降到 38 行/秒；每秒一个事务批量提交。
// samples 表用 (device_id, ts_ms) 作主键并声明 WITHOUT ROWID：数据按设备+时间聚簇存放，
// 区间查询是一次连续扫描。
class HistoryWorker : public QObject
{
    Q_OBJECT
public:
    explicit HistoryWorker(QObject *parent = nullptr);
    ~HistoryWorker() override;

    static constexpr qint64 kBucketMs = 1000;

    bool open(const QString &path);
    void close();
    void appendSamples(const QVector<Sample> &wallSamples);   // tMs 已换算成墙上时间
    void appendAlarms(const QVector<HistoryAlarm> &alarms);
    void query(int requestId, const HistoryQuery &q);
    void clearAll();
    void setRetentionHours(int hours) { m_retentionHours = hours; }
    void applyRetention(qint64 nowWallMs);
    void flush(bool includePartial);

signals:
    void queryFinished(const HistoryResult &result);
    void statsUpdated(const HistoryStats &stats);
    void errorOccurred(const QString &text);

private:
    struct Acc
    {
        qint64 bucket = -1;
        float cpu = 0, temp = 0;
        double rx = 0, tx = 0;
        int n = 0;
        bool dirty = false;   // 有尚未写入数据库的新数据
    };
    struct Row
    {
        int deviceId;
        qint64 ts;
        float cpu, temp, rx, tx;
        int n;
    };
    static Row rowOf(int deviceId, const Acc &a);
    bool exec(const QString &sql);
    void publishStats();

    QString m_conn;   // 连接名，每个 worker 独立
    QString m_path;
    bool m_open = false;
    QHash<int, Acc> m_acc;
    QVector<Row> m_rows;
    QVector<HistoryAlarm> m_alarms;
    int m_retentionHours = 24;

    QTimer *m_flushTimer;
    QTimer *m_statsTimer;
    QTimer *m_retentionTimer;
    qint64 m_written = 0;
    double m_lastCommitMs = 0;
};

// 界面线程侧的门面，结构和 CommManager 一样：有线程就放进独立线程，没有（WASM）就留在主线程
class HistoryManager : public QObject
{
    Q_OBJECT
public:
    explicit HistoryManager(const QString &path, QObject *parent = nullptr);
    ~HistoryManager() override;

    static QString defaultPath();   // 桌面版：应用数据目录；WASM：内存数据库
    QString path() const { return m_path; }
    bool isThreaded() const { return m_thread != nullptr; }

    void appendSamples(const QVector<Sample> &monoSamples);   // 采样时间为 Clock::nowMs()，这里换算成墙上时间
    void appendWallSamples(const QVector<Sample> &wallSamples);
    void appendAlarms(const QVector<HistoryAlarm> &alarms);
    int query(const HistoryQuery &q);
    void clearAll();
    void flushNow();

signals:
    void queryFinished(const HistoryResult &result);
    void statsUpdated(const HistoryStats &stats);
    void errorOccurred(const QString &text);

private:
    template <typename F>
    void post(F &&fn);

    QString m_path;
    HistoryWorker *m_worker;
    QThread *m_thread = nullptr;
    int m_nextRequest = 1;
};
