#include "comm/CommWorker.h"

#include <QThread>
#include <QTimer>

#ifdef HAS_NETWORK
#include <QTcpSocket>
#endif
#ifdef HAS_SERIALPORT
#include <QSerialPort>
#endif

namespace {
constexpr int kMaxRawPerFlush = 60;   // 报文监视每批最多转发多少条，防止刷屏拖慢界面
}

CommWorker::CommWorker(QObject *parent)
    : QObject(parent),
      m_flushTimer(new QTimer(this)),
      m_watchdog(new QTimer(this)),
      m_reconnectTimer(new QTimer(this)),
      m_statsTimer(new QTimer(this)),
      m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(5);   // Modbus 一问一答，用短周期驱动状态机
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        if (m_modbus && m_state == LinkState::Connected)
            m_modbus->tick(Clock::nowMs());
    });
    m_flushTimer->setInterval(33);
    m_watchdog->setInterval(100);
    m_statsTimer->setInterval(1000);
    m_reconnectTimer->setSingleShot(true);
    connect(m_flushTimer, &QTimer::timeout, this, &CommWorker::flush);
    connect(m_watchdog, &QTimer::timeout, this, &CommWorker::onWatchdog);
    connect(m_statsTimer, &QTimer::timeout, this, &CommWorker::publishStats);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        ++m_stats.reconnects;
        openDevice();
    });
}

// 定时器归属工作线程，必须在工作线程里 stop()，所以析构时不再做清理，由 CommManager 先调用 stop()
CommWorker::~CommWorker() = default;

// ------------------------------------------------------------------ 生命周期

void CommWorker::start(const LinkConfig &cfg)
{
    stop();
    m_cfg = cfg;
    m_wanted = true;
    m_backoffMs = 500;
    m_stats = {};
    m_prevRxBytes = m_prevFrames = 0;
    m_batches = 0;
    m_parser = Proto::FrameParser{};
    m_prevRequests = 0;
    m_modbus.reset();
    if (cfg.modbus) {
        const QList<int> slaves = cfg.kind == LinkConfig::Virtual ? cfg.deviceIds : cfg.modbusSlaves;
        ModbusMaster::Hooks h;
        h.send = [this](const QByteArray &adu) { sendFrame(adu); };
        h.sample = [this](const Sample &s) { m_samples.append(s); };
        h.alive = [this](qint64 now) { m_lastRxMs = now; };
        h.paramResult = [this](int id, bool ok, const QString &text) { emit paramResult(id, ok, text); };
        h.received = [this](const QByteArray &f, bool ok) {
            if (m_rawEnabled && m_raw.size() < 60)
                m_raw.append({Clock::nowMs(), false, ok, f});
        };
        h.registers = [this](int slave, const QVector<quint16> &in, const QVector<quint16> &hold) {
            m_regsSlave = slave;
            m_regsInput = in;
            m_regsHolding = hold;
            m_regsDirty = true;   // 随下一次 flush 投递，避免每个应答一个跨线程信号
        };
        m_modbus = std::make_unique<ModbusMaster>(
            cfg.kind == LinkConfig::Tcp ? ModbusMaster::Framing::Tcp : ModbusMaster::Framing::Rtu, slaves, cfg.pollMs,
            std::move(h));
        m_modbus->setWatch(m_watchSlave);
    }

    createDevice();
    if (!m_dev) {
        m_wanted = false;
        setState(LinkState::Disconnected, QStringLiteral("当前平台不支持这种链路"));
        return;
    }
    m_flushTimer->start();
    m_watchdog->start();
    m_statsTimer->start();
    if (m_modbus)
        m_pollTimer->start();
    openDevice();
}

void CommWorker::stop()
{
    m_wanted = false;
    m_reconnectTimer->stop();
    m_watchdog->stop();
    m_flushTimer->stop();
    m_statsTimer->stop();
    m_pollTimer->stop();
    if (m_modbus)
        m_modbus->reset(QStringLiteral("链路已断开"));
    if (m_dev) {
        m_dev->disconnect(this);
        m_dev->close();
        m_dev->deleteLater();
        m_dev = nullptr;
        m_virtual = nullptr;
    }
    flush();
    failAllPending(QStringLiteral("链路已断开"));
    m_parser.reset();
    if (m_state != LinkState::Disconnected)
        setState(LinkState::Disconnected, QStringLiteral("已断开"));
}

