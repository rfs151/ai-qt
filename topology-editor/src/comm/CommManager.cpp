#include "comm/CommManager.h"
#include "comm/CommWorker.h"

#include <QThread>

CommManager::CommManager(QObject *parent) : QObject(parent), m_worker(new CommWorker)
{
#if QT_CONFIG(thread)
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("comm-worker"));
    m_worker->moveToThread(m_thread);
    m_thread->start();
#else
    m_worker->setParent(this);
#endif

    // 工作线程 -> 界面线程：信号转信号，跨线程时自动走队列，参数被拷贝一份
    connect(m_worker, &CommWorker::stateChanged, this, [this](LinkState s, const QString &detail) {
        m_state = s;
        emit stateChanged(s, detail);
    });
    connect(m_worker, &CommWorker::samplesReady, this, &CommManager::samplesReady);
    connect(m_worker, &CommWorker::rawFrames, this, &CommManager::rawFrames);
    connect(m_worker, &CommWorker::statsUpdated, this, &CommManager::statsUpdated);
    connect(m_worker, &CommWorker::paramResult, this, &CommManager::paramResult);
    connect(m_worker, &CommWorker::registersUpdated, this, &CommManager::registersUpdated);
}

CommManager::~CommManager()
{
#if QT_CONFIG(thread)
    // 定时器和设备都属于工作线程，必须在工作线程里关闭；阻塞等它做完再退出线程
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->stop(); }, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
#else
    m_worker->stop();
#endif
}

QString CommManager::threadDescription() const
{
    return isThreaded() ? QStringLiteral("独立线程 %1").arg(m_thread->objectName())
                        : QStringLiteral("主线程（WASM 单线程版）");
}

template <typename F>
void CommManager::post(F &&fn)
{
    // 单线程时同样用队列：调用方的行为（异步返回）与多线程版一致
    QMetaObject::invokeMethod(m_worker, std::forward<F>(fn), Qt::QueuedConnection);
}

void CommManager::open(const LinkConfig &cfg)
{
    m_kind = cfg.kind;
    m_modbus = cfg.modbus;
    post([w = m_worker, cfg] { w->start(cfg); });
}

void CommManager::close()
{
    post([w = m_worker] { w->stop(); });
}

int CommManager::writeParam(int deviceId, int param, int value)
{
    const int id = m_nextRequest++;
    post([w = m_worker, id, deviceId, param, value] { w->writeParam(id, deviceId, param, value); });
    return id;
}

void CommManager::setFaults(const FaultConfig &f)
{
    post([w = m_worker, f] { w->setFaults(f); });
}

void CommManager::simulateOutage(int ms)
{
    post([w = m_worker, ms] { w->simulateOutage(ms); });
}

void CommManager::setRawCapture(bool on)
{
    post([w = m_worker, on] { w->setRawCapture(on); });
}

void CommManager::setRegisterWatch(int slave)
{
    post([w = m_worker, slave] { w->setRegisterWatch(slave); });
}
