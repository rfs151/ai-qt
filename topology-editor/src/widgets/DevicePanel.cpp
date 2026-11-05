#include "widgets/DevicePanel.h"
#include "comm/CommManager.h"
#include "comm/Modbus.h"
#include "comm/Protocol.h"
#include "monitor/DeviceMonitor.h"
#include "widgets/RealtimeChart.h"
#include "widgets/GaugeWidget.h"
#include "TopologyTypes.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QTimer>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef HAS_SERIALPORT
#include <QSerialPortInfo>
#endif

namespace {
constexpr int kStressSeriesId = -1;

QString clockText(qint64 monoMs)
{
    const qint64 wall = QDateTime::currentMSecsSinceEpoch() - (Clock::nowMs() - monoMs);
    return QDateTime::fromMSecsSinceEpoch(wall).toString(QStringLiteral("HH:mm:ss.zzz"));
}
} // namespace

DevicePanel::DevicePanel(CommManager *comm, DeviceMonitor *monitor, HistoryManager *history, QWidget *parent)
    : QWidget(parent), m_comm(comm), m_monitor(monitor), m_history(history), m_histStore(1)
{
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildChartTab(), QStringLiteral("实时曲线"));
    m_gaugeTab = buildGaugeTab();
    m_tabs->addTab(m_gaugeTab, QStringLiteral("仪表盘"));
    m_rawTab = buildRawTab();
    m_tabs->addTab(m_rawTab, QStringLiteral("报文监视"));
    m_tabs->addTab(buildParamTab(), QStringLiteral("参数下发"));
    m_tabs->addTab(buildRegisterTab(), QStringLiteral("寄存器"));
    m_tabs->addTab(buildHistoryTab(), QStringLiteral("历史查询"));
    connect(m_tabs, &QTabWidget::currentChanged, this, &DevicePanel::updateRawCapture);

    m_statsLabel = new QLabel;
    m_statsLabel->setTextFormat(Qt::RichText);
    m_statsLabel->setToolTip(QStringLiteral("由通信线程每秒汇总一次"));

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 2);
    v->setSpacing(4);
    v->addWidget(buildLinkBar());
    // 选项行：故障注入（虚拟设备）和 Modbus 参数并排
    m_faultBar = buildFaultBar();
    m_modbusBar = buildModbusBar();
    auto *opts = new QHBoxLayout;
    opts->setContentsMargins(0, 0, 0, 0);
    opts->addWidget(m_faultBar);
    opts->addWidget(m_modbusBar);
    opts->addStretch();
    v->addLayout(opts);
    v->addWidget(m_tabs, 1);
    v->addWidget(m_statsLabel);

    connect(m_comm, &CommManager::stateChanged, this, &DevicePanel::onStateChanged);
    connect(m_comm, &CommManager::statsUpdated, this, &DevicePanel::onStats);
    connect(m_comm, &CommManager::rawFrames, this, &DevicePanel::onRawFrames);
    connect(m_comm, &CommManager::paramResult, this, &DevicePanel::onParamResult);
    connect(m_comm, &CommManager::registersUpdated, this, &DevicePanel::onRegisters);
    connect(m_history, &HistoryManager::queryFinished, this, &DevicePanel::onHistoryResult);
    connect(m_history, &HistoryManager::statsUpdated, this, &DevicePanel::onHistoryStats);
    connect(m_history, &HistoryManager::errorOccurred, this,
            [this](const QString &e) { m_histInfo->setText(QStringLiteral("<span style='color:#d93025'>%1</span>").arg(e.toHtmlEscaped())); });

    updateOptionRows();
    onStateChanged(LinkState::Disconnected, QString());
    onStats({});
}

// ------------------------------------------------------------------ 界面搭建

