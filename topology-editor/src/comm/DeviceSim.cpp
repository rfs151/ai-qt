#include "comm/DeviceSim.h"
#include "comm/Modbus.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QtMath>

qint64 Clock::nowMs()
{
    // 函数内静态变量的初始化是线程安全的；elapsed() 是只读操作
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

// ------------------------------------------------------------------ DeviceSimCore

DeviceSimCore::DeviceSimCore(const QList<int> &deviceIds, quint32 seed) : m_rng(seed)
{
    m_devs.reserve(deviceIds.size());
    for (int id : deviceIds) {
        Dev d;
        d.id = quint16(id);
        d.base = 15 + m_rng.bounded(30.0);
        d.phase = m_rng.bounded(2 * M_PI);
        d.period = 20 + m_rng.bounded(40.0);
        d.traffic = 60 + m_rng.bounded(340.0);
        d.temp = 38 + d.base * 0.45;
        m_devs.append(d);
    }
}

Proto::Telemetry DeviceSimCore::sample(Dev &d, qint64 nowMs)
{
    const double t = nowMs / 1000.0;
    auto noise = [this](double amp) { return (m_rng.generateDouble() * 2 - 1) * amp; };

    // 偶发负载尖峰：平均每台设备约 4 分钟一次，持续 6 秒，让告警自然出现
    if (nowMs > d.spikeUntil && m_rng.generateDouble() < m_intervalMs / 1000.0 * 0.004)
        d.spikeUntil = nowMs + 6000;
    const bool spiking = nowMs < d.spikeUntil;

    const double cpu = std::clamp(d.base + d.bias + 12 * std::sin(2 * M_PI * t / d.period + d.phase) + noise(3) +
                                      (spiking ? 45 : 0),
                                  0.0, 100.0);
    // 温度是 CPU 的一阶滞后，时间常数约 8 秒
    d.temp += (35 + cpu * 0.5 - d.temp) * std::min(1.0, m_intervalMs / 8000.0);
    const double in = std::clamp(d.traffic * (1 + 0.5 * std::sin(2 * M_PI * t / (d.period * 1.7) + d.phase)) +
                                     noise(20) + (spiking ? d.traffic : 0),
                                 0.0, 1000.0);
    const double out = std::clamp(in * 0.55 + noise(10), 0.0, 1000.0);

    Proto::Telemetry tm;
    tm.deviceId = d.id;
    tm.values = {float(cpu), float(d.temp + noise(0.2)), float(in), float(out)};
    return tm;
}

QByteArray DeviceSimCore::maybeCorrupt(QByteArray frame)
{
    if (m_faults.corruptPermille > 0 && int(m_rng.bounded(1000)) < m_faults.corruptPermille) {
        const int i = int(m_rng.bounded(int(frame.size())));
        frame[i] = char(frame[i] ^ (1 << m_rng.bounded(8)));
    }
    return frame;
}

template <typename F>
void DeviceSimCore::runReports(qint64 nowMs, F &&onSample)
{
    if (m_nextReport < 0)
        m_nextReport = nowMs;
    // 落后太多（比如进程被挂起过）就直接跳到现在，不补发历史数据
    if (nowMs - m_nextReport > 1000)
        m_nextReport = nowMs;
    while (nowMs >= m_nextReport) {
        for (Dev &d : m_devs) {
            const Proto::Telemetry t = sample(d, m_nextReport);
            m_latest[d.id] = t;
            onSample(t);
        }
        m_nextReport += m_intervalMs;
    }
}

QByteArray DeviceSimCore::step(qint64 nowMs)
{
    QByteArray out;
    runReports(nowMs, [&](const Proto::Telemetry &t) {
        out += maybeCorrupt(Proto::encode(Proto::CmdTelemetry, m_seq++, Proto::encodeTelemetry(t)));
    });
    if (nowMs >= m_nextHeartbeat) {
        out += maybeCorrupt(Proto::encode(Proto::CmdHeartbeat, m_seq++));
        m_nextHeartbeat = nowMs + 1000;
    }
    return out;
}

void DeviceSimCore::advance(qint64 nowMs)
{
    runReports(nowMs, [](const Proto::Telemetry &) {});
}

const Proto::Telemetry *DeviceSimCore::latest(int deviceId) const
{
    auto it = m_latest.constFind(deviceId);
    return it == m_latest.cend() ? nullptr : &*it;
}

bool DeviceSimCore::hasDevice(int deviceId) const
{
    return std::any_of(m_devs.cbegin(), m_devs.cend(), [&](const Dev &d) { return d.id == deviceId; });
}

QList<int> DeviceSimCore::deviceIds() const
{
    QList<int> ids;
    for (const Dev &d : m_devs)
        ids << d.id;
    return ids;
}

quint8 DeviceSimCore::applyParam(quint16 deviceId, quint8 param, qint32 value)
{
    if (deviceId != Proto::kBroadcast && !hasDevice(deviceId))
        return Proto::AckNoDevice;
    if (param == Proto::ParamReportInterval) {
        if (value < 20 || value > 5000)
            return Proto::AckOutOfRange;
        m_intervalMs = value;
        return Proto::AckOk;
    }
    if (param == Proto::ParamLoadBias) {
        if (value < -50 || value > 80)
            return Proto::AckOutOfRange;
        for (Dev &d : m_devs)
            if (deviceId == Proto::kBroadcast || d.id == deviceId)
                d.bias = value;
        return Proto::AckOk;
    }
    return Proto::AckBadParam;
}

qint32 DeviceSimCore::paramValue(int deviceId, quint8 param) const
{
    if (param == Proto::ParamReportInterval)
        return m_intervalMs;
    for (const Dev &d : m_devs)
        if (d.id == deviceId)
            return qint32(d.bias);
    return 0;
}

bool DeviceSimCore::rollDropAck()
{
    return m_faults.dropAckPercent > 0 && int(m_rng.bounded(100)) < m_faults.dropAckPercent;
}

QByteArray DeviceSimCore::handleInput(QByteArrayView bytes)
{
    QVector<Proto::Frame> frames;
    m_parser.feed(bytes, &frames);

    QByteArray out;
    for (const Proto::Frame &f : frames) {
        Proto::WriteParamReq req;
        if (f.cmd != Proto::CmdWriteParam || !Proto::decodeWriteParam(f.payload, &req))
            continue;
        const Proto::AckMsg ack{req.param, applyParam(req.deviceId, req.param, req.value), req.value};
        // 丢应答：参数其实已经生效，上位机超时后会用同一个 seq 重发，设备重复执行也无副作用（幂等）
        if (rollDropAck())
            continue;
        out += Proto::encode(Proto::CmdAck, f.seq, Proto::encodeAck(ack));
    }
    return out;
}

// ------------------------------------------------------------------ VirtualDevice

VirtualDevice::VirtualDevice(const QList<int> &deviceIds, Mode mode, QObject *parent)
    : QIODevice(parent), m_core(deviceIds), m_timer(new QTimer(this)), m_rng(42)
{
    if (mode == Mode::ModbusRtu)
        m_modbus = std::make_unique<ModbusSlaveSim>(m_core);
    m_timer->setInterval(10);
    connect(m_timer, &QTimer::timeout, this, &VirtualDevice::tick);
}

bool VirtualDevice::open(OpenMode mode)
{
    if (Clock::nowMs() < m_outageUntil) {
        setErrorString(QStringLiteral("设备无响应"));
        return false;
    }
    m_rx.clear();
    m_timer->start();
    return QIODevice::open(mode | QIODevice::Unbuffered);
}

void VirtualDevice::close()
{
    m_timer->stop();
    m_rx.clear();
    QIODevice::close();
}

VirtualDevice::~VirtualDevice() = default;

void VirtualDevice::setFaults(const FaultConfig &f) { m_core.setFaults(f); }

void VirtualDevice::simulateOutage(int ms) { m_outageUntil = Clock::nowMs() + ms; }

void VirtualDevice::tick()
{
    const qint64 now = Clock::nowMs();
    if (m_modbus) {
        m_core.advance(now);   // Modbus 从站不主动上报，只刷新寄存器
        return;
    }
    QByteArray bytes = m_core.step(now);
    if (now < m_outageUntil)
        return;   // 设备"死机"：数据照常产生但发不出来
    deliver(std::move(bytes));
}

void VirtualDevice::deliver(QByteArray bytes)
{
    if (bytes.isEmpty())
        return;
    if (!m_core.faults().fragment) {
        m_rx += bytes;
        emit readyRead();
        return;
    }
    // 随机切块投递：一块里可能是半帧，也可能跨越好几帧
    qsizetype pos = 0;
    while (pos < bytes.size()) {
        const qsizetype chunk = std::min<qsizetype>(bytes.size() - pos, 1 + m_rng.bounded(60));
        m_rx += bytes.mid(pos, chunk);
        pos += chunk;
        emit readyRead();
    }
}

qint64 VirtualDevice::readData(char *data, qint64 maxSize)
{
    const qint64 n = std::min<qint64>(maxSize, m_rx.size());
    std::memcpy(data, m_rx.constData(), size_t(n));
    m_rx.remove(0, n);
    return n;
}

qint64 VirtualDevice::writeData(const char *data, qint64 size)
{
    if (Clock::nowMs() < m_outageUntil)
        return size;   // 写进去了，但设备不会回应
    const QByteArray reply = m_modbus ? m_modbus->handleRtu(QByteArrayView(data, size))
                                      : m_core.handleInput(QByteArrayView(data, size));
    if (!reply.isEmpty()) {
        // 应答放到下一轮事件循环再投递，模拟线路往返延迟，也避免在 write() 里重入 readyRead
        QTimer::singleShot(5, this, [this, reply] {
            if (isOpen() && Clock::nowMs() >= m_outageUntil)
                deliver(reply);
        });
    }
    return size;
}
