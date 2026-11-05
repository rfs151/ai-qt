#include "history/HistoryDb.h"
#include "comm/DeviceSim.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

// ------------------------------------------------------------------ HistoryWorker

HistoryWorker::HistoryWorker(QObject *parent)
    : QObject(parent),
      m_flushTimer(new QTimer(this)),
      m_statsTimer(new QTimer(this)),
      m_retentionTimer(new QTimer(this))
{
    m_flushTimer->setInterval(1000);
    m_statsTimer->setInterval(2000);
    m_retentionTimer->setInterval(60000);
    connect(m_flushTimer, &QTimer::timeout, this, [this] { flush(false); });
    connect(m_statsTimer, &QTimer::timeout, this, &HistoryWorker::publishStats);
    connect(m_retentionTimer, &QTimer::timeout, this,
            [this] { applyRetention(QDateTime::currentMSecsSinceEpoch()); });
}

HistoryWorker::~HistoryWorker() = default;

bool HistoryWorker::exec(const QString &sql)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (!q.exec(sql)) {
        emit errorOccurred(QStringLiteral("%1：%2").arg(sql.left(40), q.lastError().text()));
        return false;
    }
    return true;
}

bool HistoryWorker::open(const QString &path)
{
    m_path = path;
    m_conn = QStringLiteral("history-%1").arg(quintptr(this));
    if (path != QLatin1String(":memory:"))
        QDir().mkpath(QFileInfo(path).absolutePath());

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
        db.setDatabaseName(path);
        if (!db.open()) {
            emit errorOccurred(QStringLiteral("打开历史库失败：%1").arg(db.lastError().text()));
            return false;
        }
    }
    // WAL：写入不阻塞读取；synchronous=NORMAL 在 WAL 下只在检查点时 fsync，写入快很多，掉电最多丢最后一个事务
    if (path != QLatin1String(":memory:"))
        exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    const bool ok =
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS samples ("
                            " device_id INTEGER NOT NULL, ts_ms INTEGER NOT NULL,"
                            " cpu REAL, temp REAL, rx REAL, tx REAL, n INTEGER,"
                            " PRIMARY KEY (device_id, ts_ms)) WITHOUT ROWID")) &&
        exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts_ms)")) &&   // 过期清理用
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS alarms ("
                            " id INTEGER PRIMARY KEY, ts_ms INTEGER NOT NULL, device_id INTEGER NOT NULL,"
                            " device_name TEXT, level INTEGER, text TEXT)")) &&
        exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alarms_dev_ts ON alarms(device_id, ts_ms)"));
    if (!ok)
        return false;

    m_open = true;
    m_flushTimer->start();
    m_statsTimer->start();
    m_retentionTimer->start();
    applyRetention(QDateTime::currentMSecsSinceEpoch());
    publishStats();
    return true;
}

void HistoryWorker::close()
{
    if (!m_open)
        return;
    flush(true);
    m_flushTimer->stop();
    m_statsTimer->stop();
    m_retentionTimer->stop();
    QSqlDatabase::database(m_conn).close();
    m_open = false;
    // removeDatabase 要求该连接的 QSqlDatabase/QSqlQuery 对象都已销毁
    QSqlDatabase::removeDatabase(m_conn);
}

HistoryWorker::Row HistoryWorker::rowOf(int deviceId, const Acc &a)
{
    return {deviceId, a.bucket * kBucketMs, a.cpu, a.temp, float(a.rx / a.n), float(a.tx / a.n), a.n};
}

void HistoryWorker::appendSamples(const QVector<Sample> &samples)
{
    for (const Sample &s : samples) {
        const qint64 b = s.tMs / kBucketMs;
        Acc &a = m_acc[s.deviceId];
        if (b < a.bucket)
            continue;   // 乱序的旧数据丢弃，避免覆盖已完整的一秒
        if (b != a.bucket) {
            if (a.n > 0 && a.dirty)
                m_rows.append(rowOf(s.deviceId, a));
            a = Acc{};
            a.bucket = b;
            a.cpu = s.v[0];
            a.temp = s.v[1];
        }
        a.dirty = true;
        a.cpu = std::max(a.cpu, s.v[0]);
        a.temp = std::max(a.temp, s.v[1]);
        a.rx += s.v[2];
        a.tx += s.v[3];
        ++a.n;
    }
}

void HistoryWorker::appendAlarms(const QVector<HistoryAlarm> &alarms)
{
    m_alarms += alarms;
}