QWidget *DevicePanel::buildLinkBar()
{
    m_kind = new QComboBox;
    m_kind->addItems({QStringLiteral("虚拟设备（应用内）"), QStringLiteral("TCP 客户端"), QStringLiteral("串口")});
    auto *model = qobject_cast<QStandardItemModel *>(m_kind->model());
#ifndef HAS_NETWORK
    model->item(LinkConfig::Tcp)->setEnabled(false);
    model->item(LinkConfig::Tcp)->setToolTip(QStringLiteral("浏览器里没有原生 TCP，请用桌面版"));
#endif
#ifndef HAS_SERIALPORT
    model->item(LinkConfig::Serial)->setEnabled(false);
    model->item(LinkConfig::Serial)->setToolTip(QStringLiteral("浏览器里没有串口，请用桌面版"));
#endif
    Q_UNUSED(model);

    // 每种链路一页参数
    m_kindPages = new QStackedWidget;
    auto *virtPage = new QLabel(QStringLiteral("模拟拓扑中的每台设备，可注入故障 →"));
    virtPage->setEnabled(false);

    auto *tcpPage = new QWidget;
    m_host = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_host->setMaximumWidth(130);
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(9000);
    auto *th = new QHBoxLayout(tcpPage);
    th->setContentsMargins(0, 0, 0, 0);
    th->addWidget(m_host);
    th->addWidget(new QLabel(QStringLiteral(":")));
    th->addWidget(m_port);
    th->addStretch();

    auto *serialPage = new QWidget;
    m_serialPort = new QComboBox;
    m_serialPort->setEditable(true);   // 允许手填 socat 造出来的 /dev/pts/N
    m_serialPort->setMinimumWidth(120);
    auto *refresh = new QToolButton;
    refresh->setText(QStringLiteral("刷新"));
    connect(refresh, &QToolButton::clicked, this, &DevicePanel::refreshSerialPorts);
    m_baud = new QComboBox;
    for (int b : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
        m_baud->addItem(QString::number(b), b);
    m_baud->setCurrentText(QStringLiteral("115200"));
    auto *sh = new QHBoxLayout(serialPage);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->addWidget(m_serialPort);
    sh->addWidget(refresh);
    sh->addWidget(m_baud);
    sh->addStretch();

    m_kindPages->addWidget(virtPage);
    m_kindPages->addWidget(tcpPage);
    m_kindPages->addWidget(serialPage);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_kindPages->setCurrentIndex(i);
        updateOptionRows();
        if (i == LinkConfig::Serial)
            refreshSerialPorts();
    });

    m_protocol = new QComboBox;
    m_protocol->addItems({QStringLiteral("自定义协议"), QStringLiteral("Modbus")});
    m_protocol->setToolTip(QStringLiteral("Modbus：虚拟设备和串口走 RTU，TCP 走 Modbus TCP"));
    connect(m_protocol, &QComboBox::currentIndexChanged, this, &DevicePanel::updateOptionRows);

    m_connectBtn = new QPushButton;
    m_connectBtn->setMinimumWidth(64);
    connect(m_connectBtn, &QPushButton::clicked, this, &DevicePanel::toggleConnection);
    m_stateLabel = new QLabel;
    m_stateLabel->setTextFormat(Qt::RichText);

    auto *bar = new QWidget;
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(m_kind);
    h->addWidget(m_protocol);
    h->addWidget(m_kindPages, 1);
    h->addWidget(m_connectBtn);
    h->addWidget(m_stateLabel, 2);
    return bar;
}

QWidget *DevicePanel::buildFaultBar()
{
    m_fragment = new QCheckBox(QStringLiteral("分包/粘包"));
    m_fragment->setToolTip(QStringLiteral("把字节流随机切成 1~60 字节的小块投递，一块里可能是半帧或跨多帧"));
    m_corrupt = new QSpinBox;
    m_corrupt->setRange(0, 200);
    m_corrupt->setSuffix(QStringLiteral(" ‰"));
    m_corrupt->setToolTip(QStringLiteral("每帧随机翻转一位的概率，制造 CRC 错误"));
    m_dropAck = new QSpinBox;
    m_dropAck->setRange(0, 100);
    m_dropAck->setSuffix(QStringLiteral(" %"));
    m_dropAck->setToolTip(QStringLiteral("设备执行了写参数但不回应答，上位机会超时重发"));
    auto *outage = new QPushButton(QStringLiteral("模拟断线 5 秒"));
    outage->setToolTip(QStringLiteral("设备停止响应：3 秒心跳超时 → 指数退避重连 → 恢复"));
    connect(outage, &QPushButton::clicked, this, [this] { m_comm->simulateOutage(5000); });

    connect(m_fragment, &QCheckBox::toggled, this, &DevicePanel::applyFaults);
    connect(m_corrupt, &QSpinBox::valueChanged, this, &DevicePanel::applyFaults);
    connect(m_dropAck, &QSpinBox::valueChanged, this, &DevicePanel::applyFaults);

    auto *bar = new QWidget;
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("故障注入："));
    m_corrupt->setToolTip(QStringLiteral("每帧随机翻转一位的概率，制造 CRC 错误（Modbus TCP 没有 CRC，不受影响）"));
    h->addWidget(title);
    h->addWidget(m_fragment);
    h->addWidget(new QLabel(QStringLiteral("误码")));
    h->addWidget(m_corrupt);
    h->addWidget(new QLabel(QStringLiteral("丢应答")));
    h->addWidget(m_dropAck);
    h->addWidget(outage);
    return bar;
}

QWidget *DevicePanel::buildModbusBar()
{
    m_slavesLabel = new QLabel(QStringLiteral("从站"));
    m_slaves = new QLineEdit(QStringLiteral("1-39"));
    m_slaves->setMaximumWidth(90);
    m_slaves->setToolTip(QStringLiteral("外部从站地址，如 1-39 或 1,3,5-8（虚拟设备时按拓扑自动生成）"));
    m_pollMs = new QSpinBox;
    m_pollMs->setRange(50, 10000);
    m_pollMs->setSingleStep(100);
    m_pollMs->setValue(500);
    m_pollMs->setSuffix(QStringLiteral(" ms"));
    m_pollMs->setToolTip(QStringLiteral("每轮依次读取所有从站的输入寄存器"));

    auto *bar = new QWidget;
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 0, 0);
    h->addWidget(new QLabel(QStringLiteral("Modbus：")));
    h->addWidget(m_slavesLabel);
    h->addWidget(m_slaves);
    h->addWidget(new QLabel(QStringLiteral("轮询周期")));
    h->addWidget(m_pollMs);
    return bar;
}

void DevicePanel::updateOptionRows()
{
    const bool virt = m_kind->currentIndex() == LinkConfig::Virtual;
    const bool modbus = m_protocol->currentIndex() == 1;
    m_faultBar->setVisible(virt);
    m_modbusBar->setVisible(modbus);
    m_slavesLabel->setVisible(!virt);
    m_slaves->setVisible(!virt);
}

