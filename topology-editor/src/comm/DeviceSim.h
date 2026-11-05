#pragma once

#include "comm/Protocol.h"

#include <QIODevice>
#include <QHash>
#include <QList>
#include <memory>
#include <QRandomGenerator>

class QTimer;
class ModbusSlaveSim;

namespace Clock {
// 进程内单调时钟（毫秒），各线程共用，曲线的时间轴以它为准
qint64 nowMs();
} // namespace Clock

// 链路故障注入，用来演示上位机的容错处理
struct FaultConfig
{
    bool fragment = false;     // 随机分包/粘包投递
    int corruptPermille = 0;   // 每帧误码概率（‰），制造 CRC 错误
    int dropAckPercent = 0;    // 丢弃写参数应答的概率（%），触发上位机重发
};

// 设备端协议逻辑（不含 IO）：按上报周期生成遥测、处理写参数。
// 应用内的 VirtualDevice 和独立的 devsim 模拟器共用这一份。
class DeviceSimCore
{
public:
    explicit DeviceSimCore(const QList<int> &deviceIds, quint32 seed = 7);

    // 推进到 nowMs，返回这段时间内应发出的字节
    QByteArray step(qint64 nowMs);
    // 处理上位机发来的字节，返回应答
    QByteArray handleInput(QByteArrayView bytes);

    // 只推进仿真不产生帧（Modbus 从站用：数据放在寄存器里，等主站来读）
    void advance(qint64 nowMs);
    const Proto::Telemetry *latest(int deviceId) const;
    bool hasDevice(int deviceId) const;
    QList<int> deviceIds() const;
    // 写参数，deviceId 为 kBroadcast 时作用于全部设备；返回 Proto::AckResult
    quint8 applyParam(quint16 deviceId, quint8 param, qint32 value);
    qint32 paramValue(int deviceId, quint8 param) const;
    // 故障注入的随机判定，Modbus 从站也用
    bool rollDropAck();
    QByteArray maybeCorrupt(QByteArray frame);

    void setFaults(const FaultConfig &f) { m_faults = f; }
    const FaultConfig &faults() const { return m_faults; }
    int reportIntervalMs() const { return m_intervalMs; }
    int deviceCount() const { return int(m_devs.size()); }

private:
    struct Dev
    {
        quint16 id;
        double base;
        double phase;
        double period;
        double traffic;
        double temp;
        double bias = 0;
        qint64 spikeUntil = 0;
    };

    Proto::Telemetry sample(Dev &d, qint64 nowMs);
    template <typename F>
    void runReports(qint64 nowMs, F &&onSample);

    QVector<Dev> m_devs;
    QHash<int, Proto::Telemetry> m_latest;
    QRandomGenerator m_rng;
    Proto::FrameParser m_parser;
    FaultConfig m_faults;
    int m_intervalMs = 100;
    qint64 m_nextReport = -1;
    qint64 m_nextHeartbeat = 0;
    quint8 m_seq = 0;
};

// 应用内的虚拟设备：以 QIODevice 的形式出现，上位机侧代码和串口/TCP 完全一样。
// 浏览器里没有串口和 TCP，网页版就靠它演示整条通信链路。
class VirtualDevice : public QIODevice
{
    Q_OBJECT
public:
    enum class Mode { Custom, ModbusRtu };
    explicit VirtualDevice(const QList<int> &deviceIds, Mode mode = Mode::Custom, QObject *parent = nullptr);
    ~VirtualDevice() override;

    bool open(OpenMode mode) override;
    void close() override;
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_rx.size() + QIODevice::bytesAvailable(); }

    void setFaults(const FaultConfig &f);
    void simulateOutage(int ms);   // 设备在 ms 毫秒内无响应，期间 open() 也会失败

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    void tick();
    void deliver(QByteArray bytes);

    DeviceSimCore m_core;
    std::unique_ptr<ModbusSlaveSim> m_modbus;   // Modbus 模式下由它应答主站请求
    QTimer *m_timer;
    QRandomGenerator m_rng;
    QByteArray m_rx;   // 上位机可读的字节
    qint64 m_outageUntil = 0;
};