void HistoryWorker::flush(bool includePartial)
{
    if (!m_open)
        return;

    // 超过 1 秒没有新数据的设备（掉线），把它最后一秒写出去。累加器保留：
    // 如果这一秒还有迟到的数据，会接着累加并用 INSERT OR REPLACE 覆盖成完整的一行
    const qint64 nowBucket = QDateTime::currentMSecsSinceEpoch() / kBucketMs;
    for (auto it = m_acc.begin(); it != m_acc.end(); ++it) {
        if (it->dirty && it->n > 0 && it->bucket < nowBucket - 1) {
            m_rows.append(rowOf(it.key(), it.value()));
            it->dirty = false;
        }
    }
    QVector<Row> rows = m_rows;
    if (includePartial) {
        // 查询前把"正在累积的这一秒"也写进去；之后这一秒写完整时 INSERT OR REPLACE 覆盖它
        for (auto it = m_acc.cbegin(); it != m_acc.cend(); ++it)
            if (it->dirty && it->n > 0)
                rows.append(rowOf(it.key(), it.value()));
    }
    if (rows.isEmpty() && m_alarms.isEmpty())
        return;

    QElapsedTimer t;
    t.start();
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    db.transaction();   // 一个事务提交整批，比逐行自动提交快两个数量级
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO samples VALUES (?, ?, ?, ?, ?, ?, ?)"));
        for (const Row &r : std::as_const(rows)) {
            q.addBindValue(r.deviceId);
            q.addBindValue(r.ts);
            q.addBindValue(r.cpu);
            q.addBindValue(r.temp);
            q.addBindValue(r.rx);
            q.addBindValue(r.tx);
            q.addBindValue(r.n);
            q.exec();
        }
        QSqlQuery qa(db);
        qa.prepare(QStringLiteral("INSERT INTO alarms (ts_ms, device_id, device_name, level, text) VALUES (?, ?, ?, ?, ?)"));
        for (const HistoryAlarm &a : std::as_const(m_alarms)) {
            qa.addBindValue(a.wallMs);
            qa.addBindValue(a.deviceId);
            qa.addBindValue(a.deviceName);
            qa.addBindValue(a.level);
            qa.addBindValue(a.text);
            qa.exec();
        }
    }
    if (!db.commit()) {
        emit errorOccurred(QStringLiteral("提交失败：%1").arg(db.lastError().text()));
        db.rollback();
        return;
    }
    m_written += m_rows.size();
    m_lastCommitMs = t.nsecsElapsed() / 1e6;
    m_rows.clear();
    m_alarms.clear();
}

void HistoryWorker::query(int requestId, const HistoryQuery &q)
{
    HistoryResult r;
    r.requestId = requestId;
    r.query = q;
    if (!m_open) {
        r.error = QStringLiteral("历史库未打开");
        emit queryFinished(r);
        return;
    }
    flush(true);   // 保证能查到刚刚收到的数据

    QElapsedTimer t;
    t.start();
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    QSqlQuery sql(db);
    sql.prepare(QStringLiteral("SELECT COUNT(*) FROM samples WHERE device_id = ? AND ts_ms BETWEEN ? AND ?"));
    sql.addBindValue(q.deviceId);
    sql.addBindValue(q.fromWallMs);
    sql.addBindValue(q.toWallMs);
    const qint64 count = sql.exec() && sql.next() ? sql.value(0).toLongLong() : 0;

    if (count > q.maxPoints) {
        // 点数太多：在数据库里按时间桶聚合，只把聚合结果搬到内存
        const qint64 range = q.toWallMs - q.fromWallMs;
        r.bucketMs = ((range / q.maxPoints) / kBucketMs + 1) * kBucketMs;
        sql.prepare(QStringLiteral("SELECT MIN(ts_ms), MAX(cpu), MAX(temp), AVG(rx), AVG(tx) FROM samples"
                                   " WHERE device_id = ? AND ts_ms BETWEEN ? AND ?"
                                   " GROUP BY ts_ms / ? ORDER BY 1"));
        sql.addBindValue(q.deviceId);
        sql.addBindValue(q.fromWallMs);
        sql.addBindValue(q.toWallMs);
        sql.addBindValue(r.bucketMs);
    } else {
        sql.prepare(QStringLiteral("SELECT ts_ms, cpu, temp, rx, tx FROM samples"
                                   " WHERE device_id = ? AND ts_ms BETWEEN ? AND ? ORDER BY ts_ms"));
        sql.addBindValue(q.deviceId);
        sql.addBindValue(q.fromWallMs);
        sql.addBindValue(q.toWallMs);
    }
    sql.setForwardOnly(true);   // 只向前遍历，驱动不必缓存全部结果
    if (!sql.exec()) {
        r.error = sql.lastError().text();
    } else {
        r.points.reserve(qsizetype(std::min<qint64>(count, q.maxPoints)));
        while (sql.next())
            r.points.append(TelemetryPoint{sql.value(0).toLongLong(),
                             {sql.value(1).toFloat(), sql.value(2).toFloat(), sql.value(3).toFloat(),
                              sql.value(4).toFloat()}});
    }

    QSqlQuery qa(db);
    qa.prepare(QStringLiteral("SELECT ts_ms, device_id, device_name, level, text FROM alarms"
                              " WHERE device_id = ? AND ts_ms BETWEEN ? AND ? ORDER BY ts_ms DESC LIMIT 500"));
    qa.addBindValue(q.deviceId);
    qa.addBindValue(q.fromWallMs);
    qa.addBindValue(q.toWallMs);
    qa.setForwardOnly(true);
    if (qa.exec())
        while (qa.next())
            r.alarms.append(HistoryAlarm{qa.value(0).toLongLong(), qa.value(1).toInt(), qa.value(2).toString(), qa.value(3).toInt(),
                             qa.value(4).toString()});
    r.elapsedMs = t.elapsed();
    emit queryFinished(r);
}