QWidget *DevicePanel::buildChartTab()
{
    const auto &th = m_monitor->thresholds();
    m_chart = new RealtimeChart;
    m_chart->setStore(m_monitor->store());
    RealtimeChart::Lane load;
    load.unit = QStringLiteral("% / ℃");
    load.channels = {{0, QStringLiteral("CPU %"), QColor(0x3b, 0x6f, 0xe0)},
                     {1, QStringLiteral("温度 ℃"), QColor(0xe0, 0x6c, 0x10)}};
    load.yMin = 0;
    load.yMax = 100;
    load.markers = {th.cpuWarn, th.cpuAlarm};
    RealtimeChart::Lane traffic;
    traffic.unit = QStringLiteral("Mbps");
    traffic.channels = {{2, QStringLiteral("入流量"), QColor(0x1e, 0x9e, 0x6a)},
                        {3, QStringLiteral("出流量"), QColor(0x9b, 0x59, 0xb6)}};
    traffic.yMin = 0;
    traffic.autoScale = true;
    m_chart->setLanes({load, traffic});

    m_span = new QComboBox;
    m_span->addItem(QStringLiteral("10 秒"), 10);
    m_span->addItem(QStringLiteral("1 分钟"), 60);
    m_span->addItem(QStringLiteral("5 分钟"), 300);
    m_span->addItem(QStringLiteral("全部"), 0);
    m_span->setCurrentIndex(1);
    connect(m_span, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_chart->setSpanSeconds(m_span->itemData(i).toDouble()); });

    m_pauseBtn = new QPushButton(QStringLiteral("暂停"));
    m_pauseBtn->setCheckable(true);
    connect(m_pauseBtn, &QPushButton::toggled, m_chart, &RealtimeChart::setPaused);
    connect(m_chart, &RealtimeChart::pausedChanged, m_pauseBtn, [this](bool p) {
        const QSignalBlocker block(m_pauseBtn);
        m_pauseBtn->setChecked(p);
    });

    auto *stress = new QPushButton(QStringLiteral("曲线压测：20 万点"));
    stress->setToolTip(QStringLiteral("生成 1kHz × 200 秒的合成数据，观察降采样后的点数和绘制耗时"));
    connect(stress, &QPushButton::clicked, this, &DevicePanel::runChartStress);

    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(QStringLiteral("时间窗")));
    bar->addWidget(m_span);
    bar->addWidget(m_pauseBtn);
    bar->addStretch();
    bar->addWidget(stress);

    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 0);
    v->setSpacing(2);
    v->addLayout(bar);
    v->addWidget(m_chart, 1);
    return w;
}

QWidget *DevicePanel::buildRawTab()
{
    m_raw = new QPlainTextEdit;
    m_raw->setReadOnly(true);
    m_raw->setMaximumBlockCount(400);   // 只保留最近 400 行，内存和刷新开销恒定
    m_raw->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_raw->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_rawPause = new QCheckBox(QStringLiteral("暂停"));
    m_rawErrorsOnly = new QCheckBox(QStringLiteral("只看异常帧"));
    connect(m_rawPause, &QCheckBox::toggled, this, &DevicePanel::updateRawCapture);
    auto *clear = new QPushButton(QStringLiteral("清空"));
    connect(clear, &QPushButton::clicked, m_raw, &QPlainTextEdit::clear);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_rawPause);
    bar->addWidget(m_rawErrorsOnly);
    bar->addStretch();
    bar->addWidget(new QLabel(QStringLiteral("每 33ms 最多 60 帧，超出部分不显示")));
    bar->addWidget(clear);

    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 0);
    v->addLayout(bar);
    v->addWidget(m_raw, 1);
    return w;
}

QWidget *DevicePanel::buildParamTab()
{
    m_paramTarget = new QComboBox;
    m_paramTarget->addItems({QStringLiteral("当前选中设备"), QStringLiteral("广播（全部设备）")});
    m_param = new QComboBox;
    m_param->addItem(QStringLiteral("负载偏置（%）"), int(Proto::ParamLoadBias));
    m_param->addItem(QStringLiteral("上报周期（ms）"), int(Proto::ParamReportInterval));
    m_paramValue = new QSpinBox;
    auto applyRange = [this] {
        if (m_param->currentData().toInt() == Proto::ParamLoadBias) {
            // 故意放宽到设备允许范围之外，演示设备返回"数值越界"
            m_paramValue->setRange(-100, 100);
            m_paramValue->setValue(60);
        } else {
            m_paramValue->setRange(1, 10000);
            m_paramValue->setValue(100);
        }
    };
    connect(m_param, &QComboBox::currentIndexChanged, this, applyRange);
    applyRange();

    auto *send = new QPushButton(QStringLiteral("下发"));
    send->setDefault(true);
    connect(send, &QPushButton::clicked, this, &DevicePanel::sendParam);

    auto *tip = new QLabel(QStringLiteral(
        "选中一台设备，把负载偏置设为 60，几秒内它会变成告警；设回 0 恢复。<br>"
        "设备允许范围：负载偏置 -50~80，上报周期 20~5000 ms。打开“丢应答”可观察超时重发。<br>"
        "Modbus 模式下映射为写保持寄存器 40002 / 40001（FC06），越界时从站返回异常码 03。"));
    tip->setWordWrap(true);
    tip->setEnabled(false);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("目标"), m_paramTarget);
    form->addRow(QStringLiteral("参数"), m_param);
    form->addRow(QStringLiteral("数值"), m_paramValue);
    form->addRow(QString(), send);

    auto *left = new QVBoxLayout;
    left->addLayout(form);
    left->addWidget(tip);
    left->addStretch();

    m_paramLog = new QListWidget;
    m_paramLog->setToolTip(QStringLiteral("下发记录，最新的在最上面"));

    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 4, 0, 0);
    h->addLayout(left, 2);
    h->addWidget(m_paramLog, 3);
    return w;
}

