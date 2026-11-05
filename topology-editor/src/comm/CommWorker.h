#pragma once

#include "comm/CommTypes.h"
#include "comm/DeviceSim.h"
#include "comm/Modbus.h"

#include <QHash>
#include <QObject>
#include <memory>

class QIODevice;
class QTimer;

// 通信工作对象，运行在独立线程（WASM 单线程版运行在主线程）。
// 负责：打开链路、收字节解帧、心跳超时检测与指数退避重连、写参数的超时重发，
// 并把采样攒成批（约 30Hz）投递给界面线程，避免每帧一个跨线程信号。
class CommWorker : public QObject
{
    Q_OBJECT
public:
    explicit CommWorker(QObject *parent = nullptr);
    ~CommWorker() override;

    static constexpr int kHeartbeatTimeoutMs = 3000;
    static constexpr int kConnectTimeoutMs = 3000;
    static constexpr int kAckTimeoutMs = 300;
    static constexpr int kMaxTries = 3;
    static constexpr int kMaxBackoffMs = 8000;

    // 以下函数都只能在工作线程里调用（CommManager 通过 QueuedConnection 转发）
    void start(const LinkConfig &cfg);
    void stop();
    void writeParam(int requestId, int deviceId, int param, int value);
    void setFaults(const FaultConfig &f);
    void simulateOutage(int ms);
    void setRawCapture(bool on) { m_rawEnabled = on; }
    void setRegisterWatch(int slave);

signals:
    void stateChanged(LinkState state, const QString &detail);
    void samplesReady(const QVector<Sample> &batch);
    void rawFrames(const QVector<RawLog> &batch);
    void statsUpdated(const LinkStats &stats);
    void paramResult(int requestId, bool ok, const QString &text);
    void registersUpdated(int slave, const QVector<quint16> &input, const QVector<quint16> &holding);

private:
    struct Pending
    {
        int requestId;
        Proto::WriteParamReq req;
        QByteArray frame;
        int tries;
        qint64 deadline;
    };

    void createDevice();
    void openDevice();
    void onOpened();
    void onReadyRead();
    void connectionLost(const QString &why);
    void handleFrame(const Proto::Frame &f, qint64 now);
    void sendFrame(const QByteArray &frame);
    void failAllPending(const QString &why);
    void setState(LinkState s, const QString &detail);
    void onWatchdog();
    void flush();
    void publishStats();

    LinkConfig m_cfg;
    QIODevice *m_dev = nullptr;
    VirtualDevice *m_virtual = nullptr;
    FaultConfig m_faults;

    Proto::FrameParser m_parser;
    LinkState m_state = LinkState::Disconnected;
    bool m_wanted = false;
    int m_backoffMs = 500;
    qint64 m_lastRxMs = 0;
    qint64 m_connectStartMs = 0;

    QTimer *m_flushTimer;
    QTimer *m_watchdog;
    QTimer *m_reconnectTimer;
    QTimer *m_statsTimer;

    QVector<Sample> m_samples;
    QVector<RawLog> m_raw;
    bool m_rawEnabled = false;

    QHash<quint8, Pending> m_pending;   // key: seq
    quint8 m_seq = 0;

    // Modbus 主站（仅 Modbus 模式）
    std::unique_ptr<ModbusMaster> m_modbus;
    QTimer *m_pollTimer;
    int m_watchSlave = 0;
    bool m_regsDirty = false;
    int m_regsSlave = 0;
    QVector<quint16> m_regsInput, m_regsHolding;
    quint64 m_prevRequests = 0;

    LinkStats m_stats;
    quint64 m_prevRxBytes = 0;
    quint64 m_prevFrames = 0;
    int m_batches = 0;
};
