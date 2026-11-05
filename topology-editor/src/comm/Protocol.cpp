#include "comm/Protocol.h"

#include <QtEndian>
#include <cstring>

namespace Proto {

namespace {

// CRC16/MODBUS 查表法：多项式 0xA001（0x8005 反转），初值 0xFFFF
constexpr std::array<quint16, 256> makeCrcTable()
{
    std::array<quint16, 256> table{};
    for (int i = 0; i < 256; ++i) {
        quint16 c = quint16(i);
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? quint16((c >> 1) ^ 0xA001) : quint16(c >> 1);
        table[i] = c;
    }
    return table;
}
constexpr auto kCrcTable = makeCrcTable();

void putU16(QByteArray &b, quint16 v)
{
    char tmp[2];
    qToLittleEndian(v, tmp);
    b.append(tmp, 2);
}

void putI32(QByteArray &b, qint32 v)
{
    char tmp[4];
    qToLittleEndian(v, tmp);
    b.append(tmp, 4);
}

const uchar *bytesOf(QByteArrayView v) { return reinterpret_cast<const uchar *>(v.data()); }

} // namespace

quint16 crc16(QByteArrayView data)
{
    quint16 crc = 0xFFFF;
    for (uchar c : QByteArrayView(data))
        crc = quint16((crc >> 8) ^ kCrcTable[(crc ^ c) & 0xFF]);
    return crc;
}

QByteArray encode(quint8 cmd, quint8 seq, QByteArrayView payload)
{
    Q_ASSERT(payload.size() <= kMaxPayload);
    QByteArray f;
    f.reserve(kHeaderSize + payload.size() + kCrcSize);
    f.append(char(kHead0));
    f.append(char(kHead1));
    f.append(char(cmd));
    f.append(char(seq));
    putU16(f, quint16(payload.size()));
    f.append(payload);
    putU16(f, crc16(QByteArrayView(f).sliced(2)));
    return f;
}

QByteArray encodeTelemetry(const Telemetry &t)
{
    QByteArray p;
    p.reserve(3 + kChannelCount * 4);
    putU16(p, t.deviceId);
    p.append(char(kChannelCount));
    for (float v : t.values) {
        quint32 bits;
        std::memcpy(&bits, &v, 4);
        putI32(p, qint32(bits));
    }
    return p;
}

bool decodeTelemetry(QByteArrayView payload, Telemetry *out)
{
    if (payload.size() < 3)
        return false;
    const uchar *d = bytesOf(payload);
    const int count = d[2];
    if (payload.size() != 3 + count * 4)
        return false;
    out->deviceId = qFromLittleEndian<quint16>(d);
    out->values.fill(0.0f);
    // 通道数多于本机认识的就截断，少了就补 0：协议升级时新旧版本可以互通
    for (int i = 0; i < std::min(count, kChannelCount); ++i) {
        const quint32 bits = qFromLittleEndian<quint32>(d + 3 + i * 4);
        std::memcpy(&out->values[i], &bits, 4);
    }
    return true;
}

QByteArray encodeWriteParam(const WriteParamReq &r)
{
    QByteArray p;
    putU16(p, r.deviceId);
    p.append(char(r.param));
    putI32(p, r.value);
    return p;
}

bool decodeWriteParam(QByteArrayView payload, WriteParamReq *out)
{
    if (payload.size() != 7)
        return false;
    const uchar *d = bytesOf(payload);
    out->deviceId = qFromLittleEndian<quint16>(d);
    out->param = d[2];
    out->value = qFromLittleEndian<qint32>(d + 3);
    return true;
}

QByteArray encodeAck(const AckMsg &a)
{
    QByteArray p;
    p.append(char(a.param));
    p.append(char(a.result));
    putI32(p, a.value);
    return p;
}

bool decodeAck(QByteArrayView payload, AckMsg *out)
{
    if (payload.size() != 6)
        return false;
    const uchar *d = bytesOf(payload);
    out->param = d[0];
    out->result = d[1];
    out->value = qFromLittleEndian<qint32>(d + 2);
    return true;
}

QString cmdName(quint8 cmd)
{
    switch (cmd) {
    case CmdTelemetry: return QStringLiteral("遥测");
    case CmdHeartbeat: return QStringLiteral("心跳");
    case CmdWriteParam: return QStringLiteral("写参数");
    case CmdAck: return QStringLiteral("应答");
    }
    return QStringLiteral("未知(0x%1)").arg(cmd, 2, 16, QLatin1Char('0'));
}

QString paramName(quint8 param)
{
    switch (param) {
    case ParamReportInterval: return QStringLiteral("上报周期");
    case ParamLoadBias: return QStringLiteral("负载偏置");
    }
    return QStringLiteral("参数%1").arg(param);
}

QString ackResultName(quint8 result)
{
    switch (result) {
    case AckOk: return QStringLiteral("成功");
    case AckBadParam: return QStringLiteral("参数号无效");
    case AckOutOfRange: return QStringLiteral("数值越界");
    case AckNoDevice: return QStringLiteral("设备不存在");
    }
    return QStringLiteral("错误码 %1").arg(result);
}

QString toHex(QByteArrayView bytes, int maxBytes)
{
    const qsizetype n = std::min<qsizetype>(bytes.size(), maxBytes);
    QString s = QString::fromLatin1(bytes.first(n).toByteArray().toHex(' ').toUpper());
    if (n < bytes.size())
        s += QStringLiteral(" …(%1 B)").arg(bytes.size());
    return s;
}

// ------------------------------------------------------------------ FrameParser

void FrameParser::reset()
{
    m_buf.clear();
    m_pos = 0;
}

void FrameParser::feed(QByteArrayView bytes, QVector<Frame> *frames, QVector<RawFrame> *raw)
{
    m_buf.append(bytes);
    const uchar *d = reinterpret_cast<const uchar *>(m_buf.constData());
    const qsizetype n = m_buf.size();

    for (;;) {
        // 1. 找帧头 AA 55
        qsizetype h = m_pos;
        while (h + 1 < n && !(d[h] == kHead0 && d[h + 1] == kHead1))
            ++h;
        if (h + 1 >= n) {
            // 没找到完整帧头。最后一个字节如果是 AA，可能是下一帧帧头的前半，先留着
            const qsizetype keep = (n > m_pos && d[n - 1] == kHead0) ? n - 1 : n;
            m_stats.discardedBytes += quint64(keep - m_pos);
            m_pos = keep;
            break;
        }
        m_stats.discardedBytes += quint64(h - m_pos);
        m_pos = h;

        // 2. 帧头不完整：半包，等下一批
        if (n - m_pos < kHeaderSize)
            break;
        const quint16 len = qFromLittleEndian<quint16>(d + m_pos + 4);
        if (len > kMaxPayload) {
            // 长度字段离谱，说明这个 AA 55 不是真帧头，跳过一个字节重新找
            ++m_stats.lengthErrors;
            ++m_stats.discardedBytes;
            ++m_pos;
            continue;
        }
        const qsizetype total = kHeaderSize + len + kCrcSize;
        if (n - m_pos < total)
            break;

        // 3. 校验
        const QByteArrayView body(m_buf.constData() + m_pos + 2, kHeaderSize - 2 + len);
        const quint16 crc = qFromLittleEndian<quint16>(d + m_pos + kHeaderSize + len);
        if (crc16(body) != crc) {
            ++m_stats.crcErrors;
            if (raw)
                raw->append({QByteArray(m_buf.constData() + m_pos, total), false});
            ++m_stats.discardedBytes;
            ++m_pos;   // 只跳一个字节：真正的下一帧可能就在这段数据里面
            continue;
        }

        Frame f;
        f.cmd = d[m_pos + 2];
        f.seq = d[m_pos + 3];
        f.payload = QByteArray(m_buf.constData() + m_pos + kHeaderSize, len);
        frames->append(std::move(f));
        if (raw)
            raw->append({QByteArray(m_buf.constData() + m_pos, total), true});
        ++m_stats.frames;
        m_pos += total;
    }

    // 已消费的部分攒到一定量再搬移，避免每批都 memmove
    if (m_pos == m_buf.size()) {
        m_buf.clear();
        m_pos = 0;
    } else if (m_pos > 4096) {
        m_buf.remove(0, m_pos);
        m_pos = 0;
    }
}

} // namespace Proto