// ------------------------------------------------------------------ 链路

LinkConfig DevicePanel::currentConfig() const
{
    LinkConfig cfg;
    cfg.kind = LinkConfig::Kind(m_kind->currentIndex());
    cfg.host = m_host->text().trimmed();
    cfg.port = quint16(m_port->value());
    cfg.serialPort = m_serialPort->currentText().trimmed();
    cfg.baudRate = m_baud->currentData().toInt();
    if (cfg.kind == LinkConfig::Virtual && m_deviceIds)
        cfg.deviceIds = m_deviceIds();
    cfg.modbus = m_protocol->currentIndex() == 1;
    cfg.pollMs = m_pollMs->value();
    // 解析 "1-39" / "1,3,5-8"
    for (const QString &part : m_slaves->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList ab = part.trimmed().split(QLatin1Char('-'));
        const int a = ab.value(0).toInt();
        const int b = ab.size() > 1 ? ab.value(1).toInt() : a;
        for (int i = std::max(1, a); i <= std::min(247, b); ++i)
            cfg.modbusSlaves << i;
    }
    return cfg;
}

void DevicePanel::toggleConnection()
{
    if (m_comm->state() == LinkState::Disconnected) {
        applyFaults();
        m_comm->open(currentConfig());
    } else {
        m_comm->close();
        m_monitor->reset(true);   // 主动断开：节点恢复为正常，不产生离线告警
    }
}

void DevicePanel::connectVirtual()
{
    m_kind->setCurrentIndex(LinkConfig::Virtual);
    applyFaults();
    m_comm->open(currentConfig());
}

void DevicePanel::restartIfVirtual()
{
    if (m_comm->state() != LinkState::Disconnected && m_comm->kind() == LinkConfig::Virtual)
        m_comm->open(currentConfig());   // start() 内部会先 stop()
}

void DevicePanel::applyFaults()
{
    FaultConfig f;
    f.fragment = m_fragment->isChecked();
    f.corruptPermille = m_corrupt->value();
    f.dropAckPercent = m_dropAck->value();
    m_comm->setFaults(f);
}

void DevicePanel::refreshSerialPorts()
{
#ifdef HAS_SERIALPORT
    const QString keep = m_serialPort->currentText();
    m_serialPort->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        m_serialPort->addItem(info.systemLocation());
    if (!keep.isEmpty())
        m_serialPort->setCurrentText(keep);
#endif
}

void DevicePanel::onStateChanged(LinkState s, const QString &detail)
{
    QString color, text;
    switch (s) {
    case LinkState::Disconnected: color = QStringLiteral("#8a8f98"); text = QStringLiteral("未连接"); break;
    case LinkState::Connecting: color = QStringLiteral("#e09b10"); text = QStringLiteral("连接中"); break;
    case LinkState::Connected: color = QStringLiteral("#2ea043"); text = QStringLiteral("已连接"); break;
    case LinkState::Reconnecting: color = QStringLiteral("#d93025"); text = QStringLiteral("重连中"); break;
    }
    m_stateLabel->setText(QStringLiteral("<span style='color:%1'>●</span> <b>%2</b> %3")
                              .arg(color, text, detail.toHtmlEscaped()));
    m_connectBtn->setText(s == LinkState::Disconnected ? QStringLiteral("连接") : QStringLiteral("断开"));
    m_kind->setEnabled(s == LinkState::Disconnected);
    m_protocol->setEnabled(s == LinkState::Disconnected);
    m_kindPages->setEnabled(s == LinkState::Disconnected);
    m_modbusBar->setEnabled(s == LinkState::Disconnected);
}

void DevicePanel::onStats(const LinkStats &st)
{
    auto warn = [](quint64 v, const QString &text) {
        return v ? QStringLiteral("<span style='color:#d93025'>%1</span>").arg(text) : text;
    };
    if (st.modbus) {
        m_statsLabel->setText(
            QStringLiteral("Modbus 请求 %1 次/s · 应答 %2 次/s · 平均往返 %3 ms · %4 · %5 · %6 · %7 · %8 · 通信：%9")
                .arg(st.requestsPerSec, 0, 'f', 0)
                .arg(st.framesPerSec, 0, 'f', 0)
                .arg(st.avgRttMs, 0, 'f', 1)
                .arg(warn(st.mbTimeouts, QStringLiteral("超时 %1").arg(st.mbTimeouts)))
                .arg(warn(st.mbExceptions, QStringLiteral("异常应答 %1").arg(st.mbExceptions)))
                .arg(warn(st.crcErrors, QStringLiteral("CRC 错误 %1").arg(st.crcErrors)))
                .arg(warn(quint64(st.retries), QStringLiteral("重发 %1").arg(st.retries)))
                .arg(warn(quint64(st.reconnects), QStringLiteral("重连 %1").arg(st.reconnects)))
                .arg(m_comm->threadDescription()));
        return;
    }
    m_statsLabel->setText(
        QStringLiteral("收 %1 KB/s · %2 帧/s · %3 · 丢弃 %4 B · %5 · %6 · %7 · 投递界面 %8 批/s · 通信：%9")
            .arg(st.rxBytesPerSec / 1024.0, 0, 'f', 1)
            .arg(st.framesPerSec, 0, 'f', 0)
            .arg(warn(st.crcErrors, QStringLiteral("CRC 错误 %1").arg(st.crcErrors)))
            .arg(st.discardedBytes)
            .arg(warn(quint64(st.reconnects), QStringLiteral("重连 %1").arg(st.reconnects)))
            .arg(warn(quint64(st.retries), QStringLiteral("重发 %1").arg(st.retries)))
            .arg(warn(quint64(st.ackTimeouts), QStringLiteral("应答超时 %1").arg(st.ackTimeouts)))
            .arg(st.batchesPerSec)
            .arg(m_comm->threadDescription()));
}

