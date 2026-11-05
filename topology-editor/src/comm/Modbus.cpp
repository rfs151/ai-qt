#include "comm/Modbus.h"
#include "comm/DeviceSim.h"

#include <QtEndian>
#include <cstring>

namespace Modbus {

namespace {
void putBe16(QByteArray &b, quint16 v)
{
    b.append(char(v >> 8));
    b.append(char(v & 0xFF));
}
quint8 u8(QByteArrayView v, qsizetype i) { return quint8(v[i]); }
} // namespace

quint16 be16(const char *p)
{
    return quint16((quint8(p[0]) << 8) | quint8(p[1]));
}

QByteArray readRequest(quint8 fc, quint16 addr, quint16 count)
{
    QByteArray p(1, char(fc));
    putBe16(p, addr);
    putBe16(p, count);
    return p;
}

QByteArray writeSingleRequest(quint16 addr, quint16 value)
{
    QByteArray p(1, char(FcWriteSingle));
    putBe16(p, addr);
    putBe16(p, value);
    return p;
}

QByteArray writeMultipleRequest(quint16 addr, const QVector<quint16> &values)
{
    QByteArray p(1, char(FcWriteMultiple));
    putBe16(p, addr);
    putBe16(p, quint16(values.size()));
    p.append(char(values.size() * 2));
    for (quint16 v : values)
        putBe16(p, v);
    return p;
}

QByteArray exceptionResponse(quint8 fc, quint8 code)
{
    QByteArray p;
    p.append(char(fc | 0x80));
    p.append(char(code));
    return p;
}

QByteArray rtuFrame(quint8 slave, QByteArrayView pdu)
{
    QByteArray f(1, char(slave));
    f.append(pdu);
    const quint16 crc = Proto::crc16(f);   // 同一个 CRC16/MODBUS
    f.append(char(crc & 0xFF));            // RTU 的 CRC 低字节在前，和寄存器的大端相反
    f.append(char(crc >> 8));
    return f;
}

QByteArray tcpFrame(quint16 transactionId, quint8 unit, QByteArrayView pdu)
{
    QByteArray f;
    putBe16(f, transactionId);
    putBe16(f, 0);                            // 协议标识，Modbus 固定为 0
    putBe16(f, quint16(pdu.size() + 1));      // 后续字节数：单元标识 + PDU
    f.append(char(unit));
    f.append(pdu);
    return f;
}

bool rtuCrcOk(QByteArrayView frame)
{
    if (frame.size() < 4)
        return false;
    const quint16 crc = Proto::crc16(frame.first(frame.size() - 2));
    return u8(frame, frame.size() - 2) == (crc & 0xFF) && u8(frame, frame.size() - 1) == (crc >> 8);
}

int rtuRequestLength(QByteArrayView buf)
{
    if (buf.size() < 2)
        return -1;
    switch (u8(buf, 1)) {
    case FcReadHolding:
    case FcReadInput:
    case FcWriteSingle:
        return 8;
    case FcWriteMultiple:
        return buf.size() < 7 ? -1 : 9 + u8(buf, 6);
    default:
        return 0;
    }
}

int rtuResponseLength(QByteArrayView buf)
{
    if (buf.size() < 2)
        return -1;
    const quint8 fc = u8(buf, 1);
    if (fc & 0x80)
        return 5;
    switch (fc) {
    case FcReadHolding:
    case FcReadInput:
        return buf.size() < 3 ? -1 : 5 + u8(buf, 2);
    case FcWriteSingle:
    case FcWriteMultiple:
        return 8;
    default:
        return 0;
    }
}

QVector<quint16> registersOf(QByteArrayView data, int count)
{
    QVector<quint16> regs;
    regs.reserve(count);
    for (int i = 0; i < count && (i + 1) * 2 <= data.size(); ++i)
        regs.append(be16(data.data() + i * 2));
    return regs;
}

float toFloat(quint16 hi, quint16 lo)
{
    const quint32 bits = (quint32(hi) << 16) | lo;
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

void fromFloat(float v, quint16 *hi, quint16 *lo)
{
    quint32 bits;
    std::memcpy(&bits, &v, 4);
    *hi = quint16(bits >> 16);
    *lo = quint16(bits & 0xFFFF);
}

QString functionName(quint8 fc)
{
    switch (fc & 0x7F) {
    case FcReadHolding: return QStringLiteral("读保持寄存器");
    case FcReadInput: return QStringLiteral("读输入寄存器");
    case FcWriteSingle: return QStringLiteral("写单个寄存器");
    case FcWriteMultiple: return QStringLiteral("写多个寄存器");
    }
    return QStringLiteral("功能码 0x%1").arg(fc, 2, 16, QLatin1Char('0'));
}

QString exceptionName(quint8 code)
{
    switch (code) {
    case ExIllegalFunction: return QStringLiteral("非法功能码");
    case ExIllegalAddress: return QStringLiteral("非法数据地址");
    case ExIllegalValue: return QStringLiteral("非法数据值");
    case ExSlaveFailure: return QStringLiteral("从站设备故障");
    case ExGatewayNoResponse: return QStringLiteral("网关目标无响应");
    }
    return QStringLiteral("异常码 %1").arg(code);
}

QString describe(QByteArrayView adu, bool tcp)
{
    const qsizetype head = tcp ? 7 : 1;
    if (adu.size() < head + 1)
        return QStringLiteral("帧太短");
    const int unit = u8(adu, head - 1);
    const QByteArrayView pdu = adu.sliced(head, adu.size() - head - (tcp ? 0 : 2));
    const quint8 fc = u8(pdu, 0);
    if (fc & 0x80)
        return QStringLiteral("从站%1 异常 %2").arg(unit).arg(pdu.size() > 1 ? exceptionName(u8(pdu, 1)) : QString());
    return QStringLiteral("从站%1 FC%2 %3").arg(unit).arg(fc, 2, 10, QLatin1Char('0')).arg(functionName(fc));
}

} // namespace Modbus

using namespace Modbus;

// ------------------------------------------------------------------ ModbusSlaveSim

QVector<quint16> ModbusSlaveSim::inputRegisters(int unit) const
{
    QVector<quint16> r(kInputCount, 0);
    const Proto::Telemetry *t = m_core.latest(unit);
    if (!t)
        return r;
    r[0] = quint16(std::clamp(qRound(t->values[0] * 10), 0, 65535));
    r[1] = quint16(qint16(std::clamp(qRound(t->values[1] * 10), -32768, 32767)));
    fromFloat(t->values[2], &r[2], &r[3]);
    fromFloat(t->values[3], &r[4], &r[5]);
    return r;
}

quint16 ModbusSlaveSim::holdingRegister(int unit, quint16 addr) const
{
    if (addr == kHoldInterval)
        return quint16(m_core.paramValue(unit, Proto::ParamReportInterval));
    return quint16(qint16(m_core.paramValue(unit, Proto::ParamLoadBias)));
}

QByteArray ModbusSlaveSim::processPdu(int unit, QByteArrayView pdu, bool tcp, bool *respond)
{
    *respond = true;
    if (pdu.isEmpty()) {
        *respond = false;
        return {};
    }
    const quint8 fc = quint8(pdu[0]);
    const bool broadcast = unit == 0;
    if (!broadcast && !m_core.hasDevice(unit)) {
        // RTU：总线上没有这个地址，自然没人回；TCP 网关：回"目标无响应"异常
        *respond = tcp;
        return exceptionResponse(fc, ExGatewayNoResponse);
    }
    if (broadcast)
        *respond = false;   // 广播只执行写操作，不应答
    const quint16 target = broadcast ? Proto::kBroadcast : quint16(unit);

    // 保持寄存器值 -> 设备参数，先检查范围
    auto toParam = [](quint16 addr) { return addr == kHoldInterval ? Proto::ParamReportInterval : Proto::ParamLoadBias; };
    auto toValue = [](quint16 addr, quint16 raw) { return addr == kHoldInterval ? qint32(raw) : qint32(qint16(raw)); };
    auto inRange = [](quint16 addr, qint32 v) { return addr == kHoldInterval ? (v >= 20 && v <= 5000) : (v >= -50 && v <= 80); };

    switch (fc) {
    case FcReadHolding:
    case FcReadInput: {
        if (pdu.size() != 5 || broadcast)
            return exceptionResponse(fc, ExIllegalValue);
        const quint16 addr = be16(pdu.data() + 1);
        const quint16 count = be16(pdu.data() + 3);
        const int size = fc == FcReadInput ? kInputCount : kHoldingCount;
        if (count < 1 || count > 125)
            return exceptionResponse(fc, ExIllegalValue);
        if (addr + count > size)
            return exceptionResponse(fc, ExIllegalAddress);
        const QVector<quint16> input = fc == FcReadInput ? inputRegisters(unit) : QVector<quint16>{};
        QByteArray r(1, char(fc));
        r.append(char(count * 2));
        for (int i = 0; i < count; ++i) {
            const quint16 v = fc == FcReadInput ? input[addr + i] : holdingRegister(unit, quint16(addr + i));
            r.append(char(v >> 8));
            r.append(char(v & 0xFF));
        }
        return r;
    }
    case FcWriteSingle: {
        if (pdu.size() != 5)
            return exceptionResponse(fc, ExIllegalValue);
        const quint16 addr = be16(pdu.data() + 1);
        const quint16 raw = be16(pdu.data() + 3);
        if (addr >= kHoldingCount)
            return exceptionResponse(fc, ExIllegalAddress);
        if (!inRange(addr, toValue(addr, raw)))
            return exceptionResponse(fc, ExIllegalValue);
        m_core.applyParam(target, toParam(addr), toValue(addr, raw));
        return pdu.toByteArray();   // 写单个寄存器的应答就是请求原样回显
    }
    case FcWriteMultiple: {
        if (pdu.size() < 6)
            return exceptionResponse(fc, ExIllegalValue);
        const quint16 addr = be16(pdu.data() + 1);
        const quint16 count = be16(pdu.data() + 3);
        const int bytes = quint8(pdu[5]);
        if (count < 1 || count > 123 || bytes != count * 2 || pdu.size() != 6 + bytes)
            return exceptionResponse(fc, ExIllegalValue);
        if (addr + count > kHoldingCount)
            return exceptionResponse(fc, ExIllegalAddress);
        // 先全部校验再写入：要么全部生效，要么都不生效
        for (int i = 0; i < count; ++i)
            if (!inRange(quint16(addr + i), toValue(quint16(addr + i), be16(pdu.data() + 6 + i * 2))))
                return exceptionResponse(fc, ExIllegalValue);
        for (int i = 0; i < count; ++i) {
            const quint16 a = quint16(addr + i);
            m_core.applyParam(target, toParam(a), toValue(a, be16(pdu.data() + 6 + i * 2)));
        }
        return pdu.first(5).toByteArray();   // 应答：功能码 + 起始地址 + 数量
    }
    default:
        return exceptionResponse(fc, ExIllegalFunction);
    }
}

QByteArray ModbusSlaveSim::handleRtu(QByteArrayView bytes)
{
    m_rtuBuf.append(bytes);
    QByteArray out;
    for (;;) {
        int len = rtuRequestLength(m_rtuBuf);
        // 不认识的功能码推算不出帧长。真实从站靠 3.5 字符的总线静默切帧，
        // 这里近似处理：缓冲区整体 CRC 正确就当作一帧，好回"非法功能码"异常
        if (len == 0 && rtuCrcOk(m_rtuBuf))
            len = int(m_rtuBuf.size());
        if (len < 0 || m_rtuBuf.size() < len)
            break;   // 半帧，等后续字节
        if (len == 0 || !rtuCrcOk(QByteArrayView(m_rtuBuf).first(len))) {
            m_rtuBuf.remove(0, 1);   // 不认识或校验错：真实从站会静默丢弃，这里跳一个字节重新找帧
            continue;
        }
        const int unit = quint8(m_rtuBuf[0]);
        const QByteArray pdu = m_rtuBuf.mid(1, len - 3);
        m_rtuBuf.remove(0, len);
        bool respond = false;
        const QByteArray resp = processPdu(unit, pdu, false, &respond);
        if (respond && !m_core.rollDropAck())
            out += m_core.maybeCorrupt(rtuFrame(quint8(unit), resp));
    }
    return out;
}

QByteArray ModbusSlaveSim::handleTcp(QByteArrayView bytes)
{
    m_tcpBuf.append(bytes);
    QByteArray out;
    while (m_tcpBuf.size() >= 7) {
        const quint16 pid = be16(m_tcpBuf.constData() + 2);
        const quint16 len = be16(m_tcpBuf.constData() + 4);
        if (pid != 0 || len < 2 || len > 254) {
            m_tcpBuf.clear();   // TCP 流里出现非法头，无法再同步，丢掉缓冲
            break;
        }
        if (m_tcpBuf.size() < 6 + len)
            break;
        const quint16 tid = be16(m_tcpBuf.constData());
        const int unit = quint8(m_tcpBuf[6]);
        const QByteArray pdu = m_tcpBuf.mid(7, len - 1);
        m_tcpBuf.remove(0, 6 + len);
        bool respond = false;
        const QByteArray resp = processPdu(unit, pdu, true, &respond);
        if (respond && !m_core.rollDropAck())
            out += tcpFrame(tid, quint8(unit), resp);
    }
    return out;
}

// ------------------------------------------------------------------ ModbusMaster

ModbusMaster::ModbusMaster(Framing framing, const QList<int> &slaves, int pollMs, Hooks hooks)
    : m_framing(framing), m_pollMs(pollMs), m_h(std::move(hooks))
{
    for (int s : slaves)
        if (s >= 1 && s <= 247)   // Modbus 从站地址范围
            m_slaves << s;
}

void ModbusMaster::setWatch(int slave)
{
    m_watch = slave;
    m_watchInput.clear();
    m_watchHolding.clear();
}

void ModbusMaster::reset(const QString &failWhy)
{
    if (m_cur && m_cur->kind == Kind::Write)
        finishWrite(*m_cur, false, failWhy);
    for (const Txn &t : m_writes)
        finishWrite(t, false, failWhy);
    m_cur.reset();
    m_writes.clear();
    m_cycle.clear();
    m_buf.clear();
    m_nextCycle = 0;
    m_skipUntil.clear();
    m_fails.clear();
}

void ModbusMaster::startCycle(qint64 now)
{
    m_nextCycle = now + m_pollMs;
    for (int s : std::as_const(m_slaves)) {
        if (m_skipUntil.value(s) > now)
            continue;   // 连续无应答的从站暂时跳过
        m_cycle.push_back({Kind::PollInput, s, readRequest(FcReadInput, 0, kInputCount)});
        if (s == m_watch)
            m_cycle.push_back({Kind::PollHolding, s, readRequest(FcReadHolding, 0, kHoldingCount)});
    }
}

void ModbusMaster::tick(qint64 now)
{
    if (m_cur && now >= m_deadline)
        failCurrent(QStringLiteral("应答超时"), now, true);
    if (m_cur)
        return;

    // 写操作优先：用户点了"下发"不必等一整轮轮询
    if (!m_writes.empty()) {
        Txn t = m_writes.front();
        m_writes.pop_front();
        send(t, now);
        return;
    }
    if (m_cycle.empty() && now >= m_nextCycle)
        startCycle(now);
    if (!m_cycle.empty()) {
        Txn t = m_cycle.front();
        m_cycle.pop_front();
        send(t, now);
    }
}

void ModbusMaster::send(Txn t, qint64 now)
{
    ++t.tries;
    if (t.tries > 1)
        ++m_stats.retries;
    const QByteArray adu = m_framing == Framing::Rtu ? rtuFrame(quint8(t.slave), t.pdu)
                                                     : tcpFrame(++m_tid, quint8(t.slave), t.pdu);
    m_buf.clear();   // 发新请求前丢掉残留字节（比如上一个请求超时后才到的应答）
    m_cur = t;
    m_sentAt = now;
    m_deadline = now + kTimeoutMs;
    ++m_stats.requests;
    m_h.send(adu);
}

void ModbusMaster::failCurrent(const QString &why, qint64 now, bool countTimeout)
{
    Txn t = *m_cur;
    m_cur.reset();
    m_buf.clear();
    if (countTimeout)
        ++m_stats.timeouts;
    if (t.kind == Kind::Write) {
        if (t.tries < kWriteTries)
            m_writes.push_front(t);   // 同一个请求重发
        else
            finishWrite(t, false, QStringLiteral("%1（已重发 %2 次）").arg(why).arg(t.tries - 1));
        return;
    }
    // 轮询失败不重发，下一轮自然会再读；连续失败则暂停该从站
    if (++m_fails[t.slave] >= kFailsBeforeSkip) {
        m_skipUntil[t.slave] = now + kSkipMs;
        m_fails[t.slave] = 0;
    }
}

void ModbusMaster::onBytes(QByteArrayView bytes, qint64 now)
{
    if (!m_cur) {
        m_stats.discardedBytes += quint64(bytes.size());   // 没有在等应答：迟到的或者噪声
        return;
    }
    m_buf.append(bytes);

    if (m_framing == Framing::Rtu) {
        while (m_cur && m_buf.size() >= 2) {
            if (quint8(m_buf[0]) != m_cur->slave) {
                m_buf.remove(0, 1);   // 应答必须以当前从站地址开头
                ++m_stats.discardedBytes;
                continue;
            }
            const int len = rtuResponseLength(m_buf);
            if (len < 0 || m_buf.size() < len)
                return;
            if (len == 0) {
                m_buf.remove(0, 1);
                ++m_stats.discardedBytes;
                continue;
            }
            const QByteArray frame = m_buf.left(len);
            m_buf.remove(0, len);
            if (!rtuCrcOk(frame)) {
                ++m_stats.crcErrors;
                if (m_h.received)
                    m_h.received(frame, false);
                failCurrent(QStringLiteral("CRC 错误"), now, false);   // 写操作会立即重发，不用等超时
                return;
            }
            if (m_h.received)
                m_h.received(frame, true);
            handlePdu(QByteArrayView(frame).sliced(1, len - 3), now);
        }
        return;
    }

    // TCP：MBAP 头自带长度，按事务号匹配
    while (m_cur && m_buf.size() >= 7) {
        const quint16 len = be16(m_buf.constData() + 4);
        if (be16(m_buf.constData() + 2) != 0 || len < 2 || len > 254) {
            m_stats.discardedBytes += quint64(m_buf.size());
            m_buf.clear();
            return;
        }
        if (m_buf.size() < 6 + len)
            return;
        const QByteArray frame = m_buf.left(6 + len);
        m_buf.remove(0, 6 + len);
        if (be16(frame.constData()) != m_tid) {
            m_stats.discardedBytes += quint64(frame.size());   // 旧事务的迟到应答
            continue;
        }
        if (m_h.received)
            m_h.received(frame, true);
        handlePdu(QByteArrayView(frame).sliced(7), now);
    }
}

void ModbusMaster::handlePdu(QByteArrayView pdu, qint64 now)
{
    const Txn t = *m_cur;
    m_cur.reset();
    ++m_stats.responses;
    m_stats.rttSumMs += double(now - m_sentAt);
    m_fails[t.slave] = 0;
    if (m_h.alive)
        m_h.alive(now);

    const quint8 expect = quint8(t.pdu[0]);
    const quint8 fc = quint8(pdu[0]);
    if (fc == (expect | 0x80)) {
        ++m_stats.exceptions;
        const QString ex = pdu.size() > 1 ? exceptionName(quint8(pdu[1])) : QString();
        if (t.kind == Kind::Write)
            finishWrite(t, false, QStringLiteral("从站返回异常：%1").arg(ex));
        return;
    }
    if (fc != expect)
        return;

    switch (t.kind) {
    case Kind::PollInput: {
        if (pdu.size() < 2 + kInputCount * 2)
            return;
        const QVector<quint16> r = registersOf(pdu.sliced(2), kInputCount);
        Sample s;
        s.deviceId = t.slave;
        s.tMs = now;
        s.v = {r[0] / 10.0f, qint16(r[1]) / 10.0f, toFloat(r[2], r[3]), toFloat(r[4], r[5])};
        m_h.sample(s);
        if (t.slave == m_watch) {
            m_watchInput = r;
            if (m_h.registers)
                m_h.registers(t.slave, m_watchInput, m_watchHolding);
        }
        break;
    }
    case Kind::PollHolding:
        if (pdu.size() >= 2 + kHoldingCount * 2 && t.slave == m_watch) {
            m_watchHolding = registersOf(pdu.sliced(2), kHoldingCount);
            if (m_h.registers)
                m_h.registers(t.slave, m_watchInput, m_watchHolding);
        }
        break;
    case Kind::Write:
        // 写单个寄存器的应答应与请求完全一致
        finishWrite(t, pdu == QByteArrayView(t.pdu), pdu == QByteArrayView(t.pdu) ? QStringLiteral("成功")
                                                                                     : QStringLiteral("回显不一致"));
        break;
    }
}

void ModbusMaster::writeParam(int requestId, int deviceId, int param, int value)
{
    quint16 addr;
    if (param == Proto::ParamReportInterval)
        addr = kHoldInterval;
    else if (param == Proto::ParamLoadBias)
        addr = kHoldLoadBias;
    else {
        m_h.paramResult(requestId, false, QStringLiteral("寄存器表里没有这个参数"));
        return;
    }
    const QString what = QStringLiteral("%1(4%2) = %3")
                             .arg(Proto::paramName(quint8(param)))
                             .arg(addr + 1, 4, 10, QLatin1Char('0'))
                             .arg(value);
    const QByteArray pdu = writeSingleRequest(addr, quint16(qint16(value)));

    if (deviceId != Proto::kBroadcast) {
        m_writes.push_back({Kind::Write, deviceId, pdu, 0, requestId, 0, what});
        return;
    }
    // 广播：Modbus 的地址 0 广播没有应答，无法确认结果，所以逐台写入并汇总
    const int g = m_nextGroup++;
    m_groups.insert(g, {requestId, int(m_slaves.size()), 0, 0, what, {}});
    for (int s : std::as_const(m_slaves))
        m_writes.push_back({Kind::Write, s, pdu, 0, requestId, g, what});
    if (m_slaves.isEmpty()) {
        m_groups.remove(g);
        m_h.paramResult(requestId, false, QStringLiteral("没有从站"));
    }
}

void ModbusMaster::finishWrite(const Txn &t, bool ok, const QString &text)
{
    if (t.group == 0) {
        QString msg = QStringLiteral("从站%1 %2：%3").arg(t.slave).arg(t.what, text);
        if (ok && t.tries > 1)
            msg += QStringLiteral("（重发 %1 次）").arg(t.tries - 1);
        m_h.paramResult(t.requestId, ok, msg);
        return;
    }
    auto it = m_groups.find(t.group);
    if (it == m_groups.end())
        return;
    Group &g = it.value();
    ++g.done;
    if (ok)
        ++g.ok;
    else if (g.errors.size() < 3)
        g.errors << QStringLiteral("从站%1 %2").arg(t.slave).arg(text);
    if (g.done < g.total)
        return;
    QString msg = QStringLiteral("逐台写 %1 个从站 %2：成功 %3").arg(g.total).arg(g.what).arg(g.ok);
    if (g.ok < g.total)
        msg += QStringLiteral("，失败 %1（%2）").arg(g.total - g.ok).arg(g.errors.join(QStringLiteral("；")));
    m_h.paramResult(g.requestId, g.ok == g.total, msg);
    m_groups.erase(it);
}