void CommWorker::createDevice()
{
    switch (m_cfg.kind) {
    case LinkConfig::Virtual: {
        m_virtual = new VirtualDevice(m_cfg.deviceIds,
                                      m_cfg.modbus ? VirtualDevice::Mode::ModbusRtu : VirtualDevice::Mode::Custom, this);
        m_virtual->setFaults(m_faults);
        m_dev = m_virtual;
        break;
    }
    case LinkConfig::Tcp: {
#ifdef HAS_NETWORK
        auto *sock = new QTcpSocket(this);
        // 小包实时数据，关掉 Nagle 降低延迟
        sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(sock, &QTcpSocket::connected, this, &CommWorker::onOpened);
        connect(sock, &QTcpSocket::disconnected, this, [this] { connectionLost(QStringLiteral("对端关闭连接")); });
        connect(sock, &QTcpSocket::errorOccurred, this, [this, sock] { connectionLost(sock->errorString()); });
        m_dev = sock;
#endif
        break;
    }
    case LinkConfig::Serial: {
#ifdef HAS_SERIALPORT
        auto *port = new QSerialPort(this);
        connect(port, &QSerialPort::errorOccurred, this, [this, port](QSerialPort::SerialPortError e) {
            // 串口被拔掉时是 ResourceError；打开失败在 openDevice() 里处理
            if (e == QSerialPort::ResourceError || e == QSerialPort::PermissionError)
                connectionLost(port->errorString());
        });
        m_dev = port;
#endif
        break;
    }
    }
    if (m_dev)
        connect(m_dev, &QIODevice::readyRead, this, &CommWorker::onReadyRead);
}

void CommWorker::openDevice()
{
    if (!m_dev || !m_wanted)
        return;
    m_connectStartMs = Clock::nowMs();
    m_parser.reset();

    switch (m_cfg.kind) {
    case LinkConfig::Virtual:
        setState(LinkState::Connecting, QStringLiteral("打开虚拟设备"));
        if (m_virtual->open(QIODevice::ReadWrite))
            onOpened();
        else
            connectionLost(m_virtual->errorString());
        break;
    case LinkConfig::Tcp:
#ifdef HAS_NETWORK
        setState(LinkState::Connecting, QStringLiteral("连接 %1:%2").arg(m_cfg.host).arg(m_cfg.port));
        static_cast<QTcpSocket *>(m_dev)->connectToHost(m_cfg.host, m_cfg.port);   // 异步，结果走信号
#endif
        break;
    case LinkConfig::Serial:
#ifdef HAS_SERIALPORT
    {
        auto *port = static_cast<QSerialPort *>(m_dev);
        setState(LinkState::Connecting, QStringLiteral("打开 %1").arg(m_cfg.serialPort));
        port->setPortName(m_cfg.serialPort);
        port->setBaudRate(m_cfg.baudRate);
        port->setDataBits(QSerialPort::Data8);
        port->setParity(QSerialPort::NoParity);
        port->setStopBits(QSerialPort::OneStop);
        port->setFlowControl(QSerialPort::NoFlowControl);
        if (port->open(QIODevice::ReadWrite))
            onOpened();
        else
            connectionLost(port->errorString());
    }
#endif
    break;
    }
}

void CommWorker::onOpened()
{
    m_backoffMs = 500;
    m_lastRxMs = Clock::nowMs();   // 从现在开始计算心跳超时
    QString where;
    switch (m_cfg.kind) {
    case LinkConfig::Virtual: where = QStringLiteral("虚拟设备 × %1").arg(m_cfg.deviceIds.size()); break;
    case LinkConfig::Tcp: where = QStringLiteral("%1:%2").arg(m_cfg.host).arg(m_cfg.port); break;
    case LinkConfig::Serial: where = QStringLiteral("%1 @ %2").arg(m_cfg.serialPort).arg(m_cfg.baudRate); break;
    }
    setState(LinkState::Connected, where);
}