// ------------------------------------------------------------------ 报文监视

void DevicePanel::updateRawCapture()
{
    // 只在报文页可见且未暂停时让通信线程抓原始帧，省掉不必要的拷贝和跨线程传输
    m_comm->setRawCapture(m_tabs->currentWidget() == m_rawTab && !m_rawPause->isChecked());
}

void DevicePanel::onRawFrames(const QVector<RawLog> &batch)
{
    if (m_rawPause->isChecked())
        return;
    QString text;
    for (const RawLog &r : batch) {
        if (m_rawErrorsOnly->isChecked() && r.crcOk)
            continue;
        const quint8 cmd = r.bytes.size() > 2 ? quint8(r.bytes[2]) : 0;
        QString what;
        if (!r.crcOk)
            what = QStringLiteral("CRC错误");
        else if (m_comm->isModbus())
            what = Modbus::describe(r.bytes, m_comm->kind() == LinkConfig::Tcp);
        else
            what = Proto::cmdName(cmd);
        if (!text.isEmpty())
            text += QLatin1Char('\n');
        text += QStringLiteral("%1 %2 %3 %4 %5")
                    .arg(clockText(r.tMs), r.tx ? QStringLiteral("TX →") : QStringLiteral("RX ←"))
                    .arg(what, -6)
                    .arg(Proto::toHex(r.bytes), r.crcOk ? QString() : QStringLiteral("  ✗ 丢弃"));
    }
    if (!text.isEmpty())
        m_raw->appendPlainText(text);   // 一批只追加一次，避免逐行触发排版
}

// ------------------------------------------------------------------ 参数下发

void DevicePanel::sendParam()
{
    int target = Proto::kBroadcast;
    QString targetName = QStringLiteral("全部设备");
    if (m_paramTarget->currentIndex() == 0) {
        if (m_currentId <= 0) {
            auto *item = new QListWidgetItem(QStringLiteral("请先在拓扑图中选中一台设备"));
            item->setForeground(QColor(0xd9, 0x30, 0x25));
            m_paramLog->insertItem(0, item);
            return;
        }
        target = m_currentId;
        targetName = QStringLiteral("%1 (#%2)").arg(m_currentName).arg(m_currentId);
    }
    const int param = m_param->currentData().toInt();
    const int value = m_paramValue->value();
    const int req = m_comm->writeParam(target, param, value);

    auto *item = new QListWidgetItem(QStringLiteral("%1  → %2 %3 = %4，等待应答…")
                                         .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), targetName,
                                              Proto::paramName(quint8(param)))
                                         .arg(value));
    item->setForeground(QColor(0xe0, 0x9b, 0x10));
    m_paramLog->insertItem(0, item);
    m_pendingItems.insert(req, item);
    while (m_paramLog->count() > 100)
        delete m_paramLog->takeItem(m_paramLog->count() - 1);
}

void DevicePanel::onParamResult(int requestId, bool ok, const QString &text)
{
    QListWidgetItem *item = m_pendingItems.take(requestId);
    // 记录可能已被挤出列表，此时 item 指针失效，用行号确认它还在
    if (!item || m_paramLog->row(item) < 0)
        return;
    item->setText(QStringLiteral("%1  %2 %3")
                      .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), ok ? QStringLiteral("✓") : QStringLiteral("✗"),
                           text));
    item->setForeground(ok ? QColor(0x2e, 0xa0, 0x43) : QColor(0xd9, 0x30, 0x25));
}

// ------------------------------------------------------------------ 其他

void DevicePanel::setCurrentDevice(int id, const QString &name)
{
    if (id <= 0)
        return;   // 取消选择时保持当前曲线，方便边看边操作
    m_currentId = id;
    m_currentName = name;
    m_chart->setSeries(id, QStringLiteral("%1  #%2").arg(name).arg(id));
    refreshGauges();
    m_paramTarget->setItemText(0, QStringLiteral("当前选中：%1").arg(name));
    m_comm->setRegisterWatch(id);
    m_regUpdatedMs = 0;
    for (int r = 0; r < m_regTable->rowCount(); ++r)
        for (int c : {3, 4})
            m_regTable->item(r, c)->setText(QStringLiteral("--"));
}

void DevicePanel::runChartStress()
{
    m_monitor->store()->fillSynthetic(kStressSeriesId, 200000, Clock::nowMs(), 1);
    m_chart->setSeries(kStressSeriesId, QStringLiteral("压测数据：200,000 点 @ 1kHz"));
    m_span->setCurrentIndex(m_span->count() - 1);   // 全部
    m_tabs->setCurrentIndex(0);
}

// ------------------------------------------------------------------ 仪表盘

