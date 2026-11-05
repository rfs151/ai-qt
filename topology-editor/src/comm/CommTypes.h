#pragma once

#include "comm/Protocol.h"

#include <QList>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <array>

struct LinkConfig
{
    enum Kind { Virtual, Tcp, Serial };
    Kind kind = Virtual;
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 9000;
    QString serialPort;
    qint32 baudRate = 115200;
    QList<int> deviceIds;   // 仅虚拟设备使用：要模拟哪些设备

    // Modbus：虚拟设备/串口走 RTU，TCP 走 Modbus TCP（MBAP）
    bool modbus = false;
    QList<int> modbusSlaves;   // 外部从站的地址列表；虚拟设备时用 deviceIds
    int pollMs = 500;
};

enum class LinkState { Disconnected, Connecting, Connected, Reconnecting };

// 一条遥测采样。时间戳是上位机收到的时刻（Clock::nowMs）
struct Sample
{
    int deviceId = 0;
    qint64 tMs = 0;
    std::array<float, Proto::kChannelCount> v{};
};

struct RawLog
{
    qint64 tMs = 0;
    bool tx = false;
    bool crcOk = true;
    QByteArray bytes;
};

struct LinkStats
{
    quint64 rxBytes = 0;
    quint64 txBytes = 0;
    quint64 frames = 0;
    quint64 crcErrors = 0;
    quint64 discardedBytes = 0;
    int reconnects = 0;
    int retries = 0;
    int ackTimeouts = 0;
    double rxBytesPerSec = 0;
    double framesPerSec = 0;
    int batchesPerSec = 0;   // 每秒投递给界面线程的批次数

    // Modbus 主站
    bool modbus = false;
    double requestsPerSec = 0;
    quint64 mbTimeouts = 0;
    quint64 mbExceptions = 0;
    double avgRttMs = 0;
};

Q_DECLARE_METATYPE(LinkConfig)
Q_DECLARE_METATYPE(LinkState)
Q_DECLARE_METATYPE(LinkStats)
