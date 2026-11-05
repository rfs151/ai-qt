#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QVector>
#include <array>

// 设备通信协议（小端）：
//
//   AA 55 | cmd u8 | seq u8 | len u16 | payload[len] | crc16 u16
//
// CRC16/MODBUS 覆盖 cmd 到 payload 末尾。seq 由发起方递增，应答原样带回，
// 重发时沿用同一个 seq，接收方据此去重。
namespace Proto {

constexpr quint8 kHead0 = 0xAA;
constexpr quint8 kHead1 = 0x55;
constexpr int kHeaderSize = 6;
constexpr int kCrcSize = 2;
constexpr int kMaxPayload = 512;
constexpr int kChannelCount = 4;   // CPU% / 温度 / 入流量 / 出流量
constexpr quint16 kBroadcast = 0xFFFF;

enum Cmd : quint8 {
    CmdTelemetry = 0x01,   // 设备 -> 上位机：一组采样值
    CmdHeartbeat = 0x02,   // 设备 -> 上位机：心跳
    CmdWriteParam = 0x10,  // 上位机 -> 设备：写参数
    CmdAck = 0x90,         // 设备 -> 上位机：写参数应答
};

enum Param : quint8 {
    ParamReportInterval = 1,   // 上报周期 ms，全局
    ParamLoadBias = 2,         // 负载偏置 %，叠加到 CPU 上，用来人为触发告警
};

enum AckResult : quint8 { AckOk = 0, AckBadParam = 1, AckOutOfRange = 2, AckNoDevice = 3 };

struct Frame
{
    quint8 cmd = 0;
    quint8 seq = 0;
    QByteArray payload;
};

struct RawFrame
{
    QByteArray bytes;
    bool crcOk = true;
};

struct Telemetry
{
    quint16 deviceId = 0;
    std::array<float, kChannelCount> values{};
};

struct WriteParamReq
{
    quint16 deviceId = 0;
    quint8 param = 0;
    qint32 value = 0;
};

struct AckMsg
{
    quint8 param = 0;
    quint8 result = 0;
    qint32 value = 0;
};

quint16 crc16(QByteArrayView data);
QByteArray encode(quint8 cmd, quint8 seq, QByteArrayView payload = {});

QByteArray encodeTelemetry(const Telemetry &t);
bool decodeTelemetry(QByteArrayView payload, Telemetry *out);
QByteArray encodeWriteParam(const WriteParamReq &r);
bool decodeWriteParam(QByteArrayView payload, WriteParamReq *out);
QByteArray encodeAck(const AckMsg &a);
bool decodeAck(QByteArrayView payload, AckMsg *out);

QString cmdName(quint8 cmd);
QString paramName(quint8 param);
QString ackResultName(quint8 result);
QString toHex(QByteArrayView bytes, int maxBytes = 40);

// 流式帧解析：处理半包（数据不够就等下一批）、粘包（一批里多帧）、
// 帧头前的垃圾字节、CRC 错误（跳过一个字节重新找帧头）。
class FrameParser
{
public:
    struct Stats
    {
        quint64 frames = 0;
        quint64 crcErrors = 0;
        quint64 lengthErrors = 0;
        quint64 discardedBytes = 0;
    };

    // 追加收到的字节，解析出的完整帧追加到 frames；raw 不为空时同时记录原始帧（含 CRC 错误帧）
    void feed(QByteArrayView bytes, QVector<Frame> *frames, QVector<RawFrame> *raw = nullptr);
    void reset();

    const Stats &stats() const { return m_stats; }
    qsizetype buffered() const { return m_buf.size() - m_pos; }

private:
    QByteArray m_buf;
    qsizetype m_pos = 0;   // 已消费到的位置，攒够再一次性 remove，避免每帧搬移内存
    Stats m_stats;
};

} // namespace Proto
