#include "comm/CommManager.h"
#include "comm/DeviceSim.h"
#include "comm/Modbus.h"

#include <QSignalSpy>
#include <QtTest>

#ifdef HAS_SERIALBUS
#include <QModbusReply>
#include <QModbusTcpClient>
#include <QModbusTcpServer>
#include <QProcess>
#endif

using namespace Modbus;

namespace {
// 从站解析一个 RTU 请求，返回应答帧
QByteArray rtuRoundTrip(ModbusSlaveSim &slave, quint8 unit, const QByteArray &pdu)
{
    return slave.handleRtu(rtuFrame(unit, pdu));
}
} // namespace

class TestModbus : public QObject
{
    Q_OBJECT

private slots:
    void rtuFrameKnownVector()
    {
        // 经典示例：从站 1 读保持寄存器 0 起 10 个，CRC = C5 CD
        QCOMPARE(rtuFrame(1, readRequest(FcReadHolding, 0, 10)).toHex(' '), QByteArray("01 03 00 00 00 0a c5 cd"));
        QVERIFY(rtuCrcOk(QByteArray::fromHex("01030000000AC5CD")));
        QVERIFY(!rtuCrcOk(QByteArray::fromHex("01030000000AC5CE")));
    }

    void tcpFrameHeader()
    {
        // MBAP：事务号 0x0102、协议 0、长度 = 1 + PDU
        QCOMPARE(tcpFrame(0x0102, 7, readRequest(FcReadInput, 0, 6)).toHex(' '),
                 QByteArray("01 02 00 00 00 06 07 04 00 00 00 06"));
    }

    void floatWordOrder()
    {
        quint16 hi, lo;
        fromFloat(1.0f, &hi, &lo);
        QCOMPARE(hi, quint16(0x3F80));   // ABCD：高字在前
        QCOMPARE(lo, quint16(0x0000));
        QCOMPARE(toFloat(0x4248, 0x0000), 50.0f);
    }

    void slaveReadsAndExceptions()
    {
        DeviceSimCore core({1, 2});
        core.advance(0);
        ModbusSlaveSim slave(core);

        const QByteArray r = rtuRoundTrip(slave, 1, readRequest(FcReadInput, 0, kInputCount));
        QCOMPARE(r.size(), 5 + kInputCount * 2);
        QCOMPARE(quint8(r[2]), quint8(kInputCount * 2));
        const QVector<quint16> regs = registersOf(QByteArrayView(r).sliced(3), kInputCount);
        QCOMPARE(regs[0] / 10.0f, std::round(core.latest(1)->values[0] * 10) / 10.0f);

        // 越界地址 -> 异常 02
        const QByteArray ex = rtuRoundTrip(slave, 1, readRequest(FcReadInput, 4, 5));
        QCOMPARE(quint8(ex[1]), quint8(FcReadInput | 0x80));
        QCOMPARE(quint8(ex[2]), quint8(ExIllegalAddress));
        // 不支持的功能码 -> 异常 01
        QCOMPARE(quint8(rtuRoundTrip(slave, 1, QByteArray::fromHex("0500000000"))[2]), quint8(ExIllegalFunction));
        // 不存在的从站、广播：RTU 下都不应答
        QVERIFY(rtuRoundTrip(slave, 9, readRequest(FcReadInput, 0, 1)).isEmpty());
        QVERIFY(rtuRoundTrip(slave, 0, writeSingleRequest(kHoldLoadBias, 10)).isEmpty());
        QCOMPARE(core.paramValue(2, Proto::ParamLoadBias), 10);   // 但广播写已经生效
    }

    void slaveWrites()
    {
        DeviceSimCore core({1});
        ModbusSlaveSim slave(core);
        const QByteArray req = writeSingleRequest(kHoldLoadBias, quint16(qint16(-20)));
        QCOMPARE(rtuRoundTrip(slave, 1, req), rtuFrame(1, req));   // 回显
        QCOMPARE(core.paramValue(1, Proto::ParamLoadBias), -20);

        // 超出允许范围 -> 异常 03，值不变
        QCOMPARE(quint8(rtuRoundTrip(slave, 1, writeSingleRequest(kHoldLoadBias, 99))[2]), quint8(ExIllegalValue));
        QCOMPARE(core.paramValue(1, Proto::ParamLoadBias), -20);

        // FC16 一次写两个；其中一个越界则全部不生效
        QCOMPARE(quint8(rtuRoundTrip(slave, 1, writeMultipleRequest(0, {200, 99}))[2]), quint8(ExIllegalValue));
        QCOMPARE(core.reportIntervalMs(), 100);
        QCOMPARE(rtuRoundTrip(slave, 1, writeMultipleRequest(0, {200, 30})).size(), 8);
        QCOMPARE(core.reportIntervalMs(), 200);
        QCOMPARE(core.paramValue(1, Proto::ParamLoadBias), 30);
    }