void CommWorker::connectionLost(const QString &why)
{
    // 先改状态再关设备：TCP 的 abort() 会同步发出 disconnected，靠状态判断避免重入
    if (!m_wanted || m_state == LinkState::Reconnecting || m_state == LinkState::Disconnected)
        return;
    setState(LinkState::Reconnecting, QStringLiteral("%1，%2 秒后重连").arg(why).arg(m_backoffMs / 1000.0, 0, 'f', 1));
    failAllPending(QStringLiteral("链路中断：%1").arg(why));
    if (m_modbus)
        m_modbus->reset(QStringLiteral("链路中断：%1").arg(why));
#ifdef HAS_NETWORK
    if (auto *sock = qobject_cast<QTcpSocket *>(m_dev))
        sock->abort();
    else
#endif
        m_dev->close();
    m_reconnectTimer->start(m_backoffMs);
    m_backoffMs = std::min(m_backoffMs * 2, kMaxBackoffMs);   // 指数退避，避免设备没起来时疯狂重连
}

// ------------------------------------------------------------------ 收发

void CommWorker::onReadyRead()
{
    const QByteArray bytes = m_dev->readAll();
    if (bytes.isEmpty())
        return;
    m_stats.rxBytes += quint64(bytes.size());
    if (m_modbus) {
        m_modbus->onBytes(bytes, Clock::nowMs());
        return;
    }

    QVector<Proto::Frame> frames;
    QVector<Proto::RawFrame> raw;
    m_parser.feed(bytes, &frames, m_rawEnabled ? &raw : nullptr);

    const qint64 now = Clock::nowMs();
    for (const Proto::Frame &f : std::as_const(frames))
        handleFrame(f, now);
    for (Proto::RawFrame &r : raw)
        if (m_raw.size() < kMaxRawPerFlush)
            m_raw.append({now, false, r.crcOk, std::move(r.bytes)});
}

void CommWorker::handleFrame(const Proto::Frame &f, qint64 now)
{
    m_lastRxMs = now;   // 只有校验通过的帧才算"活着"，线路上的噪声不算
    switch (f.cmd) {
    case Proto::CmdTelemetry: {
        Proto::Telemetry t;
        if (Proto::decodeTelemetry(f.payload, &t))
            m_samples.append({t.deviceId, now, t.values});
        break;
    }
    case Proto::CmdAck: {
        auto it = m_pending.find(f.seq);
        Proto::AckMsg ack;
        if (it == m_pending.end() || !Proto::decodeAck(f.payload, &ack))
            break;   // 重发后迟到的重复应答，直接丢弃
        const Pending &p = it.value();
        const QString target = p.req.deviceId == Proto::kBroadcast ? QStringLiteral("全部设备")
                                                                   : QStringLiteral("#%1").arg(p.req.deviceId);
        QString text = QStringLiteral("%1 %2 = %3：%4")
                           .arg(target, Proto::paramName(p.req.param))
                           .arg(p.req.value)
                           .arg(Proto::ackResultName(ack.result));
        if (p.tries > 1)
            text += QStringLiteral("（重发 %1 次）").arg(p.tries - 1);
        emit paramResult(p.requestId, ack.result == Proto::AckOk, text);
        m_pending.erase(it);
        break;
    }
    default:
        break;   // 心跳只用来刷新 m_lastRxMs
    }
}

void CommWorker::sendFrame(const QByteArray &frame)
{
    if (!m_dev || !m_dev->isOpen())
        return;
    m_dev->write(frame);
    m_stats.txBytes += quint64(frame.size());
    if (m_rawEnabled)
        m_raw.append({Clock::nowMs(), true, true, frame});
}

void CommWorker::writeParam(int requestId, int deviceId, int param, int value)
{
    if (m_state != LinkState::Connected) {
        emit paramResult(requestId, false, QStringLiteral("链路未连接，无法下发"));
        return;
    }
    if (m_modbus) {
        m_modbus->writeParam(requestId, deviceId, param, value);   // 结果经 Hooks::paramResult 回来
        return;
    }
    do {
        ++m_seq;
    } while (m_pending.contains(m_seq));

    Pending p;
    p.requestId = requestId;
    p.req = {quint16(deviceId), quint8(param), qint32(value)};
    p.frame = Proto::encode(Proto::CmdWriteParam, m_seq, Proto::encodeWriteParam(p.req));
    p.tries = 1;
    p.deadline = Clock::nowMs() + kAckTimeoutMs;
    sendFrame(p.frame);
    m_pending.insert(m_seq, p);
}

