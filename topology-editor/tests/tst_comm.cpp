#include "comm/CommManager.h"
#include "comm/DeviceSim.h"
#include "comm/Protocol.h"
#include "comm/TelemetryStore.h"

#include <QProcess>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

using namespace Proto;

namespace {
QByteArray telemetryFrame(quint16 id, float cpu, quint8 seq = 0)
{
    Telemetry t;
    t.deviceId = id;
    t.values = {cpu, 40.f, 100.f, 50.f};
    return encode(CmdTelemetry, seq, encodeTelemetry(t));
}
} // namespace

class TestComm : public QObject
{
    Q_OBJECT

private slots:
    void crcKnownVector()
    {
        // CRC-16/MODBUS 标准校验值
        QCOMPARE(crc16("123456789"), quint16(0x4B37));
    }

    void telemetryRoundTrip()
    {
        FrameParser p;
        QVector<Frame> frames;
        p.feed(telemetryFrame(7, 12.5f, 3), &frames);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].cmd, quint8(CmdTelemetry));
        QCOMPARE(frames[0].seq, quint8(3));
        Telemetry t;
        QVERIFY(decodeTelemetry(frames[0].payload, &t));
        QCOMPARE(t.deviceId, quint16(7));
        QCOMPARE(t.values[0], 12.5f);
    }

    void halfPacketsByteByByte()
    {
        const QByteArray stream = telemetryFrame(1, 1) + telemetryFrame(2, 2) + telemetryFrame(3, 3);
        FrameParser p;
        QVector<Frame> frames;
        for (char c : stream)
            p.feed(QByteArrayView(&c, 1), &frames);
        QCOMPARE(frames.size(), 3);
        QCOMPARE(p.stats().crcErrors, 0u);
        QCOMPARE(p.buffered(), 0);
    }

    void garbageAndCorruption()
    {
        QByteArray bad = telemetryFrame(2, 2);
        bad[10] = char(bad[10] ^ 0x40);   // 改一位，制造 CRC 错误
        // 垃圾 + 好帧 + 坏帧 + 一个孤立的 AA + 好帧，一次性到达（粘包）
        const QByteArray stream = QByteArray("\x01\x02\xAA\x10", 4) + telemetryFrame(1, 1) + bad +
                                  QByteArray(1, char(0xAA)) + telemetryFrame(3, 3);
        FrameParser p;
        QVector<Frame> frames;
        QVector<RawFrame> raw;
        p.feed(stream, &frames, &raw);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(p.stats().crcErrors, 1u);
        QVERIFY(p.stats().discardedBytes > 0);
        Telemetry t;
        QVERIFY(decodeTelemetry(frames[1].payload, &t));
        QCOMPARE(t.deviceId, quint16(3));
        // 报文监视里能看到那条坏帧
        QVERIFY(std::any_of(raw.cbegin(), raw.cend(), [](const RawFrame &r) { return !r.crcOk; }));
    }

    void absurdLengthResyncs()
    {
        // 伪帧头后面跟一个超大长度，解析器不能傻等
        const QByteArray stream = QByteArray("\xAA\x55\x01\x00\xFF\xFF", 6) + telemetryFrame(9, 9);
        FrameParser p;
        QVector<Frame> frames;
        p.feed(stream, &frames);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(p.stats().lengthErrors, 1u);
    }

    void ringBufferWraps()
    {
        RingBuffer<int> rb(4);
        for (int i = 0; i < 10; ++i)
            rb.push(i);
        QCOMPARE(rb.size(), 4);
        QCOMPARE(rb.at(0), 6);
        QCOMPARE(rb.back(), 9);
        QCOMPARE(rb.lowerBound(8, [](int v) { return v; }), 2);
        QCOMPARE(rb.lowerBound(100, [](int v) { return v; }), 4);
    }

    void decimateKeepsPeaks()
    {
        TelemetryStore store;
        store.fillSynthetic(1, 200000, 200000, 1);
        const TelemetrySeries *s = store.series(1);
        QVERIFY(s);
        float trueMax = -1e9f;
        for (qsizetype i = 0; i < s->size(); ++i)
            trueMax = std::max(trueMax, s->at(i).v[0]);

        QElapsedTimer t;
        t.start();
        const auto r = decimateMinMax(*s, 0, s->at(0).t, s->back().t, 800, 1000);
        qInfo("20 万点 -> %lld 点，用时 %lld us", qlonglong(r.outputPoints), qlonglong(t.nsecsElapsed() / 1000));
        QVERIFY(r.outputPoints <= 4 * 800 + 8);
        QCOMPARE(float(r.maxY), trueMax);   // 抽取后峰值不能丢
    }

    void decimateBreaksOnGap()
    {
        TelemetrySeries s(100);
        for (int i = 0; i < 10; ++i)
            s.push({i * 100, {float(i), 0, 0, 0}});
        for (int i = 0; i < 10; ++i)
            s.push({5000 + i * 100, {float(i), 0, 0, 0}});
        const auto r = decimateMinMax(s, 0, 0, 6000, 600, 1500);
        QCOMPARE(r.segments.size(), 2);
    }

    void simCoreParams()
    {
        DeviceSimCore core({1, 2});
        auto ackOf = [&](const QByteArray &req) {
            FrameParser p;
            QVector<Frame> frames;
            p.feed(core.handleInput(req), &frames);
            AckMsg a;
            return frames.size() == 1 && decodeAck(frames[0].payload, &a) ? int(a.result) : -1;
        };
        // 宏参数里不能直接写花括号初始化（逗号会被当成宏参数分隔符），先包一层
        auto req = [](quint8 seq, quint16 dev, quint8 param, qint32 value) {
            return encode(CmdWriteParam, seq, encodeWriteParam(WriteParamReq{dev, param, value}));
        };
        QCOMPARE(ackOf(req(1, 1, ParamReportInterval, 200)), int(AckOk));
        QCOMPARE(core.reportIntervalMs(), 200);
        QCOMPARE(ackOf(req(2, 1, ParamReportInterval, 5)), int(AckOutOfRange));
        QCOMPARE(ackOf(req(3, 99, ParamLoadBias, 10)), int(AckNoDevice));
        QCOMPARE(ackOf(req(4, 1, 77, 10)), int(AckBadParam));
    }

    // ------------------------------------------------------------ 线程 + 链路

    void workerRunsInOwnThread()
    {
        CommManager comm;
        QVERIFY(comm.isThreaded());

        QThread *seenIn = nullptr;
        connect(&comm, &CommManager::samplesReady, this, [&] { seenIn = QThread::currentThread(); });
        QSignalSpy samples(&comm, &CommManager::samplesReady);
        QSignalSpy states(&comm, &CommManager::stateChanged);

        LinkConfig cfg;
        cfg.deviceIds = {1, 2, 3};
        comm.open(cfg);
        QTRY_VERIFY_WITH_TIMEOUT(samples.size() >= 3, 3000);
        QCOMPARE(comm.state(), LinkState::Connected);
        QCOMPARE(seenIn, QThread::currentThread());   // 槽在界面线程执行

        // 批次里应该包含多台设备的采样
        QSet<int> ids;
        for (const auto &args : samples)
            for (const Sample &s : args.at(0).value<QVector<Sample>>())
                ids.insert(s.deviceId);
        QCOMPARE(ids, QSet<int>({1, 2, 3}));
    }

    void paramRetryThenTimeout()
    {
        CommManager comm;
        QSignalSpy results(&comm, &CommManager::paramResult);
        LinkConfig cfg;
        cfg.deviceIds = {1};
        comm.open(cfg);
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Connected, 2000);

        comm.writeParam(1, ParamLoadBias, 30);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
        QVERIFY(results.at(0).at(1).toBool());

        // 应答全部丢弃：重发 2 次后报超时
        FaultConfig f;
        f.dropAckPercent = 100;
        comm.setFaults(f);
        QSignalSpy stats(&comm, &CommManager::statsUpdated);
        comm.writeParam(1, ParamLoadBias, 0);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 3000);
        QVERIFY(!results.at(1).at(1).toBool());
        QVERIFY(results.at(1).at(2).toString().contains(QStringLiteral("超时")));
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty() && stats.last().at(0).value<LinkStats>().retries == 2, 2000);
    }

    void survivesFragmentationAndNoise()
    {
        CommManager comm;
        QSignalSpy stats(&comm, &CommManager::statsUpdated);
        QSignalSpy samples(&comm, &CommManager::samplesReady);
        FaultConfig f;
        f.fragment = true;
        f.corruptPermille = 50;
        comm.setFaults(f);
        LinkConfig cfg;
        cfg.deviceIds = {1, 2, 3, 4, 5};
        comm.open(cfg);
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty() && stats.last().at(0).value<LinkStats>().frames > 50, 4000);
        const LinkStats s = stats.last().at(0).value<LinkStats>();
        QVERIFY(s.crcErrors > 0);
        QCOMPARE(comm.state(), LinkState::Connected);   // 误码不应导致断线
        QVERIFY(!samples.isEmpty());
    }

    void reconnectsAfterOutage()
    {
        CommManager comm;
        QSignalSpy states(&comm, &CommManager::stateChanged);
        LinkConfig cfg;
        cfg.deviceIds = {1};
        comm.open(cfg);
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Connected, 2000);

        comm.simulateOutage(3500);
        // 3 秒心跳超时 -> 重连中 -> 设备恢复后重新连上
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Reconnecting, 4500);
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Connected, 6000);
        const bool sawTimeout = std::any_of(states.cbegin(), states.cend(), [](const QList<QVariant> &a) {
            return a.at(1).toString().contains(QStringLiteral("心跳超时"));
        });
        QVERIFY(sawTimeout);
    }