    void slaveHandlesSplitAndNoisyRtu()
    {
        DeviceSimCore core({3});
        ModbusSlaveSim slave(core);
        const QByteArray req = rtuFrame(3, readRequest(FcReadHolding, 0, 2));
        // 前面带噪声字节、请求分两次到达
        QVERIFY(slave.handleRtu(QByteArray::fromHex("FF00") + req.left(3)).isEmpty());
        const QByteArray r = slave.handleRtu(req.mid(3));
        QCOMPARE(r.size(), 9);
        QCOMPARE(be16(r.constData() + 3), quint16(100));   // 40001 采样周期
    }

    // ------------------------------------------------------------ 主站 + 虚拟从站（通信线程）

    void masterPollsVirtualSlaves()
    {
        CommManager comm;
        QSignalSpy samples(&comm, &CommManager::samplesReady);
        QSignalSpy regs(&comm, &CommManager::registersUpdated);
        QSignalSpy stats(&comm, &CommManager::statsUpdated);
        LinkConfig cfg;
        cfg.modbus = true;
        cfg.pollMs = 200;
        cfg.deviceIds = {1, 2, 3, 4};
        comm.setRegisterWatch(2);
        comm.open(cfg);
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Connected, 2000);

        QSet<int> seen;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     for (const auto &a : samples)
                                         for (const Sample &s : a.at(0).value<QVector<Sample>>())
                                             seen.insert(s.deviceId);
                                     return seen.size() == 4;
                                 }()),
                                 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!regs.isEmpty() && regs.last().at(2).value<QVector<quint16>>().size() == kHoldingCount,
                                 2000);
        QCOMPARE(regs.last().at(0).toInt(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty() && stats.last().at(0).value<LinkStats>().modbus, 2000);
        QCOMPARE(stats.last().at(0).value<LinkStats>().mbTimeouts, 0u);
    }

    void masterWriteExceptionAndRetry()
    {
        CommManager comm;
        QSignalSpy results(&comm, &CommManager::paramResult);
        LinkConfig cfg;
        cfg.modbus = true;
        cfg.pollMs = 200;
        cfg.deviceIds = {1, 2};
        comm.open(cfg);
        QTRY_COMPARE_WITH_TIMEOUT(comm.state(), LinkState::Connected, 2000);

        comm.writeParam(1, Proto::ParamLoadBias, 40);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
        QVERIFY(results.at(0).at(1).toBool());

        comm.writeParam(1, Proto::ParamLoadBias, 100);   // 从站允许 -50~80
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 2000);
        QVERIFY(!results.at(1).at(1).toBool());
        QVERIFY(results.at(1).at(2).toString().contains(QStringLiteral("非法数据值")));

        comm.writeParam(Proto::kBroadcast, Proto::ParamLoadBias, 0);   // 逐台写并汇总
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 3, 2000);
        QVERIFY(results.at(2).at(1).toBool());
        QVERIFY(results.at(2).at(2).toString().contains(QStringLiteral("成功 2")));

        // 从站全部不应答：重发 2 次后失败
        FaultConfig f;
        f.dropAckPercent = 100;
        comm.setFaults(f);
        comm.writeParam(2, Proto::ParamLoadBias, 10);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 4, 3000);
        QVERIFY(!results.at(3).at(1).toBool());
        QVERIFY(results.at(3).at(2).toString().contains(QStringLiteral("重发 2 次")));
    }

    void masterSurvivesCorruption()
    {
        CommManager comm;
        QSignalSpy stats(&comm, &CommManager::statsUpdated);
        FaultConfig f;
        f.fragment = true;
        f.corruptPermille = 100;
        comm.setFaults(f);
        LinkConfig cfg;
        cfg.modbus = true;
        cfg.pollMs = 50;
        cfg.deviceIds = {1, 2, 3};
        comm.open(cfg);
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty() && stats.last().at(0).value<LinkStats>().crcErrors > 0, 6000);
        QCOMPARE(comm.state(), LinkState::Connected);
    }