QWidget *DevicePanel::buildGaugeTab()
{
    const auto &th = m_monitor->thresholds();
    struct Def { const char *title; const char *unit; double max, warn, alarm; int decimals; };
    const Def defs[4] = {
        {"CPU", "%", 100, th.cpuWarn, th.cpuAlarm, 1},
        {"温度", "℃", 100, th.tempWarn, th.tempAlarm, 1},
        {"入流量", "Mbps", 1000, 800, 950, 0},
        {"出流量", "Mbps", 1000, 800, 950, 0},
    };
    auto *row = new QHBoxLayout;
    for (int i = 0; i < 4; ++i) {
        auto *g = new GaugeWidget;
        g->setTitle(QString::fromUtf8(defs[i].title));
        g->setUnit(QString::fromUtf8(defs[i].unit));
        g->setRange(0, defs[i].max);
        g->setThresholds(defs[i].warn, defs[i].alarm);
        g->setDecimals(defs[i].decimals);
        m_gauges[i] = g;
        row->addWidget(g);
    }
    m_gaugeTitle = new QLabel;
    m_gaugeTitle->setTextFormat(Qt::RichText);

    // 仪表 10Hz 取最新值；指针自己做缓动，看起来比数据刷新率顺滑
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DevicePanel::refreshGauges);
    timer->start(100);

    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 0);
    v->addWidget(m_gaugeTitle);
    v->addLayout(row, 1);
    return w;
}

void DevicePanel::refreshGauges()
{
    if (!m_gaugeTab || !m_gaugeTab->isVisible())
        return;   // 不可见时不更新，指针动画也随之停下
    const Sample *s = m_currentId > 0 ? m_monitor->latest(m_currentId) : nullptr;
    const qint64 age = s ? Clock::nowMs() - s->tMs : -1;
    const bool online = s && age < m_monitor->thresholds().offlineMs;
    for (int i = 0; i < 4; ++i) {
        if (online)
            m_gauges[i]->setValue(s->v[size_t(i)]);
        else
            m_gauges[i]->clearValue();
    }
    if (m_currentId <= 0)
        m_gaugeTitle->setText(QStringLiteral("在拓扑图中选择一台设备"));
    else if (online)
        m_gaugeTitle->setText(QStringLiteral("<b>%1</b> #%2 · <span style='color:#2ea043'>在线</span> · %3 ms 前更新")
                                  .arg(m_currentName.toHtmlEscaped()).arg(m_currentId).arg(age));
    else
        m_gaugeTitle->setText(QStringLiteral("<b>%1</b> #%2 · <span style='color:#8a8f98'>无数据 / 离线</span>")
                                  .arg(m_currentName.toHtmlEscaped()).arg(m_currentId));
}

// ------------------------------------------------------------------ 历史查询

QWidget *DevicePanel::buildHistoryTab()
{
    m_histRange = new QComboBox;
    m_histRange->addItem(QStringLiteral("最近 10 分钟"), 600);
    m_histRange->addItem(QStringLiteral("最近 1 小时"), 3600);
    m_histRange->addItem(QStringLiteral("最近 24 小时"), 86400);
    m_histRange->addItem(QStringLiteral("自定义"), -1);
    const QDateTime now = QDateTime::currentDateTime();
    m_histFrom = new QDateTimeEdit(now.addSecs(-600));
    m_histTo = new QDateTimeEdit(now);
    for (QDateTimeEdit *e : {m_histFrom, m_histTo}) {
        e->setDisplayFormat(QStringLiteral("MM-dd HH:mm:ss"));
        e->setEnabled(false);
    }
    connect(m_histRange, &QComboBox::currentIndexChanged, this, [this](int i) {
        const bool custom = m_histRange->itemData(i).toInt() < 0;
        m_histFrom->setEnabled(custom);
        m_histTo->setEnabled(custom);
    });

    auto *queryBtn = new QPushButton(QStringLiteral("查询"));
    connect(queryBtn, &QPushButton::clicked, this, &DevicePanel::runHistoryQuery);
    auto *csvBtn = new QPushButton(QStringLiteral("导出 CSV"));
    connect(csvBtn, &QPushButton::clicked, this, &DevicePanel::exportCsv);
    auto *clearBtn = new QPushButton(QStringLiteral("清空历史库"));
    connect(clearBtn, &QPushButton::clicked, this, &DevicePanel::confirmClearHistory);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_histRange);
    bar->addWidget(m_histFrom);
    bar->addWidget(new QLabel(QStringLiteral("~")));
    bar->addWidget(m_histTo);
    bar->addWidget(queryBtn);
    bar->addWidget(csvBtn);
    bar->addStretch();
    bar->addWidget(clearBtn);

    m_histChart = new RealtimeChart;
    m_histChart->setLiveEnabled(false);
    m_histChart->setStore(&m_histStore);
    m_histChart->setLanes(m_chart->lanes());
    m_histChart->setEmptyText(QStringLiteral("选择设备和时间范围后点“查询”"));

    m_histAlarms = new QListWidget;
    m_histAlarms->setToolTip(QStringLiteral("该时间段内这台设备的告警记录（最新在上）"));

    auto *split = new QSplitter;
    split->addWidget(m_histChart);
    split->addWidget(m_histAlarms);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    m_histInfo = new QLabel;
    m_histStats = new QLabel;
    m_histStats->setEnabled(false);

    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 0);
    v->setSpacing(2);
    v->addLayout(bar);
    v->addWidget(m_histInfo);
    v->addWidget(split, 1);
    v->addWidget(m_histStats);
    return w;
}

