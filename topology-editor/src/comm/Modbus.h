#pragma once

#include "comm/CommTypes.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <deque>
#include <functional>
#include <optional>

class DeviceSimCore;

// Modbus RTU / TCP 编解码（手写实现，不依赖 QtSerialBus，所以浏览器版也能用；
// 单元测试里拿 QtSerialBus 的官方实现做互操作验证）。
//
// 寄存器表（每台设备一个从站，从站地址 = 设备 ID）：
//   输入寄存器 FC04（只读）
//     30001  CPU 使用率   uint16  ×0.1 %
//     30002  温度         int16   ×0.1 ℃
//     30003-30004  入流量 float32 Mbps，高字在前（ABCD）
//     30005-30006  出流量 float32 Mbps，高字在前（ABCD）
//   保持寄存器 FC03/06/16
//     40001  采样周期     uint16  ms，20~5000（全局）
//     40002  负载偏置     int16   %，-50~80
namespace Modbus {

enum Function : quint8 {
    FcReadHolding = 0x03,
    FcReadInput = 0x04,
    FcWriteSingle = 0x06,
    FcWriteMultiple = 0x10,
};

enum ExceptionCode : quint8 {
    ExIllegalFunction = 0x01,
    ExIllegalAddress = 0x02,
    ExIllegalValue = 0x03,
    ExSlaveFailure = 0x04,
    ExGatewayNoResponse = 0x0B,   // TCP 网关：目标从站无响应
};

constexpr int kInputCount = 6;
constexpr int kHoldingCount = 2;
constexpr quint16 kHoldInterval = 0;
constexpr quint16 kHoldLoadBias = 1;

// PDU 构造
QByteArray readRequest(quint8 fc, quint16 addr, quint16 count);
QByteArray writeSingleRequest(quint16 addr, quint16 value);
QByteArray writeMultipleRequest(quint16 addr, const QVector<quint16> &values);
QByteArray exceptionResponse(quint8 fc, quint8 code);

// ADU 封装：RTU = 从站 + PDU + CRC16（低字节在前）；TCP = MBAP 头 + PDU
QByteArray rtuFrame(quint8 slave, QByteArrayView pdu);
QByteArray tcpFrame(quint16 transactionId, quint8 unit, QByteArrayView pdu);
bool rtuCrcOk(QByteArrayView frame);

// RTU 没有长度字段，只能按功能码推算帧长。返回 -1 表示还不够判断，0 表示无法识别
int rtuRequestLength(QByteArrayView buf);
int rtuResponseLength(QByteArrayView buf);

quint16 be16(const char *p);
QVector<quint16> registersOf(QByteArrayView data, int count);
float toFloat(quint16 hi, quint16 lo);   // ABCD 字序
void fromFloat(float v, quint16 *hi, quint16 *lo);

QString functionName(quint8 fc);
QString exceptionName(quint8 code);
QString describe(QByteArrayView adu, bool tcp);   // 报文监视用的一句话说明

} // namespace Modbus

// 从站模拟：读写的是 DeviceSimCore 里各设备的实时值和参数
class ModbusSlaveSim
{
public:
    explicit ModbusSlaveSim(DeviceSimCore &core) : m_core(core) {}

    QByteArray handleRtu(QByteArrayView bytes);   // 串口字节流，可能半帧/多帧
    QByteArray handleTcp(QByteArrayView bytes);   // TCP 字节流，按 MBAP 头切帧

    // 处理一个请求 PDU，返回应答 PDU；*respond 为 false 表示不应答（广播或设备不存在）
    QByteArray processPdu(int unit, QByteArrayView pdu, bool tcp, bool *respond);

private:
    QVector<quint16> inputRegisters(int unit) const;
    quint16 holdingRegister(int unit, quint16 addr) const;

    DeviceSimCore &m_core;
    QByteArray m_rtuBuf;
    QByteArray m_tcpBuf;
};

// 主站轮询引擎（运行在通信线程，由 CommWorker 驱动）：
//   - 一问一答，同一时刻只有一个未完成的请求（RTU 总线是半双工）；
//   - 每个轮询周期依次读每个从站的输入寄存器，正在查看的从站额外读保持寄存器；
//   - 写操作优先插队，超时重发 2 次；广播写拆成逐台写，汇总结果；
//   - 某从站连续 3 次无应答就暂停轮询 5 秒，避免一台掉线拖慢整条总线。
class ModbusMaster
{
public:
    enum class Framing { Rtu, Tcp };

    struct Stats
    {
        quint64 requests = 0;
        quint64 responses = 0;
        quint64 timeouts = 0;
        quint64 exceptions = 0;
        quint64 crcErrors = 0;
        quint64 discardedBytes = 0;
        quint64 retries = 0;
        double rttSumMs = 0;
    };

    struct Hooks
    {
        std::function<void(const QByteArray &)> send;
        std::function<void(const Sample &)> sample;
        std::function<void(int slave, const QVector<quint16> &input, const QVector<quint16> &holding)> registers;
        std::function<void(int requestId, bool ok, const QString &text)> paramResult;
        std::function<void(const QByteArray &frame, bool crcOk)> received;
        std::function<void(qint64 now)> alive;   // 收到任何合法应答
    };

    static constexpr int kTimeoutMs = 300;
    static constexpr int kWriteTries = 3;
    static constexpr int kFailsBeforeSkip = 3;
    static constexpr int kSkipMs = 5000;

    ModbusMaster(Framing framing, const QList<int> &slaves, int pollMs, Hooks hooks);

    void reset(const QString &failWhy);   // 链路断开：清缓冲，未完成的写操作报失败
    void onBytes(QByteArrayView bytes, qint64 now);
    void tick(qint64 now);
    void writeParam(int requestId, int deviceId, int param, int value);
    void setWatch(int slave);
    const Stats &stats() const { return m_stats; }
    Framing framing() const { return m_framing; }

private:
    enum class Kind { PollInput, PollHolding, Write };
    struct Txn
    {
        Kind kind;
        int slave;
        QByteArray pdu;
        int tries = 0;
        int requestId = 0;
        int group = 0;
        QString what;
    };
    struct Group
    {
        int requestId;
        int total;
        int done = 0;
        int ok = 0;
        QString what;
        QStringList errors;
    };

    void send(Txn t, qint64 now);
    void handlePdu(QByteArrayView pdu, qint64 now);
    void failCurrent(const QString &why, qint64 now, bool countTimeout);
    void finishWrite(const Txn &t, bool ok, const QString &text);
    void startCycle(qint64 now);

    Framing m_framing;
    QList<int> m_slaves;
    int m_pollMs;
    Hooks m_h;

    std::optional<Txn> m_cur;
    qint64 m_sentAt = 0;
    qint64 m_deadline = 0;
    quint16 m_tid = 0;
    QByteArray m_buf;

    std::deque<Txn> m_writes;
    std::deque<Txn> m_cycle;
    qint64 m_nextCycle = 0;
    QHash<int, int> m_fails;
    QHash<int, qint64> m_skipUntil;

    int m_watch = 0;
    QVector<quint16> m_watchInput;
    QVector<quint16> m_watchHolding;

    QHash<int, Group> m_groups;
    int m_nextGroup = 1;
    Stats m_stats;
};