#ifdef HAS_SERIALBUS
    // ------------------------------------------------------------ 与 QtSerialBus 官方实现互操作

    // 我们的主站 -> Qt 的 QModbusTcpServer
    void interopMasterWithQtServer()
    {
        QModbusTcpServer server;
        server.setConnectionParameter(QModbusDevice::NetworkPortParameter, 15020);
        server.setConnectionParameter(QModbusDevice::NetworkAddressParameter, QStringLiteral("127.0.0.1"));
        server.setServerAddress(1);
        QModbusDataUnitMap map;
        map.insert(QModbusDataUnit::InputRegisters, {QModbusDataUnit::InputRegisters, 0, kInputCount});
        map.insert(QModbusDataUnit::HoldingRegisters, {QModbusDataUnit::HoldingRegisters, 0, kHoldingCount});
        server.setMap(map);
        quint16 hi, lo;
        fromFloat(123.5f, &hi, &lo);
        server.setData(QModbusDataUnit(QModbusDataUnit::InputRegisters, 0, {456, quint16(qint16(-35)), hi, lo, hi, lo}));
        QVERIFY(server.connectDevice());

        CommManager comm;
        QSignalSpy samples(&comm, &CommManager::samplesReady);
        QSignalSpy results(&comm, &CommManager::paramResult);
        LinkConfig cfg;
        cfg.kind = LinkConfig::Tcp;
        cfg.port = 15020;
        cfg.modbus = true;
        cfg.modbusSlaves = {1};
        cfg.pollMs = 100;
        comm.open(cfg);
        QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty(), 3000);
        const Sample s = samples.first().at(0).value<QVector<Sample>>().first();
        QCOMPARE(s.v[0], 45.6f);
        QCOMPARE(s.v[1], -3.5f);
        QCOMPARE(s.v[2], 123.5f);

        comm.writeParam(1, Proto::ParamLoadBias, 25);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
        QVERIFY(results.at(0).at(1).toBool());
        quint16 v = 0;
        QVERIFY(server.data(QModbusDataUnit::HoldingRegisters, kHoldLoadBias, &v));
        QCOMPARE(v, quint16(25));
    }

    // Qt 的 QModbusTcpClient -> 我们的从站（devsim --modbus 进程）
    void interopQtClientWithOurSlave()
    {
        QProcess sim;
        sim.start(QStringLiteral(DEVSIM_PATH), {QStringLiteral("--modbus"), QStringLiteral("--tcp"),
                                                 QStringLiteral("15021"), QStringLiteral("--devices"), QStringLiteral("3")});
        QVERIFY(sim.waitForStarted());
        QByteArray out;
        QTRY_VERIFY_WITH_TIMEOUT((out += sim.readAllStandardOutput()).contains("Modbus"), 3000);

        QModbusTcpClient client;
        client.setConnectionParameter(QModbusDevice::NetworkPortParameter, 15021);
        client.setConnectionParameter(QModbusDevice::NetworkAddressParameter, QStringLiteral("127.0.0.1"));
        QVERIFY(client.connectDevice());
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), QModbusDevice::ConnectedState, 3000);

        QModbusReply *read = client.sendReadRequest({QModbusDataUnit::InputRegisters, 0, kInputCount}, 2);
        QVERIFY(read);
        QTRY_VERIFY_WITH_TIMEOUT(read->isFinished(), 2000);
        if (read->error() != QModbusDevice::NoError)
            qWarning() << "read error" << read->error() << read->errorString() << read->rawResult();
        QCOMPARE(read->error(), QModbusDevice::NoError);
        const QModbusDataUnit unit = read->result();
        QCOMPARE(unit.valueCount(), qsizetype(kInputCount));
        QVERIFY(unit.value(0) <= 1000);   // CPU ×10
        delete read;

        QModbusReply *write = client.sendWriteRequest({QModbusDataUnit::HoldingRegisters, kHoldLoadBias, QList<quint16>{50}}, 2);
        QTRY_VERIFY_WITH_TIMEOUT(write->isFinished(), 2000);
        QCOMPARE(write->error(), QModbusDevice::NoError);
        delete write;

        // 越界值：Qt 客户端应收到协议异常
        QModbusReply *bad = client.sendWriteRequest({QModbusDataUnit::HoldingRegisters, kHoldLoadBias, QList<quint16>{90}}, 2);
        QTRY_VERIFY_WITH_TIMEOUT(bad->isFinished(), 2000);
        QCOMPARE(bad->error(), QModbusDevice::ProtocolError);
        QCOMPARE(bad->rawResult().exceptionCode(), QModbusPdu::IllegalDataValue);
        delete bad;

        client.disconnectDevice();
        sim.kill();
        sim.waitForFinished();
    }
#endif
};

QTEST_GUILESS_MAIN(TestModbus)
#include "tst_modbus.moc"