void DevicePanel::runHistoryQuery()
{
    if (m_currentId <= 0) {
        m_histInfo->setText(QStringLiteral("请先在拓扑图中选中一台设备"));
        return;
    }
    HistoryQuery q;
    q.deviceId = m_currentId;
    const int range = m_histRange->currentData().toInt();
    if (range > 0) {
        q.toWallMs = QDateTime::currentMSecsSinceEpoch();
        q.fromWallMs = q.toWallMs - qint64(range) * 1000;
    } else {
        q.fromWallMs = m_histFrom->dateTime().toMSecsSinceEpoch();
        q.toWallMs = m_histTo->dateTime().toMSecsSinceEpoch();
        if (q.toWallMs <= q.fromWallMs) {
            m_histInfo->setText(QStringLiteral("结束时间必须晚于开始时间"));
            return;
        }
    }
    m_histRequest = m_history->query(q);
    m_histInfo->setText(QStringLiteral("查询中…"));
}

void DevicePanel::onHistoryResult(const HistoryResult &r)
{
    if (r.requestId != m_histRequest)
        return;   // 用户连点了几次，只认最后一次
    if (!r.error.isEmpty()) {
        m_histInfo->setText(QStringLiteral("<span style='color:#d93025'>查询失败：%1</span>").arg(r.error.toHtmlEscaped()));
        return;
    }
    m_histResult = r;

    // 历史库存的是墙上时间，曲线控件用单调时钟，换算过去
    const qint64 offset = QDateTime::currentMSecsSinceEpoch() - Clock::nowMs();
    QVector<TelemetryPoint> pts = r.points;
    for (TelemetryPoint &p : pts)
        p.t -= offset;
    m_histStore.setSeries(r.query.deviceId, pts);
    const QString fmt = QStringLiteral("MM-dd HH:mm");
    m_histChart->setSeries(r.query.deviceId,
                           QStringLiteral("%1 #%2  %3 ~ %4")
                               .arg(m_currentName)
                               .arg(r.query.deviceId)
                               .arg(QDateTime::fromMSecsSinceEpoch(r.query.fromWallMs).toString(fmt),
                                    QDateTime::fromMSecsSinceEpoch(r.query.toWallMs).toString(fmt)));
    m_histChart->showRange(r.query.fromWallMs - offset, r.query.toWallMs - offset);

    m_histInfo->setText(QStringLiteral("%1 个点 · %2 · %3 条告警 · 查询耗时 %4 ms")
                            .arg(r.points.size())
                            .arg(r.bucketMs ? QStringLiteral("数据量大，按 %1 秒聚合（CPU/温度取峰值，流量取均值）").arg(r.bucketMs / 1000)
                                            : QStringLiteral("原始 1 秒数据"))
                            .arg(r.alarms.size())
                            .arg(r.elapsedMs));

    m_histAlarms->clear();
    for (const HistoryAlarm &a : r.alarms) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  [%2] %3")
                                             .arg(QDateTime::fromMSecsSinceEpoch(a.wallMs).toString(QStringLiteral("MM-dd HH:mm:ss")),
                                                  statusName(NodeStatus(a.level)), a.text));
        item->setForeground(statusColor(NodeStatus(a.level)));
        m_histAlarms->addItem(item);
    }
    if (r.alarms.isEmpty())
        m_histAlarms->addItem(QStringLiteral("该时段无告警"));
}

void DevicePanel::onHistoryStats(const HistoryStats &s)
{
    const bool mem = s.path == QLatin1String(":memory:");
    m_histStats->setText(QStringLiteral("SQLite %1 · 遥测 %2 行 · 告警 %3 行%4 · 写入 %5 行/s · 提交 %6 ms · 保留 %7 小时 · %8")
                             .arg(mem ? QStringLiteral("内存库") : QFileInfo(s.path).fileName())
                             .arg(s.sampleRows)
                             .arg(s.alarmRows)
                             .arg(s.fileBytes ? QStringLiteral(" · %1 MB").arg(s.fileBytes / 1048576.0, 0, 'f', 2) : QString())
                             .arg(s.rowsPerSec, 0, 'f', 0)
                             .arg(s.lastCommitMs, 0, 'f', 2)
                             .arg(s.retentionHours)
                             .arg(m_history->isThreaded() ? QStringLiteral("写库线程") : QStringLiteral("主线程")));
    m_histStats->setToolTip(mem ? QStringLiteral("网页版没有持久文件系统，历史库在内存里，刷新页面即清空") : s.path);
}

void DevicePanel::exportCsv()
{
    if (m_histResult.points.isEmpty()) {
        m_histInfo->setText(QStringLiteral("没有可导出的数据，请先查询"));
        return;
    }
    // 带 BOM 的 UTF-8，Excel 打开中文表头不乱码
    QByteArray csv("\xEF\xBB\xBF");
    csv += QStringLiteral("时间,CPU(%),温度(℃),入流量(Mbps),出流量(Mbps)\n").toUtf8();
    for (const TelemetryPoint &p : std::as_const(m_histResult.points)) {
        csv += QDateTime::fromMSecsSinceEpoch(p.t).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")).toUtf8();
        for (float v : p.v)
            csv += ',' + QByteArray::number(v, 'f', 2);
        csv += '\n';
    }
    const QString name = QStringLiteral("history-%1-%2.csv")
                             .arg(m_histResult.query.deviceId)
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
#ifdef Q_OS_WASM
    QFileDialog::saveFileContent(csv, name);   // 浏览器下载
#else
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CSV"), name, QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(csv) != csv.size()) {
        m_histInfo->setText(QStringLiteral("写入失败：%1").arg(path));
        return;
    }
    m_histInfo->setText(QStringLiteral("已导出 %1 行到 %2").arg(m_histResult.points.size()).arg(path));
