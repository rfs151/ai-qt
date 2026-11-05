#pragma once

#include "comm/CommTypes.h"
#include "comm/TelemetryStore.h"
#include "history/HistoryDb.h"

#include <QHash>
#include <QWidget>
#include <functional>

class CommManager;
class DeviceMonitor;
class GaugeWidget;
class HistoryManager;
class QDateTimeEdit;
class RealtimeChart;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QWidget;

// "设备通信"停靠面板：链路配置与故障注入、实时曲线、报文监视、参数下发、通信统计
class DevicePanel : public QWidget
{
    Q_OBJECT
public:
    DevicePanel(CommManager *comm, DeviceMonitor *monitor, HistoryManager *history, QWidget *parent = nullptr);

    // 连接虚拟设备时，从拓扑里取要模拟的设备 ID
    void setDeviceIdProvider(std::function<QList<int>()> fn) { m_deviceIds = std::move(fn); }
    void setCurrentDevice(int id, const QString &name);   // id 为 0 表示没有选中
    void connectVirtual();
    void restartIfVirtual();   // 拓扑重建后用新的设备列表重连

private:
    QWidget *buildLinkBar();
    QWidget *buildFaultBar();
    QWidget *buildChartTab();
    QWidget *buildRawTab();
    QWidget *buildParamTab();
    QWidget *buildGaugeTab();
    QWidget *buildHistoryTab();
    QWidget *buildModbusBar();
    QWidget *buildRegisterTab();
    void onRegisters(int slave, const QVector<quint16> &input, const QVector<quint16> &holding);
    void updateOptionRows();
    void refreshGauges();
    void runHistoryQuery();
    void onHistoryResult(const HistoryResult &r);
    void onHistoryStats(const HistoryStats &s);
    void exportCsv();
    void confirmClearHistory();

    void toggleConnection();
    LinkConfig currentConfig() const;
    void applyFaults();
    void onStateChanged(LinkState s, const QString &detail);
    void onStats(const LinkStats &st);
    void onRawFrames(const QVector<RawLog> &batch);
    void onParamResult(int requestId, bool ok, const QString &text);
    void sendParam();
    void updateRawCapture();
    void refreshSerialPorts();
    void runChartStress();

    CommManager *m_comm;
    DeviceMonitor *m_monitor;
    HistoryManager *m_history;
    std::function<QList<int>()> m_deviceIds;
    int m_currentId = 0;
    QString m_currentName;

    // 链路
    QComboBox *m_kind;
    QComboBox *m_protocol;
    QWidget *m_modbusBar;
    QLineEdit *m_slaves;
    QLabel *m_slavesLabel;
    QSpinBox *m_pollMs;
    QStackedWidget *m_kindPages;
    QLineEdit *m_host;
    QSpinBox *m_port;
    QComboBox *m_serialPort;
    QComboBox *m_baud;
    QPushButton *m_connectBtn;
    QLabel *m_stateLabel;
    QWidget *m_faultBar;
    QCheckBox *m_fragment;
    QSpinBox *m_corrupt;
    QSpinBox *m_dropAck;

    QTabWidget *m_tabs;

    // 曲线
    RealtimeChart *m_chart;
    QComboBox *m_span;
    QPushButton *m_pauseBtn;

    // 报文
    QPlainTextEdit *m_raw;
    QCheckBox *m_rawPause;
    QCheckBox *m_rawErrorsOnly;

    // 参数
    QComboBox *m_paramTarget;
    QComboBox *m_param;
    QSpinBox *m_paramValue;
    QListWidget *m_paramLog;
    QHash<int, QListWidgetItem *> m_pendingItems;

    QLabel *m_statsLabel;
    QWidget *m_rawTab = nullptr;

    // 仪表盘
    QWidget *m_gaugeTab = nullptr;
    QLabel *m_gaugeTitle;
    GaugeWidget *m_gauges[4];

    // 寄存器
    QLabel *m_regTitle;
    QTableWidget *m_regTable;
    qint64 m_regUpdatedMs = 0;

    // 历史
    QComboBox *m_histRange;
    QDateTimeEdit *m_histFrom;
    QDateTimeEdit *m_histTo;
    RealtimeChart *m_histChart;
    TelemetryStore m_histStore;
    QListWidget *m_histAlarms;
    QLabel *m_histInfo;
    QLabel *m_histStats;
    int m_histRequest = 0;
    HistoryResult m_histResult;
};