void CommWorker::failAllPending(const QString &why)
{
    for (const Pending &p : std::as_const(m_pending))
        emit paramResult(p.requestId, false, why);
    m_pending.clear();
}

// ------------------------------------------------------------------ 定时任务

void CommWorker::onWatchdog()
{
    const qint64 now = Clock::nowMs();
    if (m_state == LinkState::Connected && now - m_lastRxMs > kHeartbeatTimeoutMs) {
        connectionLost(QStringLiteral("心跳超时（%1 秒未收到有效帧）").arg(kHeartbeatTimeoutMs / 1000));
        return;
    }
    if (m_state == LinkState::Connecting && now - m_connectStartMs > kConnectTimeoutMs) {
        connectionLost(QStringLiteral("连接超时"));
        return;
    }

    // 写参数超时重发：沿用原 seq，设备端按 seq 去重/幂等执行
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        Pending &p = it.value();
        if (now < p.deadline) {
            ++it;
            continue;
        }
        if (p.tries < kMaxTries) {
            ++p.tries;
            ++m_stats.retries;
            p.deadline = now + kAckTimeoutMs;
            sendFrame(p.frame);
            ++it;
        } else {
            ++m_stats.ackTimeouts;
            emit paramResult(p.requestId, false,
                             QStringLiteral("#%1 %2：应答超时（已重发 %3 次）")
                                 .arg(p.req.deviceId)
                                 .arg(Proto::paramName(p.req.param))
                                 .arg(kMaxTries - 1));
            it = m_pending.erase(it);
        }
    }
}

void CommWorker::flush()
{
    if (!m_samples.isEmpty()) {
        emit samplesReady(m_samples);
        m_samples.clear();
        ++m_batches;
    }
    if (!m_raw.isEmpty()) {
        emit rawFrames(m_raw);
        m_raw.clear();
    }
    if (m_regsDirty) {
        emit registersUpdated(m_regsSlave, m_regsInput, m_regsHolding);
        m_regsDirty = false;
    }
}

void CommWorker::publishStats()
{
    if (m_modbus) {
        const auto &ms = m_modbus->stats();
        m_stats.modbus = true;
        m_stats.frames = ms.responses;
        m_stats.crcErrors = ms.crcErrors;
        m_stats.discardedBytes = ms.discardedBytes;
        m_stats.retries = int(ms.retries);
        m_stats.mbTimeouts = ms.timeouts;
        m_stats.mbExceptions = ms.exceptions;
        m_stats.avgRttMs = ms.responses ? ms.rttSumMs / double(ms.responses) : 0;
        m_stats.requestsPerSec = double(ms.requests - m_prevRequests);
        m_prevRequests = ms.requests;
    } else {
        const auto &ps = m_parser.stats();
        m_stats.frames = ps.frames;
        m_stats.crcErrors = ps.crcErrors;
        m_stats.discardedBytes = ps.discardedBytes;
    }
    m_stats.rxBytesPerSec = double(m_stats.rxBytes - m_prevRxBytes);
    m_stats.framesPerSec = double(m_stats.frames - m_prevFrames);
    m_stats.batchesPerSec = m_batches;
    m_prevRxBytes = m_stats.rxBytes;
    m_prevFrames = m_stats.frames;
    m_batches = 0;
    emit statsUpdated(m_stats);
}

void CommWorker::setState(LinkState s, const QString &detail)
{
    m_state = s;
    emit stateChanged(s, detail);
}

void CommWorker::setFaults(const FaultConfig &f)
{
    m_faults = f;
    if (m_virtual)
        m_virtual->setFaults(f);
}

void CommWorker::setRegisterWatch(int slave)
{
    m_watchSlave = slave;
    if (m_modbus)
        m_modbus->setWatch(slave);
}

void CommWorker::simulateOutage(int ms)
{
    if (m_virtual)
        m_virtual->simulateOutage(ms);
}