#endif
}

void DevicePanel::confirmClearHistory()
{
    auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("清空历史库"),
                                QStringLiteral("删除全部历史遥测和告警记录？此操作不能撤销。"),
                                QMessageBox::Yes | QMessageBox::No, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QMessageBox::finished, this, [this](int r) {
        if (r != QMessageBox::Yes)
            return;
        m_history->clearAll();
        m_histStore.clear();
        m_histResult = {};
        m_histAlarms->clear();
        m_histInfo->setText(QStringLiteral("历史库已清空"));
    });
    box->open();   // 非阻塞，WASM 下不能用 exec()
}

// ------------------------------------------------------------------ 寄存器

QWidget *DevicePanel::buildRegisterTab()
{
    m_regTitle = new QLabel;
    m_regTitle->setTextFormat(Qt::RichText);
    m_regTitle->setWordWrap(true);

    struct Row { const char *addr, *name, *type; };
    const Row rows[] = {
        {"30001", "CPU 使用率", "uint16 ×0.1 %"},
        {"30002", "温度", "int16 ×0.1 ℃"},
        {"30003-30004", "入流量", "float32 ABCD"},
        {"30005-30006", "出流量", "float32 ABCD"},
        {"40001", "采样周期", "uint16 ms"},
        {"40002", "负载偏置", "int16 %"},
    };
    m_regTable = new QTableWidget(int(std::size(rows)), 5);
    m_regTable->setHorizontalHeaderLabels({QStringLiteral("地址"), QStringLiteral("名称"), QStringLiteral("类型"),
                                           QStringLiteral("原始值"), QStringLiteral("工程值")});
    m_regTable->verticalHeader()->hide();
    m_regTable->verticalHeader()->setDefaultSectionSize(22);
    m_regTable->horizontalHeader()->setStretchLastSection(true);
    m_regTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_regTable->setSelectionMode(QAbstractItemView::NoSelection);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (int r = 0; r < int(std::size(rows)); ++r) {
        const char *cells[3] = {rows[r].addr, rows[r].name, rows[r].type};
        for (int c = 0; c < 5; ++c) {
            auto *item = new QTableWidgetItem(c < 3 ? QString::fromUtf8(cells[c]) : QStringLiteral("--"));
            if (c == 0 || c == 3)
                item->setFont(mono);
            m_regTable->setItem(r, c, item);
        }
    }
    m_regTable->resizeColumnsToContents();
    m_regTable->setColumnWidth(3, 120);

    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 0);
    v->addWidget(m_regTitle);
    v->addWidget(m_regTable, 1);

    // 标题里显示"多久前更新"，每秒刷新一次
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!m_regTitle->isVisible())
            return;
        if (!m_comm->isModbus() || m_comm->state() != LinkState::Connected) {
            m_regTitle->setText(QStringLiteral("切换到 <b>Modbus</b> 协议并连接后，这里显示选中从站的原始寄存器。"
                                               "float32 占两个寄存器，高字在前（ABCD），不同厂商可能是 CDAB，接入时要按手册核对。"));
            return;
        }
        const QString age = m_regUpdatedMs ? QStringLiteral("%1 ms 前更新").arg(Clock::nowMs() - m_regUpdatedMs)
                                           : QStringLiteral("等待数据…");
        m_regTitle->setText(QStringLiteral("从站 <b>%1</b>（%2）· FC04 读 30001-30006，FC03 读 40001-40002，每轮 %3 ms · %4")
                                .arg(m_currentId)
                                .arg(m_currentName.toHtmlEscaped())
                                .arg(m_pollMs->value())
                                .arg(age));
    });
    timer->start(1000);
    return w;
}

void DevicePanel::onRegisters(int slave, const QVector<quint16> &in, const QVector<quint16> &hold)
{
    if (slave != m_currentId)
        return;
    m_regUpdatedMs = Clock::nowMs();
    auto hex = [](quint16 v) { return QStringLiteral("0x%1").arg(v, 4, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x")); };
    auto set = [this](int row, const QString &raw, const QString &value) {
        m_regTable->item(row, 3)->setText(raw);
        m_regTable->item(row, 4)->setText(value);
    };
    if (in.size() >= Modbus::kInputCount) {
        set(0, hex(in[0]), QStringLiteral("%1 %").arg(in[0] / 10.0, 0, 'f', 1));
        set(1, hex(in[1]), QStringLiteral("%1 ℃").arg(qint16(in[1]) / 10.0, 0, 'f', 1));
        set(2, hex(in[2]) + QLatin1Char(' ') + hex(in[3]),
            QStringLiteral("%1 Mbps").arg(Modbus::toFloat(in[2], in[3]), 0, 'f', 2));
        set(3, hex(in[4]) + QLatin1Char(' ') + hex(in[5]),
            QStringLiteral("%1 Mbps").arg(Modbus::toFloat(in[4], in[5]), 0, 'f', 2));
    }
    if (hold.size() >= Modbus::kHoldingCount) {
        set(4, hex(hold[0]), QStringLiteral("%1 ms").arg(hold[0]));
        set(5, hex(hold[1]), QStringLiteral("%1 %").arg(qint16(hold[1])));
    }
}
