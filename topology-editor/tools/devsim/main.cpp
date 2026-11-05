// devsim：独立的设备模拟器，和应用内的虚拟设备用同一套协议代码（DeviceSimCore）。
//
//   devsim --tcp 9000                      作为 TCP 服务端，上位机选"TCP 客户端"连 127.0.0.1:9000
//   devsim --serial /dev/pts/3             从串口输出。没有真串口时可以用 socat 造一对虚拟串口：
//                                          socat -d -d pty,raw,echo=0 pty,raw,echo=0
//   devsim --modbus --tcp 1502             改为 Modbus TCP 从站（每台设备一个单元号）
//   devsim --modbus --serial /dev/pts/3    改为 Modbus RTU 从站
//   其他参数：--devices 39 --corrupt 5 --drop-ack 30
#include "comm/DeviceSim.h"
#include "comm/Modbus.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#ifdef HAS_NETWORK
#include <QTcpServer>
#include <QTcpSocket>
#endif
#ifdef HAS_SERIALPORT
#include <QSerialPort>
#endif

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("devsim"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("拓扑监控演示用的设备模拟器"));
    cli.addHelpOption();
    const QCommandLineOption tcpOpt(QStringLiteral("tcp"), QStringLiteral("TCP 监听端口"), QStringLiteral("port"),
                                    QStringLiteral("9000"));
    const QCommandLineOption serialOpt(QStringLiteral("serial"), QStringLiteral("改用串口输出"), QStringLiteral("name"));
    const QCommandLineOption baudOpt(QStringLiteral("baud"), QStringLiteral("波特率"), QStringLiteral("rate"),
                                     QStringLiteral("115200"));
    const QCommandLineOption devOpt(QStringLiteral("devices"), QStringLiteral("设备数量，ID 从 1 开始"),
                                    QStringLiteral("n"), QStringLiteral("39"));
    const QCommandLineOption corruptOpt(QStringLiteral("corrupt"), QStringLiteral("误码率（‰）"), QStringLiteral("permille"),
                                        QStringLiteral("0"));
    const QCommandLineOption dropOpt(QStringLiteral("drop-ack"), QStringLiteral("丢应答概率（%）"), QStringLiteral("percent"),
                                     QStringLiteral("0"));
    const QCommandLineOption modbusOpt(QStringLiteral("modbus"), QStringLiteral("作为 Modbus 从站（TCP 或 RTU）"));
    cli.addOptions({tcpOpt, serialOpt, baudOpt, devOpt, corruptOpt, dropOpt, modbusOpt});
    cli.process(app);

    QList<int> ids;
    for (int i = 1; i <= cli.value(devOpt).toInt(); ++i)
        ids << i;
    DeviceSimCore core(ids, 2024);
    FaultConfig faults;
    faults.corruptPermille = cli.value(corruptOpt).toInt();
    faults.dropAckPercent = cli.value(dropOpt).toInt();
    core.setFaults(faults);
    const bool modbus = cli.isSet(modbusOpt);
    // 每个连接一个从站解析器（各自的接收缓冲），共享同一份设备数据
    QHash<QIODevice *, std::shared_ptr<ModbusSlaveSim>> slaves;

    // 所有已连接的端点；新数据广播给每一个
    QList<QIODevice *> peers;
    quint64 sentBytes = 0;

    auto onInput = [&core, &slaves, modbus](QIODevice *dev) {
        QByteArray reply;
        if (!modbus) {
            reply = core.handleInput(dev->readAll());
        } else {
            auto &slave = slaves[dev];
            if (!slave)
                slave = std::make_shared<ModbusSlaveSim>(core);
            const bool tcp = dev->inherits("QTcpSocket");
            reply = tcp ? slave->handleTcp(dev->readAll()) : slave->handleRtu(dev->readAll());
        }
        if (!reply.isEmpty())
            dev->write(reply);
    };

    if (cli.isSet(serialOpt)) {
#ifdef HAS_SERIALPORT
        auto *port = new QSerialPort(cli.value(serialOpt), &app);
        port->setBaudRate(cli.value(baudOpt).toInt());
        if (!port->open(QIODevice::ReadWrite)) {
            std::fprintf(stderr, "无法打开串口 %s：%s\n", qPrintable(port->portName()), qPrintable(port->errorString()));
            return 1;
        }
        QObject::connect(port, &QSerialPort::readyRead, port, [&, port] { onInput(port); });
        peers << port;
        std::printf("串口 %s @ %d，模拟 %lld 台设备\n", qPrintable(port->portName()), int(port->baudRate()),
                    qlonglong(ids.size()));
#else
        std::fprintf(stderr, "编译时没有 QtSerialPort 模块\n");
        return 1;
#endif
    } else {
#ifdef HAS_NETWORK
        auto *server = new QTcpServer(&app);
        if (!server->listen(QHostAddress::Any, quint16(cli.value(tcpOpt).toUInt()))) {
            std::fprintf(stderr, "监听失败：%s\n", qPrintable(server->errorString()));
            return 1;
        }
        QObject::connect(server, &QTcpServer::newConnection, server, [&, server] {
            while (QTcpSocket *s = server->nextPendingConnection()) {
                std::printf("客户端接入 %s:%d\n", qPrintable(s->peerAddress().toString()), s->peerPort());
                std::fflush(stdout);
                peers << s;
                QObject::connect(s, &QTcpSocket::readyRead, s, [&, s] { onInput(s); });
                QObject::connect(s, &QTcpSocket::disconnected, s, [&, s] {
                    std::printf("客户端断开\n");
                    std::fflush(stdout);
                    peers.removeOne(s);
                    slaves.remove(s);
                    s->deleteLater();
                });
            }
        });
        std::printf("TCP 监听 %d，模拟 %lld 台设备%s\n", server->serverPort(), qlonglong(ids.size()),
                    modbus ? "（Modbus TCP 从站）" : "");
#else
        std::fprintf(stderr, "编译时没有 QtNetwork 模块\n");
        return 1;
#endif
    }
    std::fflush(stdout);

    QTimer tick;
    QObject::connect(&tick, &QTimer::timeout, &app, [&] {
        if (modbus) {
            core.advance(Clock::nowMs());   // Modbus 从站只刷新寄存器，等主站来读
            return;
        }
        const QByteArray bytes = core.step(Clock::nowMs());
        if (bytes.isEmpty() || peers.isEmpty())
            return;
        for (QIODevice *p : std::as_const(peers))
            p->write(bytes);
        sentBytes += quint64(bytes.size()) * quint64(peers.size());
    });
    tick.start(10);

    QTimer report;
    QObject::connect(&report, &QTimer::timeout, &app, [&] {
        std::printf("连接 %lld · 已发送 %.1f KB · 上报周期 %d ms\n", qlonglong(peers.size()), sentBytes / 1024.0,
                    core.reportIntervalMs());
        std::fflush(stdout);
    });
    report.start(5000);

    return app.exec();
}
