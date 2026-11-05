#include "history/HistoryDb.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
Sample wallSample(int dev, qint64 t, float cpu, float rx = 100)
{
    Sample s;
    s.deviceId = dev;
    s.tMs = t;
    s.v = {cpu, 40, rx, rx / 2};
    return s;
}

// 对齐到整秒、一分钟前，避开"当前正在累积的这一秒"和 24 小时过期清理
qint64 baseTime() { return (QDateTime::currentMSecsSinceEpoch() / 1000 - 60) * 1000; }

HistoryResult runQuery(HistoryWorker &w, int dev, qint64 from, qint64 to, int maxPoints = 20000)
{
    QSignalSpy spy(&w, &HistoryWorker::queryFinished);
    w.query(1, {dev, from, to, maxPoints});
    return spy.size() == 1 ? spy.at(0).at(0).value<HistoryResult>() : HistoryResult{};
}
} // namespace

class TestHistory : public QObject
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(m_dir.isValid()); }

    void aggregatesPerSecond()
    {
        HistoryWorker w;
        QVERIFY(w.open(m_dir.filePath(QStringLiteral("agg.db"))));
        const qint64 t0 = baseTime();
        QVector<Sample> batch;
        for (int i = 0; i < 30; ++i)   // 3 秒，每秒 10 个点
            batch << wallSample(1, t0 + i * 100, float(i), float(i % 10) * 10);
        w.appendSamples(batch);

        const HistoryResult r = runQuery(w, 1, t0, t0 + 10000);
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.points.size(), 3);
        QCOMPARE(r.bucketMs, 0);
        QCOMPARE(r.points[0].t, t0);
        QCOMPARE(r.points[0].v[0], 9.0f);    // CPU 取一秒内最大值
        QCOMPARE(r.points[2].v[0], 29.0f);
        QCOMPARE(r.points[1].v[2], 45.0f);   // 流量取平均：(0+10+…+90)/10
        w.close();
    }

    void partialSecondIsCompletedLater()
    {
        HistoryWorker w;
        QVERIFY(w.open(m_dir.filePath(QStringLiteral("partial.db"))));
        const qint64 t0 = baseTime();
        w.appendSamples({wallSample(1, t0, 10), wallSample(1, t0 + 100, 20)});
        QCOMPARE(runQuery(w, 1, t0, t0 + 999).points.size(), 1);   // 查询时先写入未完成的这一秒

        // 同一秒后面来了更大的值：同主键 INSERT OR REPLACE，不会重复
        w.appendSamples({wallSample(1, t0 + 500, 70), wallSample(1, t0 + 1000, 5)});
        const HistoryResult r = runQuery(w, 1, t0, t0 + 999);
        QCOMPARE(r.points.size(), 1);
        QCOMPARE(r.points[0].v[0], 70.0f);
        w.close();
    }

    void largeRangeIsAggregatedInSql()
    {
        HistoryWorker w;
        QVERIFY(w.open(m_dir.filePath(QStringLiteral("big.db"))));
        const qint64 t0 = baseTime() - 6000 * 1000;
        QVector<Sample> batch;
        for (int i = 0; i < 6000; ++i)
            batch << wallSample(2, t0 + i * 1000, i == 4321 ? 99.f : 20.f);
        w.appendSamples(batch);

        const HistoryResult r = runQuery(w, 2, t0, t0 + 6000 * 1000, 1000);
        QVERIFY(r.bucketMs >= 6000);
        QVERIFY(r.points.size() <= 1001);
        float peak = 0;
        for (const TelemetryPoint &p : r.points)
            peak = std::max(peak, p.v[0]);
        QCOMPARE(peak, 99.0f);   // 聚合后尖峰仍在
        w.close();
    }

    void alarmsAndRetention()
    {
        HistoryWorker w;
        QVERIFY(w.open(m_dir.filePath(QStringLiteral("ret.db"))));
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 old = now - 30LL * 3600 * 1000;
        w.appendSamples({wallSample(3, old, 1), wallSample(3, old + 1000, 1)});
        w.appendAlarms({{old, 3, QStringLiteral("dev3"), 2, QStringLiteral("旧告警")},
                        {now - 5000, 3, QStringLiteral("dev3"), 1, QStringLiteral("新告警")}});
        w.flush(true);
        QCOMPARE(runQuery(w, 3, old - 1, now).alarms.size(), 2);

        w.setRetentionHours(24);
        w.applyRetention(now);
        const HistoryResult r = runQuery(w, 3, old - 1, now);
        QCOMPARE(r.points.size(), 0);
        QCOMPARE(r.alarms.size(), 1);
        QCOMPARE(r.alarms[0].text, QStringLiteral("新告警"));
        w.close();
    }

    // 通过 HistoryManager 走写库线程；析构时把缓冲数据写完，重新打开后能查到
    void threadedWriteSurvivesRestart()
    {
        const QString path = m_dir.filePath(QStringLiteral("persist.db"));
        const qint64 t0 = baseTime();
        {
            HistoryManager m(path);
            QVERIFY(m.isThreaded());
            QVector<Sample> batch;
            for (int i = 0; i < 50; ++i)
                batch << wallSample(5, t0 + i * 100, 30);
            m.appendWallSamples(batch);
        }
        HistoryManager m(path);
        QSignalSpy spy(&m, &HistoryManager::queryFinished);
        m.query({5, t0, t0 + 10000, 20000});
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 3000);
        QCOMPARE(spy.at(0).at(0).value<HistoryResult>().points.size(), 5);
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestHistory)
#include "tst_history.moc"
