#pragma once

#include "comm/CommTypes.h"
#include "comm/DeviceSim.h"

#include <QObject>

class CommWorker;
class QThread;

// 界面线程这一侧的门面：创建工作线程，把调用以 QueuedConnection 转到工作线程，
// 把工作线程的信号转回来（跨线程的信号-信号连接自动排队）。
// 平台不支持线程（WASM 单线程版）时，工作对象直接留在主线程，接口和行为不变。
class CommManager : public QObject
{
    Q_OBJECT
public:
    explicit CommManager(QObject *parent = nullptr);
    ~CommManager() override;

    bool isThreaded() const { return m_thread != nullptr; }
    QString threadDescription() const;
    LinkState state() const { return m_state; }
    LinkConfig::Kind kind() const { return m_kind; }

    void open(const LinkConfig &cfg);
    void close();
    int writeParam(int deviceId, int param, int value);   // 返回请求号，结果见 paramResult
    void setFaults(const FaultConfig &f);
    void simulateOutage(int ms);
    void setRawCapture(bool on);
    void setRegisterWatch(int slave);
    bool isModbus() const { return m_modbus; }

signals:
    void stateChanged(LinkState state, const QString &detail);
    void samplesReady(const QVector<Sample> &batch);
    void rawFrames(const QVector<RawLog> &batch);
    void statsUpdated(const LinkStats &stats);
    void paramResult(int requestId, bool ok, const QString &text);
    void registersUpdated(int slave, const QVector<quint16> &input, const QVector<quint16> &holding);

private:
    template <typename F>
    void post(F &&fn);

    CommWorker *m_worker;
    QThread *m_thread = nullptr;
    LinkState m_state = LinkState::Disconnected;
    LinkConfig::Kind m_kind = LinkConfig::Virtual;
    bool m_modbus = false;
    int m_nextRequest = 1;
};