void HistoryWorker::clearAll()
{
    m_acc.clear();
    m_rows.clear();
    m_alarms.clear();
    exec(QStringLiteral("DELETE FROM samples"));
    exec(QStringLiteral("DELETE FROM alarms"));
    if (m_path != QLatin1String(":memory:"))
        exec(QStringLiteral("VACUUM"));   // 归还磁盘空间
    publishStats();
}

void HistoryWorker::applyRetention(qint64 nowWallMs)
{
    if (!m_open || m_retentionHours <= 0)
        return;
    const qint64 cutoff = nowWallMs - qint64(m_retentionHours) * 3600 * 1000;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("DELETE FROM samples WHERE ts_ms < ?"));
    q.addBindValue(cutoff);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM alarms WHERE ts_ms < ?"));
    q.addBindValue(cutoff);
    q.exec();
}

void HistoryWorker::publishStats()
{
    if (!m_open)
        return;
    HistoryStats s;
    s.path = m_path;
    s.retentionHours = m_retentionHours;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (q.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM samples), (SELECT COUNT(*) FROM alarms)")) && q.next()) {
        s.sampleRows = q.value(0).toLongLong();
        s.alarmRows = q.value(1).toLongLong();
    }
    if (m_path != QLatin1String(":memory:"))
        s.fileBytes = QFileInfo(m_path).size() + QFileInfo(m_path + QStringLiteral("-wal")).size();
    s.rowsPerSec = m_written / (m_statsTimer->interval() / 1000.0);
    s.lastCommitMs = m_lastCommitMs;
    m_written = 0;
    emit statsUpdated(s);
}

// ------------------------------------------------------------------ HistoryManager

HistoryManager::HistoryManager(const QString &path, QObject *parent)
    : QObject(parent), m_path(path), m_worker(new HistoryWorker)
{
#if QT_CONFIG(thread)
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("history-writer"));
    m_worker->moveToThread(m_thread);
    m_thread->start(QThread::LowPriority);   // 写库不和界面、通信抢 CPU
#else
    m_worker->setParent(this);
#endif
    connect(m_worker, &HistoryWorker::queryFinished, this, &HistoryManager::queryFinished);
    connect(m_worker, &HistoryWorker::statsUpdated, this, &HistoryManager::statsUpdated);
    connect(m_worker, &HistoryWorker::errorOccurred, this, &HistoryManager::errorOccurred);
    post([w = m_worker, path] { w->open(path); });
}

HistoryManager::~HistoryManager()
{
#if QT_CONFIG(thread)
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->close(); }, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
#else
    m_worker->close();
#endif
}

QString HistoryManager::defaultPath()
{
#ifdef Q_OS_WASM
    return QStringLiteral(":memory:");   // 浏览器里没有持久文件系统，刷新页面即清空
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/history.db");
#endif
}

template <typename F>
void HistoryManager::post(F &&fn)
{
    QMetaObject::invokeMethod(m_worker, std::forward<F>(fn), Qt::QueuedConnection);
}

void HistoryManager::appendSamples(const QVector<Sample> &monoSamples)
{
    // 单调时钟 -> 墙上时间：历史库跨进程保存，必须用绝对时间
    const qint64 offset = QDateTime::currentMSecsSinceEpoch() - Clock::nowMs();
    QVector<Sample> wall = monoSamples;
    for (Sample &s : wall)
        s.tMs += offset;
    appendWallSamples(wall);
}

void HistoryManager::appendWallSamples(const QVector<Sample> &wallSamples)
{
    post([w = m_worker, wallSamples] { w->appendSamples(wallSamples); });
}

void HistoryManager::appendAlarms(const QVector<HistoryAlarm> &alarms)
{
    post([w = m_worker, alarms] { w->appendAlarms(alarms); });
}

int HistoryManager::query(const HistoryQuery &q)
{
    const int id = m_nextRequest++;
    post([w = m_worker, id, q] { w->query(id, q); });
    return id;
}

void HistoryManager::clearAll()
{
    post([w = m_worker] { w->clearAll(); });
}

void HistoryManager::flushNow()
{
    post([w = m_worker] { w->flush(true); });
}