#ifdef HAS_NETWORK
    // 端到端：启动独立的 devsim 进程，通过真实 TCP 连接收数据、下发参数
    void tcpToDevsimProcess()
    {
        QProcess sim;
        sim.start(QStringLiteral(DEVSIM_PATH), {QStringLiteral("--tcp"), QStringLiteral("19123"),
                                                 QStringLiteral("--devices"), QStringLiteral("4")});
        QVERIFY(sim.waitForStarted());
        QByteArray out;
        QTRY_VERIFY_WITH_TIMEOUT((out += sim.readAllStandardOutput()).contains("TCP"), 3000);

        CommManager comm;
        QSignalSpy samples(&comm, &CommManager::samplesReady);
        QSignalSpy results(&comm, &CommManager::paramResult);
        LinkConfig cfg;
        cfg.kind = LinkConfig::Tcp;
        cfg.port = 19123;
        comm.open(cfg);
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Connected, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(samples.size() >= 3, 3000);
        comm.writeParam(2, ParamLoadBias, 40);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
        QVERIFY(results.at(0).at(1).toBool());

        // 模拟器进程退出：上位机应进入重连
        sim.kill();
        sim.waitForFinished();
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Reconnecting, 4000);
    }
#endif
};

QTEST_GUILESS_MAIN(TestComm)
#include "tst_comm.moc"
